
// Copyright 2021 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <glad/glad.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/quaternion.hpp>

#include <iostream>
#include <cstring>
#include <map>
#include <vector>
#include <chrono>

#include <chezscheme.h>
#include <racketcs.h>

#include <nfd.h>

#include "errors.h"
#include "gl_boilerplate.h"
#include "../shaders/defines.h"

#if _WIN64
#define TANGERINE_API __declspec(dllexport)
#elif defined(__GNUC__)
#define TANGERINE_API __attribute__ ((visibility ("default")))
#endif

#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2


struct SectionUpload
{
	glm::mat4 LocalToWorld;
	glm::mat4 WorldToLocal;
	glm::vec4 Center;
	glm::vec4 Extent;
};


struct SubtreeSection
{
	SubtreeSection(glm::mat4 LocalToWorld, glm::vec4 Center, glm::vec4 Extent)
	{
		SectionData.LocalToWorld = LocalToWorld;
		SectionData.WorldToLocal = glm::inverse(LocalToWorld);
		SectionData.Center = Center;
		SectionData.Extent = Extent;

		SectionBuffer.DebugName = "Subtree Section Buffer";
		SectionBuffer.Upload((void*)&SectionData, sizeof(SectionUpload));
	}
	void Release()
	{
		SectionBuffer.Release();
	}
	SectionUpload SectionData;
	Buffer SectionBuffer;
};


struct ModelSubtree
{
	ModelSubtree(size_t ParamCount, float* InParams)
	{
		size_t Padding = DIV_UP(ParamCount, 4) * 4 - ParamCount;
		size_t UploadSize = ParamCount + Padding;
		Params.reserve(UploadSize);
		for (int i = 0; i < ParamCount; ++i)
		{
			Params.push_back(InParams[i]);
		}
		for (int i = 0; i < Padding; ++i)
		{
			Params.push_back(0.0);
		}

		ParamsBuffer.DebugName = "Subtree Parameter Buffer";
		ParamsBuffer.Upload((void*)Params.data(), UploadSize * sizeof(float));
	}
	void Release()
	{
		for (SubtreeSection& Section : Sections)
		{
			Section.Release();
		}
		Sections.clear();
	}
	std::vector<float> Params;
	std::vector<SubtreeSection> Sections;
	Buffer ParamsBuffer;
};


struct SubtreeShader
{
	SubtreeShader(SubtreeShader&& Old)
		: DebugName(Old.DebugName)
		, DistSource(Old.DistSource)
		, IsValid(Old.IsValid)
	{
		Old.IsValid = false;
		std::swap(DepthShader, Old.DepthShader);
		std::swap(DepthQuery, Old.DepthQuery);
		std::swap(Instances, Old.Instances);
	}
	SubtreeShader(std::string InDebugName, std::string InDistSource)
	{
		DebugName = InDebugName;
		DistSource = InDistSource;
		IsValid = false;
	}
	StatusCode Compile()
	{
		StatusCode Result = DepthShader.Setup(
			{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_draw.vs.glsl", true)},
			  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", DistSource, "shaders/cluster_draw.fs.glsl")} },
			DebugName.c_str());

		if (Result == StatusCode::FAIL)
		{
			DepthShader.Reset();
			return Result;
		}

		glGenQueries(1, &DepthQuery);
		IsValid = true;
		return StatusCode::PASS;
	}
	void Reset()
	{
		for (ModelSubtree& Subtree : Instances)
		{
			Subtree.Release();
		}
		Instances.clear();
	}
	void Release()
	{
		Reset();
		DepthShader.Reset();
		if (IsValid)
		{
			IsValid = false;
			glDeleteQueries(1, &DepthQuery);
		}
	}
	bool IsValid;
	std::string DebugName;
	std::string DistSource;
	ShaderPipeline DepthShader;
	GLuint DepthQuery;

	std::vector<ModelSubtree> Instances;
};


std::map<std::string, size_t> SubtreeMap;
std::vector<SubtreeShader> SubtreeShaders;
std::vector<size_t> PendingShaders;


extern "C" size_t TANGERINE_API EmitShader(const char* ShaderTree, const char* ShaderSource)
{
	std::string Tree = std::string(ShaderTree);
	std::string Source = std::string(ShaderSource);
	auto Found = SubtreeMap.find(Tree);
	if (Found == SubtreeMap.end())
	{
		size_t Index = SubtreeShaders.size();
		SubtreeShaders.emplace_back(Tree, Source);
		SubtreeMap[Tree] = Index;
		PendingShaders.push_back(Index);
		return Index;
	}
	else
	{
		return Found->second;
	}
}


ModelSubtree* PendingSubtree = nullptr;


extern "C" void TANGERINE_API EmitSubtree(size_t ShaderIndex, size_t ParamCount, float* Params)
{
	SubtreeShaders[ShaderIndex].Instances.emplace_back(ParamCount, Params);
	PendingSubtree = &(SubtreeShaders[ShaderIndex].Instances.back());
}


extern "C" void TANGERINE_API EmitSection(float InExtent[3], float InCenter[3], float Matrix[16])
{
	glm::mat4 LocalToWorld;
	for (int i = 0; i < 4; ++i)
	{
		LocalToWorld[i] = glm::vec4(Matrix[i * 4 + 0], Matrix[i * 4 + 1], Matrix[i * 4 + 2], Matrix[i * 4 + 3]);
	}
	glm::vec4 Center = glm::vec4(InCenter[0], InCenter[1], InCenter[2], 0.0);
	glm::vec4 Extent = glm::vec4(InExtent[0], InExtent[1], InExtent[2], 0.0);
	PendingSubtree->Sections.emplace_back(LocalToWorld, Center, Extent);
}


GLuint NullVAO;
ShaderPipeline PaintShader;
ShaderPipeline NoiseShader;
ShaderPipeline BgShader;

Buffer ViewInfo("ViewInfo Buffer");
Buffer OutlinerOptions("Outliner Options Buffer");
Buffer InstanceInfo("InstanceInfo Buffer");

Buffer DepthTimeBuffer("Subtree Heatmap Buffer");
GLuint DepthPass;
GLuint DepthBuffer;

GLuint PositionBuffer;
GLuint NormalBuffer;
GLuint IDBuffer;
const GLuint FinalPass = 0;

GLuint DepthTimeQuery;
GLuint GridBgTimeQuery;
GLuint OutlinerTimeQuery;
GLuint UiTimeQuery;


void AllocateRenderTargets(int ScreenWidth, int ScreenHeight)
{
	static bool Initialized = false;
	if (Initialized)
	{
		glDeleteFramebuffers(1, &DepthPass);
		glDeleteTextures(1, &DepthBuffer);
		glDeleteTextures(1, &PositionBuffer);
		glDeleteTextures(1, &NormalBuffer);
	}
	else
	{
		Initialized = true;
	}

	// Depth Pass
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &DepthBuffer);
		glTextureStorage2D(DepthBuffer, 1, GL_DEPTH_COMPONENT32F, ScreenWidth, ScreenHeight);
		glTextureParameteri(DepthBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(DepthBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(DepthBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(DepthBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, DepthBuffer, -1, "DepthBuffer");

		glCreateTextures(GL_TEXTURE_2D, 1, &PositionBuffer);
		glTextureStorage2D(PositionBuffer, 1, GL_RGB32F, ScreenWidth, ScreenHeight);
		glTextureParameteri(PositionBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(PositionBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(PositionBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(PositionBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, PositionBuffer, -1, "World Position");

		glCreateTextures(GL_TEXTURE_2D, 1, &NormalBuffer);
#if VISUALIZE_TRACING_ERROR
		glTextureStorage2D(NormalBuffer, 1, GL_RGBA8_SNORM, ScreenWidth, ScreenHeight);
#else
		glTextureStorage2D(NormalBuffer, 1, GL_RGB8_SNORM, ScreenWidth, ScreenHeight);
#endif
		glTextureParameteri(NormalBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(NormalBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(NormalBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(NormalBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, NormalBuffer, -1, "World Normal");

		glCreateTextures(GL_TEXTURE_2D, 1, &IDBuffer);
		glTextureStorage2D(IDBuffer, 1, GL_R32UI, ScreenWidth, ScreenHeight);
		glTextureParameteri(IDBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(IDBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(IDBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(IDBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, IDBuffer, -1, "Subtree ID");

		glCreateFramebuffers(1, &DepthPass);
		glObjectLabel(GL_FRAMEBUFFER, DepthPass, -1, "DepthPass");
		glNamedFramebufferTexture(DepthPass, GL_DEPTH_ATTACHMENT, DepthBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT0, PositionBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT1, NormalBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT2, IDBuffer, 0);
		GLenum ColorAttachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
		glNamedFramebufferDrawBuffers(DepthPass, 3, ColorAttachments);
	}
}


struct ViewInfoUpload
{
	glm::mat4 WorldToView;
	glm::mat4 ViewToWorld;
	glm::mat4 ViewToClip;
	glm::mat4 ClipToView;
	glm::vec4 CameraOrigin;
	glm::vec4 ScreenSize;
	glm::vec4 ModelMin;
	glm::vec4 ModelMax;
	float CurrentTime;
	float Padding[3] = { 0 };
};


struct OutlinerOptionsUpload
{
	GLuint OutlinerFlags;
};


// Renderer setup.
StatusCode SetupRenderer()
{
	// For drawing without a VBO bound.
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

#if VISUALIZE_CLUSTER_COVERAGE
	RETURN_ON_FAIL(ClusterCoverageShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_coverage.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/cluster_coverage.fs.glsl", true)} },
		"Cluster Coverage Shader"));
#else

	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/outliner.fs.glsl", true)} },
		"Outliner Shader"));

	RETURN_ON_FAIL(BgShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/bg.fs.glsl", true)} },
		"Background Shader"));
#endif

	RETURN_ON_FAIL(NoiseShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/noise.fs.glsl", true)} },
		"Noise Shader"));

	glGenQueries(1, &DepthTimeQuery);
	glGenQueries(1, &GridBgTimeQuery);
	glGenQueries(1, &OutlinerTimeQuery);
	glGenQueries(1, &UiTimeQuery);

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	glDepthRange(1.0, 0.0);
	glDepthFunc(GL_GREATER);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepth(0.0);

	return StatusCode::PASS;
}


double ShaderCompilerStallMs = 0.0;
std::vector<SubtreeShader*> Drawables;
void CompileNewShaders()
{
	Drawables.clear();
	Drawables.reserve(PendingShaders.size());

	using Clock = std::chrono::high_resolution_clock;
	Clock::time_point StartTimePoint = Clock::now();

	for (size_t SubtreeIndex : PendingShaders)
	{
		SubtreeShader& Shader = SubtreeShaders[SubtreeIndex];
		StatusCode Result = Shader.Compile();
		if (Result == StatusCode::PASS && Shader.Instances.size() > 0)
		{
			Drawables.push_back(&Shader);
		}
	}
	PendingShaders.clear();

	Clock::time_point EndTimePoint = Clock::now();
	std::chrono::duration<double, std::milli> Delta = EndTimePoint - StartTimePoint;
	ShaderCompilerStallMs = Delta.count();
}


int MouseMotionX = 0;
int MouseMotionY = 0;
int MouseMotionZ = 0;
int ShowBackground = 0;
bool ShowSubtrees = false;
bool ShowHeatmap = false;
bool ResetCamera = true;
glm::vec4 ModelMin = glm::vec4(0.0);
glm::vec4 ModelMax = glm::vec4(0.0);
float PresentFrequency = 0.0;
float PresentDeltaMs = 0.0;
glm::vec3 CameraFocus = glm::vec3(0.0, 0.0, 0.0);
void RenderFrame(int ScreenWidth, int ScreenHeight)
{
	if (PendingShaders.size() > 0)
	{
		CompileNewShaders();
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}

	double CurrentTime;
	{
		using Clock = std::chrono::high_resolution_clock;
		static Clock::time_point StartTimePoint = Clock::now();
		static Clock::time_point LastTimePoint = StartTimePoint;
		Clock::time_point CurrentTimePoint = Clock::now();
		{
			std::chrono::duration<double, std::milli> FrameDelta = CurrentTimePoint - LastTimePoint;
			PresentDeltaMs = float(FrameDelta.count());
		}
		{
			std::chrono::duration<double, std::milli> EpochDelta = CurrentTimePoint - StartTimePoint;
			CurrentTime = float(EpochDelta.count());
		}
		LastTimePoint = CurrentTimePoint;
		PresentFrequency = float(1000.0 / PresentDeltaMs);
	}

	static int FrameNumber = 0;
	++FrameNumber;

	static int Width = 0;
	static int Height = 0;
	{
		if (ScreenWidth != Width || ScreenHeight != Height)
		{
			Width = ScreenWidth;
			Height = ScreenHeight;
			glViewport(0, 0, Width, Height);
			AllocateRenderTargets(Width, Height);
		}
	}

	{
		static float RotateX;
		static float RotateZ;
		static float Zoom;
		if (ResetCamera)
		{
			ResetCamera = false;
			RotateX = 0.0;
			RotateZ = 0.0;
			Zoom = 14.0;
			CameraFocus = (ModelMax.xyz - ModelMin.xyz) * glm::vec3(0.5) + ModelMin.xyz;
		}

		RotateX = fmodf(RotateX - MouseMotionY, 360.0);
		RotateZ = fmodf(RotateZ - MouseMotionX, 360.0);
		Zoom = fmaxf(0.0, Zoom - MouseMotionZ);
		const float ToRadians = float(M_PI / 180.0);

		glm::mat4 Orientation = glm::identity<glm::mat4>();
		Orientation = glm::rotate(Orientation, RotateZ * ToRadians, glm::vec3(0.0, 0.0, 1.0));
		Orientation = glm::rotate(Orientation, RotateX * ToRadians, glm::vec3(1.0, 0.0, 0.0));

		glm::vec4 Fnord = Orientation * glm::vec4(0.0, -Zoom, 0.0, 1.0);
		glm::vec3 CameraOrigin = glm::vec3(Fnord.x, Fnord.y, Fnord.z) / Fnord.w;

		Fnord = Orientation * glm::vec4(0.0, 0.0, 1.0, 1.0);
		glm::vec3 UpDir = glm::vec3(Fnord.x, Fnord.y, Fnord.z) / Fnord.w;

		const glm::mat4 WorldToView = glm::lookAt(CameraFocus + CameraOrigin, CameraFocus, UpDir);
		const glm::mat4 ViewToWorld = glm::inverse(WorldToView);

		{
			glm::vec4 CameraLocal = ViewToWorld * glm::vec4(0.0, 0.0, 0.0, 1.0);
			CameraLocal /= CameraLocal.w;
			CameraOrigin = glm::vec3(CameraLocal.x, CameraLocal.y, CameraLocal.z);
		}

		const float AspectRatio = float(Width) / float(Height);
		const glm::mat4 ViewToClip = glm::infinitePerspective(glm::radians(45.f), AspectRatio, 1.0f);
		const glm::mat4 ClipToView = inverse(ViewToClip);

		ViewInfoUpload BufferData = {
			WorldToView,
			ViewToWorld,
			ViewToClip,
			ClipToView,
			glm::vec4(CameraOrigin, 1.0f),
			glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height),
			ModelMin,
			ModelMax,
			float(CurrentTime),
		};
		ViewInfo.Upload((void*)&BufferData, sizeof(BufferData));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);
	}

	{
		GLuint OutlinerFlags = 0;
		if (ShowSubtrees)
		{
			OutlinerFlags |= 1;
		}
		if (ShowHeatmap)
		{
			OutlinerFlags |= 1 << 1;
		}
		OutlinerOptionsUpload BufferData = {
			OutlinerFlags
		};
		OutlinerOptions.Upload((void*)&BufferData, sizeof(BufferData));
	}

	if (Drawables.size() > 0)
	{
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth");
			glBeginQuery(GL_TIME_ELAPSED, DepthTimeQuery);
			glBindFramebuffer(GL_FRAMEBUFFER, DepthPass);
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
			glClear(GL_DEPTH_BUFFER_BIT);
			if (ShowHeatmap)
			{
				glEndQuery(GL_TIME_ELAPSED);
			}
			for (SubtreeShader* Shader : Drawables)
			{
				GLsizei DebugNameLen = Shader->DebugName.size() < 100 ? -1 : 100;
				glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, DebugNameLen, Shader->DebugName.c_str());
				if (ShowHeatmap)
				{
					glBeginQuery(GL_TIME_ELAPSED, Shader->DepthQuery);
				}
				Shader->DepthShader.Activate();
				for (ModelSubtree& Subtree : Shader->Instances)
				{
					Subtree.ParamsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 0);
					for (SubtreeSection& Section : Subtree.Sections)
					{
						Section.SectionBuffer.Bind(GL_UNIFORM_BUFFER, 2);
						glDrawArrays(GL_TRIANGLES, 0, 36);
					}
				}
				if (ShowHeatmap)
				{
					glEndQuery(GL_TIME_ELAPSED);
				}
				glPopDebugGroup();
			}
			if (!ShowHeatmap)
			{
				glEndQuery(GL_TIME_ELAPSED);
			}
			glPopDebugGroup();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Background");
			glBeginQuery(GL_TIME_ELAPSED, GridBgTimeQuery);
			glDepthMask(GL_FALSE);
			glDisable(GL_DEPTH_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
			switch (ShowBackground)
			{
			case 0:
				BgShader.Activate();
				glDrawArrays(GL_TRIANGLES, 0, 3);
				break;
			default:
				ShowBackground = -1;
				glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			glEndQuery(GL_TIME_ELAPSED);
			glPopDebugGroup();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Paint");
			glBeginQuery(GL_TIME_ELAPSED, OutlinerTimeQuery);
			glBindTextureUnit(1, DepthBuffer);
			glBindTextureUnit(2, PositionBuffer);
			glBindTextureUnit(3, NormalBuffer);
			glBindTextureUnit(4, IDBuffer);
			OutlinerOptions.Bind(GL_UNIFORM_BUFFER, 1);
			DepthTimeBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			PaintShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glEndQuery(GL_TIME_ELAPSED);
			glPopDebugGroup();
		}
	}
	else
	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Dead Channel");
		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
		NoiseShader.Activate();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glPopDebugGroup();
	}
}


void ToggleFullScreen(SDL_Window* Window)
{
	static bool FullScreen = false;
	FullScreen = !FullScreen;
	SDL_SetWindowFullscreen(Window, FullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}


extern "C" void TANGERINE_API SetLimitsCallback(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
{
	ModelMin = glm::vec4(MinX, MinY, MinZ, 1.0);
	ModelMax = glm::vec4(MaxX, MaxY, MaxZ, 1.0);
}


std::vector<std::string> RacketErrors;
extern "C" void TANGERINE_API RacketErrorCallback(const char* ErrorMessage)
{
	std::cout << ErrorMessage << "\n";
	RacketErrors.push_back(std::string(ErrorMessage));
}


double ModelProcessingStallMs = 0.0;
void LoadModel(nfdchar_t* Path)
{
	static nfdchar_t* LastPath = nullptr;
	if (!Path)
	{
		// Reload
		Path = LastPath;
	}
	else
	{
		ResetCamera = true;
	}
	if (Path)
	{
		for (SubtreeShader& Shader : SubtreeShaders)
		{
			Shader.Release();
		}
		SubtreeShaders.clear();
		SubtreeMap.clear();
		PendingShaders.clear();
		Drawables.clear();

		using Clock = std::chrono::high_resolution_clock;
		Clock::time_point StartTimePoint = Clock::now();

		Sactivate_thread();
		ptr ModuleSymbol = Sstring_to_symbol("tangerine");
		ptr ProcSymbol = Sstring_to_symbol("renderer-load-and-process-model");
		ptr Proc = Scar(racket_dynamic_require(ModuleSymbol, ProcSymbol));
		ptr Args = Scons(Sstring(Path), Snil);
		racket_apply(Proc, Args);
		Sdeactivate_thread();

		Clock::time_point EndTimePoint = Clock::now();
		std::chrono::duration<double, std::milli> Delta = EndTimePoint - StartTimePoint;
		ModelProcessingStallMs = Delta.count();

		LastPath = Path;
		PendingSubtree = nullptr;
	}
}


void OpenModel()
{
	nfdchar_t* Path = nullptr;
	nfdresult_t Result = NFD_OpenDialog("rkt", "models", &Path);
	if (Result == NFD_OKAY)
	{
		LoadModel(Path);
	}
}


void UpdateElapsedTime(GLuint Query, double& TimeMs)
{
	GLint TimeNs = 0;
	glGetQueryObjectiv(Query, GL_QUERY_RESULT, &TimeNs);
	TimeMs = double(TimeNs) / 1000000.0;
}


double DepthElapsedTimeMs = 0.0;
double GridBgElapsedTimeMs = 0.0;
double OutlinerElapsedTimeMs = 0.0;
double UiElapsedTimeMs = 0.0;
void RenderUI(SDL_Window* Window, bool& Live)
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

#if 0
	static bool ShowDemoWindow = true;
	ImGui::ShowDemoWindow(&ShowDemoWindow);
#endif

	static bool ShowFocusOverlay = false;
	static bool ShowStatsOverlay = false;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open", "Ctrl+O"))
			{
				OpenModel();
			}
			if (ImGui::MenuItem("Reload", "Ctrl+R"))
			{
				LoadModel(nullptr);
			}
			if (ImGui::MenuItem("Exit"))
			{
				Live = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::BeginMenu("Background"))
			{
				if (ImGui::MenuItem("Solid Color", nullptr, ShowBackground == -1))
				{
					ShowBackground = -1;
				}
				if (ImGui::MenuItem("Test Grid", nullptr, ShowBackground == 0))
				{
					ShowBackground = 0;
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Highlight Subtrees", nullptr, &ShowSubtrees))
			{
				ShowHeatmap = false;
			}
			if (ImGui::MenuItem("Show Heatmap", nullptr, &ShowHeatmap))
			{
				ShowSubtrees = false;
			}
			if (ImGui::MenuItem("Recenter"))
			{
				ResetCamera = true;
			}
			if (ImGui::MenuItem("Full Screen", "Ctrl+F"))
			{
				ToggleFullScreen(Window);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Window"))
		{
			if (ImGui::MenuItem("Camera Parameters", nullptr, &ShowFocusOverlay))
			{
			}
			if (ImGui::MenuItem("Performance Stats", nullptr, &ShowStatsOverlay))
			{
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (ShowFocusOverlay)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin("Camera Parameters", &ShowFocusOverlay, WindowFlags))
		{
			ImGui::Text("Focal Point:\n");

			ImGui::Text("X");
			ImGui::SameLine();
			ImGui::InputFloat("##FocusX", &CameraFocus.x, 1.0f);

			ImGui::Text("Y");
			ImGui::SameLine();
			ImGui::InputFloat("##FocusY", &CameraFocus.y, 1.0f);

			ImGui::Text("Z");
			ImGui::SameLine();
			ImGui::InputFloat("##FocusZ", &CameraFocus.z, 1.0f);
		}
		ImGui::End();
	}

	if (ShowStatsOverlay)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNavInputs |
			ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoInputs;

		const ImGuiViewport* Viewport = ImGui::GetMainViewport();
		ImVec2 Position = Viewport->WorkPos;
		Position.x += 5.0;
		Position.y += 5.0;
		ImVec2 Pivot;
		Pivot.x = 0.0;
		Pivot.y = 0.0;
		ImGui::SetNextWindowPos(Position, ImGuiCond_Always, Pivot);

		if (ImGui::Begin("Performance Stats", &ShowStatsOverlay, WindowFlags))
		{
			ImGui::Text("Cadence\n");
			ImGui::Text(" %.0f hz\n", round(PresentFrequency));
			ImGui::Text(" %.1f ms\n", PresentDeltaMs);

			ImGui::Separator();
			ImGui::Text("GPU Timeline\n");
			double TotalTimeMs = \
				DepthElapsedTimeMs +
				GridBgElapsedTimeMs +
				OutlinerElapsedTimeMs +
				UiElapsedTimeMs;
			ImGui::Text("   Depth: %.2f ms\n", DepthElapsedTimeMs);
			ImGui::Text("   'Sky': %.2f ms\n", GridBgElapsedTimeMs);
			ImGui::Text(" Outline: %.2f ms\n", OutlinerElapsedTimeMs);
			ImGui::Text("      UI: %.2f ms\n", UiElapsedTimeMs);
			ImGui::Text("   Total: %.2f ms\n", TotalTimeMs);

			ImGui::Separator();
			ImGui::Text("Model Loading\n");
			ImGui::Text(" Racket: %.1f ms\n", ModelProcessingStallMs);
			ImGui::Text(" OpenGL: %.1f ms\n", ShaderCompilerStallMs);
			ImGui::Text("  Total: %.1f ms\n", ModelProcessingStallMs + ShaderCompilerStallMs);
		}
		ImGui::End();
	}

	if (RacketErrors.size() > 0)
	{
		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::OpenPopup("Error");
		if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text(RacketErrors[RacketErrors.size() - 1].c_str());
			ImGui::SetItemDefaultFocus();
			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				RacketErrors.pop_back();
			}
			ImGui::EndPopup();
		}
	}
}


int main(int argc, char* argv[])
{
	SDL_Window* Window = nullptr;
	SDL_GLContext Context = nullptr;
	{
		std::cout << "Setting up SDL2... ";
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER ) == 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, MINIMUM_VERSION_MAJOR);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, MINIMUM_VERSION_MINOR);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
			Window = SDL_CreateWindow(
				"Tangerine",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				512, 512,
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
		}
		if (Window == nullptr)
		{
			std::cout << "Failed to create SDL2 window.\n";
			return 0;
		}
		else
		{
			Context = SDL_GL_CreateContext(Window);
			if (Context == nullptr)
			{
				std::cout << "Failed to create SDL2 OpenGL Context.\n";
				return 0;
			}
			else
			{
				SDL_GL_MakeCurrent(Window, Context);
				SDL_GL_SetSwapInterval(1);
				std::cout << "Done!\n";
			}
		}
	}
	{
		std::cout << "Setting up OpenGL... ";
		if (gladLoadGL())
		{
			std::cout << "Done!\n";
		}
		else
		{
			std::cout << "Failed to setup OpenGL.\n";
		}
	}
	{
		std::cout << "Setting up Racket CS... ";
		racket_boot_arguments_t BootArgs;
		memset(&BootArgs, 0, sizeof(BootArgs));
		BootArgs.boot1_path = "./racket/petite.boot";
		BootArgs.boot2_path = "./racket/scheme.boot";
		BootArgs.boot3_path = "./racket/racket.boot";
		BootArgs.exec_file = "tangerine.exe";
		racket_boot(&BootArgs);
		racket_embedded_load_file("./racket/modules", 1);
		std::cout << "Done!\n";
	}
	{
		std::cout << "Setting up Dear ImGui... ";
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGui::StyleColorsLight();
		ImGui_ImplSDL2_InitForOpenGL(Window, Context);
		ImGui_ImplOpenGL3_Init("#version 130");
		std::cout << "Done!\n";
	}
	std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";
	if (SetupRenderer() == StatusCode::FAIL)
	{
		return 0;
	}
	bool Live = true;
	{
		ImGuiIO& io = ImGui::GetIO();
		while (Live)
		{
			SDL_Event Event;
			MouseMotionX = 0;
			MouseMotionY = 0;
			MouseMotionZ = 0;
			while (SDL_PollEvent(&Event))
			{
				ImGui_ImplSDL2_ProcessEvent(&Event);
				if (Event.type == SDL_QUIT ||
					(Event.type == SDL_WINDOWEVENT && Event.window.event == SDL_WINDOWEVENT_CLOSE && Event.window.windowID == SDL_GetWindowID(Window)))
				{
					Live = false;
					break;
				}
				if (!io.WantCaptureMouse)
				{
					static bool Dragging = false;
					switch (Event.type)
					{
					case SDL_MOUSEMOTION:
						if (Dragging)
						{
							MouseMotionX = Event.motion.xrel;
							MouseMotionY = Event.motion.yrel;
						}
						break;
					case SDL_MOUSEBUTTONDOWN:
						Dragging = true;
						SDL_SetRelativeMouseMode(SDL_TRUE);
						break;
					case SDL_MOUSEBUTTONUP:
						Dragging = false;
						SDL_SetRelativeMouseMode(SDL_FALSE);
						break;
					case SDL_MOUSEWHEEL:
						MouseMotionZ = Event.wheel.y;
						break;
					}
				}
				if (!io.WantCaptureKeyboard && Event.type == SDL_KEYDOWN)
				{
					const int SHIFT_FLAG = 1 << 9;
					const int CTRL_FLAG = 1 << 10;
					const int ALT_FLAG = 1 << 11;
					const int OPEN_MODEL = CTRL_FLAG | SDLK_o;
					const int RELOAD_MODEL = CTRL_FLAG | SDLK_r;
					const int TOGGLE_FULLSCREEN = CTRL_FLAG | SDLK_f;
					int Key = Event.key.keysym.sym;
					int Mod = Event.key.keysym.mod;
					if ((Mod & KMOD_SHIFT) != 0)
					{
						Key |= SHIFT_FLAG;
					}
					if ((Mod & KMOD_CTRL) != 0)
					{
						Key |= CTRL_FLAG;
					}
					if ((Mod & KMOD_ALT) != 0)
					{
						Key |= ALT_FLAG;
					}
					switch (Key)
					{
					case OPEN_MODEL:
						OpenModel();
						break;
					case RELOAD_MODEL:
						LoadModel(nullptr);
						break;
					case TOGGLE_FULLSCREEN:
						ToggleFullScreen(Window);
						break;
					case SDLK_KP_MULTIPLY:
						MouseMotionZ += 5;
						break;
					case SDLK_KP_DIVIDE:
						MouseMotionZ -= 5;
						break;
					case SDLK_KP_1: // ⭩
						MouseMotionX += 45;
						MouseMotionY -= 45;
						break;
					case SDLK_KP_2: // ⭣
						MouseMotionY -= 45;
						break;
					case SDLK_KP_3: // ⭨
						MouseMotionX -= 45;
						MouseMotionY -= 45;
						break;
					case SDLK_KP_4: // ⭠
						MouseMotionX += 45;
						break;
					case SDLK_KP_6: // ⭢
						MouseMotionX -= 45;
						break;
					case SDLK_KP_7: // ⭦
						MouseMotionX += 45;
						MouseMotionY += 45;
						break;
					case SDLK_KP_8: // ⭡
						MouseMotionY += 45;
						break;
					case SDLK_KP_9: // ⭧
						MouseMotionX -= 45;
						MouseMotionY += 45;
						break;
					}
				}
			}
			{
				RenderUI(Window, Live);
				ImGui::Render();
			}
			{
				int ScreenWidth;
				int ScreenHeight;
				SDL_GetWindowSize(Window, &ScreenWidth, &ScreenHeight);
				RenderFrame(ScreenWidth, ScreenHeight);
			}
			{
				{
					glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Dear ImGui");
					glBeginQuery(GL_TIME_ELAPSED, UiTimeQuery);
					ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
					glEndQuery(GL_TIME_ELAPSED);
					glPopDebugGroup();
				}
				SDL_GL_SwapWindow(Window);
			}
			UpdateElapsedTime(DepthTimeQuery, DepthElapsedTimeMs);
			UpdateElapsedTime(GridBgTimeQuery, GridBgElapsedTimeMs);
			UpdateElapsedTime(OutlinerTimeQuery, OutlinerElapsedTimeMs);
			UpdateElapsedTime(UiTimeQuery, UiElapsedTimeMs);
			if (ShowHeatmap)
			{
				const size_t QueryCount = Drawables.size();
				float Range = 0.0;
				std::vector<float> Upload(QueryCount, 0.0);
				for (int i = 0; i < QueryCount; ++i)
				{
					GLuint TimeQuery = Drawables[i]->DepthQuery;
					double ElapsedTimeMs;
					UpdateElapsedTime(TimeQuery, ElapsedTimeMs);
					Upload[i] = float(ElapsedTimeMs);
					DepthElapsedTimeMs += ElapsedTimeMs;
					Range = fmax(Range, float(ElapsedTimeMs));
				}
				for (int i = 0; i < QueryCount; ++i)
				{
					Upload[i] /= Range;
				}
				DepthTimeBuffer.Upload(Upload.data(), QueryCount * sizeof(float));
			}
		}
	}
	{
		std::cout << "Shutting down...\n";
		for (SubtreeShader& Shader : SubtreeShaders)
		{
			Shader.Release();
		}
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
	}
	return 0;
}
