
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
#include <SDL_clipboard.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>

#include "sdfs.h"
#include "shape_compiler.h"
#include "export.h"

#include <iostream>
#include <cstring>
#include <map>
#include <vector>
#include <chrono>
#include <thread>

#include <chezscheme.h>
#include <racketcs.h>

#include <nfd.h>

#include "profiling.h"

#include "errors.h"
#include "gl_boilerplate.h"
#include "../shaders/defines.h"

#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2

#define ASYNC_SHADER_COMPILE 0

using Clock = std::chrono::high_resolution_clock;


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
		, PrettyTree(Old.PrettyTree)
		, DistSource(Old.DistSource)
		, IsValid(Old.IsValid)
		, Incomplete(Old.Incomplete)
	{
		Old.IsValid = false;
		std::swap(DepthShader, Old.DepthShader);
		std::swap(DepthQuery, Old.DepthQuery);
		std::swap(Instances, Old.Instances);
	}
	SubtreeShader(std::string InDebugName, std::string InPrettyTree, std::string InDistSource)
	{
		DebugName = InDebugName;
		PrettyTree = InPrettyTree;
		DistSource = InDistSource;
		IsValid = false;
		Incomplete = false;
	}

	void StartAsyncSetup()
	{
		Incomplete = true;
		DepthShader.AsyncSetup(
			{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_draw.vs.glsl", true)},
			  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", DistSource, "shaders/cluster_draw.fs.glsl")} },
			DebugName.c_str());
	}

	bool WaitingForCompiler()
	{
		return DepthShader.WaitingForCompiler();
	}

	StatusCode FinishAsyncSetup()
	{
		StatusCode Result = DepthShader.FinishSetup();

		if (Result == StatusCode::FAIL)
		{
			DepthShader.Reset();
			return Result;
		}

		glGenQueries(1, &DepthQuery);
		IsValid = true;
		Incomplete = false;
		return StatusCode::PASS;
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
	bool Incomplete;
	std::string DebugName;
	std::string PrettyTree;
	std::string DistSource;
	ShaderPipeline DepthShader;
	GLuint DepthQuery;

	std::vector<ModelSubtree> Instances;
};


std::map<std::string, size_t> SubtreeMap;
std::vector<SubtreeShader> SubtreeShaders;
std::vector<size_t> PendingShaders;


ShaderPipeline TestMaterials[6];


ModelSubtree* PendingSubtree = nullptr;
size_t EmitShader(std::string Source)
{
	// TODO: debug versions
	std::string& Tree = Source;
	std::string& Pretty = Source;

	auto Found = SubtreeMap.find(Tree);

	size_t ShaderIndex;
	if (Found == SubtreeMap.end())
	{
		size_t Index = SubtreeShaders.size();
		SubtreeShaders.emplace_back(Tree, Pretty, Source);
		SubtreeMap[Tree] = Index;
		PendingShaders.push_back(Index);
		ShaderIndex = Index;
	}
	else
	{
		ShaderIndex = Found->second;
	}

	return ShaderIndex;
}

void EmitParameters(size_t ShaderIndex, std::vector<float> Params)
{
	// TODO: Instances is currently a vector, but should it be a map...?
	SubtreeShaders[ShaderIndex].Instances.emplace_back(Params.size(), Params.data());
	PendingSubtree = &(SubtreeShaders[ShaderIndex].Instances.back());
}

void EmitVoxel(AABB Bounds)
{
	glm::mat4 LocalToWorld = glm::identity<glm::mat4>();
	glm::vec3 Extent = (Bounds.Max - Bounds.Min) * glm::vec3(0.5);
	glm::vec3 Center = Extent + Bounds.Min;
	PendingSubtree->Sections.emplace_back(LocalToWorld, glm::vec4(Center, 0.0), glm::vec4(Extent, 0.0));
}


SDFNode* TreeEvaluator = nullptr;


void ClearTreeEvaluator()
{
	if (TreeEvaluator != nullptr)
	{
		delete TreeEvaluator;
		TreeEvaluator = nullptr;
	}
}


AABB ModelBounds = { glm::vec3(0.0), glm::vec3(0.0) };
//extern "C" TANGERINE_API void SetTreeEvaluator(void* InTreeEvaluator)
void SetTreeEvaluator(SDFNode* InTreeEvaluator, AABB Limits)
{
	ClearTreeEvaluator();
	TreeEvaluator = InTreeEvaluator;
	ModelBounds = Limits;// TreeEvaluator->Bounds();
}


GLuint NullVAO;
ShaderPipeline PaintShader;
ShaderPipeline MaterialResolveShader;
ShaderPipeline NoiseShader;
ShaderPipeline BgShader;
ShaderPipeline ResolveOutputShader;

Buffer ViewInfo("ViewInfo Buffer");
Buffer MaterialInfo("MaterialInfo Buffer");
Buffer OutlinerOptions("Outliner Options Buffer");

Buffer DepthTimeBuffer("Subtree Heatmap Buffer");
GLuint DepthPass;
GLuint MaterialResolvePass;
GLuint ColorPass;
const GLuint FinalPass = 0;

GLuint DepthBuffer;
GLuint PositionBuffer;
GLuint NormalBuffer;
GLuint SubtreeBuffer;
GLuint MaterialBuffer;
GLuint MaterialStencilBuffer;
GLuint ColorBuffer;

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
		glDeleteFramebuffers(1, &MaterialResolvePass);
		glDeleteFramebuffers(1, &ColorPass);
		glDeleteTextures(1, &DepthBuffer);
		glDeleteTextures(1, &PositionBuffer);
		glDeleteTextures(1, &NormalBuffer);
		glDeleteTextures(1, &SubtreeBuffer);
		glDeleteTextures(1, &MaterialBuffer);
		glDeleteTextures(1, &MaterialStencilBuffer);
		glDeleteTextures(1, &ColorBuffer);
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

		glCreateTextures(GL_TEXTURE_2D, 1, &SubtreeBuffer);
		glTextureStorage2D(SubtreeBuffer, 1, GL_R32UI, ScreenWidth, ScreenHeight);
		glTextureParameteri(SubtreeBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(SubtreeBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(SubtreeBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(SubtreeBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, SubtreeBuffer, -1, "Subtree ID");

		glCreateTextures(GL_TEXTURE_2D, 1, &MaterialBuffer);
		glTextureStorage2D(MaterialBuffer, 1, GL_R32UI, ScreenWidth, ScreenHeight);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, MaterialBuffer, -1, "Material ID");

		glCreateFramebuffers(1, &DepthPass);
		glObjectLabel(GL_FRAMEBUFFER, DepthPass, -1, "Depth Pass");
		glNamedFramebufferTexture(DepthPass, GL_DEPTH_ATTACHMENT, DepthBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT0, PositionBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT1, NormalBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT2, SubtreeBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT3, MaterialBuffer, 0);
		GLenum ColorAttachments[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
		glNamedFramebufferDrawBuffers(DepthPass, 4, ColorAttachments);
	}

	// Resolve material to depth pass.
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &MaterialStencilBuffer);
		glTextureStorage2D(MaterialStencilBuffer, 1, GL_DEPTH_COMPONENT32F, ScreenWidth, ScreenHeight);
		glTextureParameteri(MaterialStencilBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialStencilBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialStencilBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(MaterialStencilBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, MaterialStencilBuffer, -1, "Material Stencil");

		glCreateFramebuffers(1, &MaterialResolvePass);
		glObjectLabel(GL_FRAMEBUFFER, MaterialResolvePass, -1, "Material Resolve Pass");
		glNamedFramebufferTexture(MaterialResolvePass, GL_DEPTH_ATTACHMENT, MaterialStencilBuffer, 0);
		glNamedFramebufferDrawBuffers(MaterialResolvePass, 0, nullptr);
	}

	// Color passes.
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &ColorBuffer);
		glTextureStorage2D(ColorBuffer, 1, GL_RGB8, ScreenWidth, ScreenHeight);
		glTextureParameteri(ColorBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(ColorBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(ColorBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(ColorBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, ColorBuffer, -1, "Color Buffer");

		glCreateFramebuffers(1, &ColorPass);
		glObjectLabel(GL_FRAMEBUFFER, ColorPass, -1, "Material Painting Pass");
		glNamedFramebufferTexture(ColorPass, GL_DEPTH_ATTACHMENT, MaterialStencilBuffer, 0);
		glNamedFramebufferTexture(ColorPass, GL_COLOR_ATTACHMENT0, ColorBuffer, 0);
		GLenum ColorAttachments[1] = { GL_COLOR_ATTACHMENT0 };
		glNamedFramebufferDrawBuffers(ColorPass, 1, ColorAttachments);
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


void UploadMaterialInfo(unsigned int MaterialMask)
{
	GLfloat MaskFloat = 0.0;
	if (MaterialMask < UINT_MAX)
	{
		MaskFloat = 1.0 / (GLfloat(MaterialMask) + 1.0);
	}
	GLfloat BufferData[4] = {
		MaskFloat,
		0,
		0,
		0
	};
	MaterialInfo.Upload((void*)&BufferData, sizeof(GLuint) * 4);
}


struct OutlinerOptionsUpload
{
	GLuint OutlinerFlags;
	GLuint Unused1;
	GLuint Unused2;
	GLuint Unused3;
};


// Renderer setup.
StatusCode SetupRenderer()
{
	// For drawing without a VBO bound.
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

	{
		const int MaxThreads = std::thread::hardware_concurrency();
		glMaxShaderCompilerThreadsARB(MaxThreads > 2 ? MaxThreads : 2);
	}

#if VISUALIZE_CLUSTER_COVERAGE
	RETURN_ON_FAIL(ClusterCoverageShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_coverage.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/cluster_coverage.fs.glsl", true)} },
		"Cluster Coverage Shader"));
#else

	RETURN_ON_FAIL(MaterialResolveShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/resolve_stencil.fs.glsl", true)} },
		"Material Resolve Shader"));

	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/outliner.fs.glsl", true)} },
		"Outliner Shader"));

	RETURN_ON_FAIL(BgShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/bg.fs.glsl", true)} },
		"Background Shader"));
#endif

	RETURN_ON_FAIL(TestMaterials[0].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/black.glsl", true)} },
		"Black Test Material Shader"));

	RETURN_ON_FAIL(TestMaterials[1].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/gray.glsl", true)} },
		"Gray Test Material Shader"));

	RETURN_ON_FAIL(TestMaterials[2].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/white.glsl", true)} },
		"White Test Material Shader"));

	RETURN_ON_FAIL(TestMaterials[3].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/red.glsl", true)} },
		"Red Test Material Shader"));

	RETURN_ON_FAIL(TestMaterials[4].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/green.glsl", true)} },
		"Green Test Material Shader"));

	RETURN_ON_FAIL(TestMaterials[5].Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/masked.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("materials/blue.glsl", true)} },
		"Blue Test Material Shader"));

	RETURN_ON_FAIL(ResolveOutputShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/resolve.fs.glsl", true)} },
		"Resolve BackBuffer Shader"));

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
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepth(0.0);

	return StatusCode::PASS;
}


double ShaderCompilerConvergenceMs = 0.0;
Clock::time_point ShaderCompilerStart;
std::vector<SubtreeShader*> Drawables;
void CompileNewShaders(const double LastInnerFrameDeltaMs)
{
	BeginEvent("Compile New Shaders");
	Clock::time_point ProcessingStart = Clock::now();

	double Budget = 16.6 - LastInnerFrameDeltaMs;
	Budget = fmaxf(Budget, 1.0);
	Budget = fminf(Budget, 14.0);

	while (PendingShaders.size() > 0)
	{
		{
			BeginEvent("Start Async Compile");
			size_t SubtreeIndex = PendingShaders.back();
			PendingShaders.pop_back();

			SubtreeShader& Shader = SubtreeShaders[SubtreeIndex];
			Shader.StartAsyncSetup();
			if (Shader.Instances.size() > 0)
			{
				Drawables.push_back(&Shader);
			}
			EndEvent();
		}

		std::chrono::duration<double, std::milli> Delta = Clock::now() - ProcessingStart;
		if (Delta.count() > Budget)
		{
			break;
		}
	}
	EndEvent();
}


int MouseMotionX = 0;
int MouseMotionY = 0;
int MouseMotionZ = 0;
int ShowBackground = 0;
bool ShowSubtrees = false;
bool ShowHeatmap = false;
bool ResetCamera = true;
float PresentFrequency = 0.0;
float PresentDeltaMs = 0.0;
glm::vec3 CameraFocus = glm::vec3(0.0, 0.0, 0.0);
void RenderFrame(int ScreenWidth, int ScreenHeight)
{
	BeginEvent("RenderFrame");
	static double LastInnerFrameDeltaMs = 0.0;

	if (PendingShaders.size() > 0)
	{
		CompileNewShaders(LastInnerFrameDeltaMs);
	}

	Clock::time_point FrameStartTimePoint = Clock::now();

	double CurrentTime;
	{
		static Clock::time_point StartTimePoint = FrameStartTimePoint;
		static Clock::time_point LastTimePoint = StartTimePoint;
		{
			std::chrono::duration<double, std::milli> FrameDelta = FrameStartTimePoint - LastTimePoint;
			PresentDeltaMs = float(FrameDelta.count());
		}
		{
			std::chrono::duration<double, std::milli> EpochDelta = FrameStartTimePoint - StartTimePoint;
			CurrentTime = float(EpochDelta.count());
		}
		LastTimePoint = FrameStartTimePoint;
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
			CameraFocus = (ModelBounds.Max - ModelBounds.Min) * glm::vec3(0.5) + ModelBounds.Min;
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
			glm::vec4(ModelBounds.Min, 1.0),
			glm::vec4(ModelBounds.Max, 1.0),
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
			OutlinerFlags,
			0,
			0,
			0
		};
		OutlinerOptions.Upload((void*)&BufferData, sizeof(BufferData));
	}

	if (Drawables.size() > 0)
	{
		{
			BeginEvent("Depth");
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth");
			glBeginQuery(GL_TIME_ELAPSED, DepthTimeQuery);
			glBindFramebuffer(GL_FRAMEBUFFER, DepthPass);
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_GREATER);
			glClear(GL_DEPTH_BUFFER_BIT);
			if (ShowHeatmap)
			{
				glEndQuery(GL_TIME_ELAPSED);
			}
			for (SubtreeShader* Shader : Drawables)
			{
				if (Shader->Incomplete)
				{
					if (Shader->WaitingForCompiler())
					{
						continue;
					}
					else
					{
						BeginEvent("FinishAsyncSetup");
						StatusCode Result = Shader->FinishAsyncSetup();
						glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
						std::chrono::duration<double, std::milli> Delta = Clock::now() - ShaderCompilerStart;
						ShaderCompilerConvergenceMs = Delta.count();
						EndEvent();
					}
				}
				else if (!Shader->IsValid)
				{
					continue;
				}

				BeginEvent("Draw Drawable");
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
				EndEvent();
			}
			if (!ShowHeatmap)
			{
				glEndQuery(GL_TIME_ELAPSED);
			}
			glPopDebugGroup();
			EndEvent();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Resolve Material Stencil");
			glBindFramebuffer(GL_FRAMEBUFFER, MaterialResolvePass);
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_ALWAYS);
			glBindTextureUnit(1, DepthBuffer);
			glBindTextureUnit(2, MaterialBuffer);
			MaterialResolveShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glPopDebugGroup();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Background");
			UploadMaterialInfo(-1);
			MaterialInfo.Bind(GL_UNIFORM_BUFFER, 1);
			glBindFramebuffer(GL_FRAMEBUFFER, ColorPass);
			glBeginQuery(GL_TIME_ELAPSED, GridBgTimeQuery);
			glDepthMask(GL_FALSE);
			glDepthFunc(GL_EQUAL);
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
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Default Material");
			UploadMaterialInfo(0);
			glBeginQuery(GL_TIME_ELAPSED, OutlinerTimeQuery);
			glBindTextureUnit(1, DepthBuffer);
			glBindTextureUnit(2, PositionBuffer);
			glBindTextureUnit(3, NormalBuffer);
			glBindTextureUnit(4, SubtreeBuffer);
			MaterialInfo.Bind(GL_UNIFORM_BUFFER, 1);
			OutlinerOptions.Bind(GL_UNIFORM_BUFFER, 2);
			DepthTimeBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			PaintShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glEndQuery(GL_TIME_ELAPSED);
			glPopDebugGroup();
		}
		for (int TestMaterial = 0; TestMaterial < 6; ++TestMaterial)
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Test Material");
			UploadMaterialInfo(TestMaterial + 1);
			MaterialInfo.Bind(GL_UNIFORM_BUFFER, 1);
			TestMaterials[TestMaterial].Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glPopDebugGroup();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Resolve Output");
			glDisable(GL_DEPTH_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
			glBindTextureUnit(1, ColorBuffer);
			ResolveOutputShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
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

	{
		std::chrono::duration<double, std::milli> InnerFrameDelta = Clock::now() - FrameStartTimePoint;
		LastInnerFrameDeltaMs = float(InnerFrameDelta.count());
	}

	EndEvent();
}


void ToggleFullScreen(SDL_Window* Window)
{
	static bool FullScreen = false;
	FullScreen = !FullScreen;
	SDL_SetWindowFullscreen(Window, FullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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
	BeginEvent("Load Model");
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
		ClearTreeEvaluator();

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

		Drawables.reserve(PendingShaders.size());
		ShaderCompilerConvergenceMs = 0.0;
		ShaderCompilerStart = Clock::now();
	}
	EndEvent();
}


void OpenModel()
{
	nfdchar_t* Path = nullptr;
	BeginEvent("NFD_OpenDialog");
	nfdresult_t Result = NFD_OpenDialog("rkt", "models", &Path);
	EndEvent();
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
	static bool ShowPrettyTrees = false;

	static bool ShowExportOptions = false;
	static float ExportStepSize;
	static float ExportSplitStep[3];
	static bool ExportSkipRefine;
	static int ExportRefinementSteps;

	const bool DefaultExportSkipRefine = false;
	const float DefaultExportStepSize = 0.01;
	const int DefaultExportRefinementSteps = 5;

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
			if (ImGui::MenuItem("Export", nullptr, false, TreeEvaluator != nullptr))
			{
				ShowExportOptions = true;

				ExportStepSize = DefaultExportStepSize;
				for (int i = 0; i < 3; ++i)
				{
					ExportSplitStep[i] = ExportStepSize;
				}
				ExportSkipRefine = DefaultExportSkipRefine;
				ExportRefinementSteps = DefaultExportRefinementSteps;
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
			if (ImGui::MenuItem("CSG Subtrees", nullptr, &ShowPrettyTrees))
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
			ImGui::Text(" Racket: %.3f s\n", ModelProcessingStallMs / 1000.0);
			ImGui::Text(" OpenGL: %.3f s\n", ShaderCompilerConvergenceMs / 1000.0);
		}
		ImGui::End();
	}

	if (ShowPrettyTrees && SubtreeShaders.size() > 0)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin("CSG Subtrees", &ShowPrettyTrees, WindowFlags))
		{
			bool First = true;
			for (SubtreeShader& Subtree : SubtreeShaders)
			{
				if (First)
				{
					First = false;
				}
				else
				{
					ImGui::Separator();
				}
				ImGui::TextUnformatted(Subtree.PrettyTree.c_str(), nullptr);
			}
		}
		ImGui::End();
	}

	{
		ExportProgress Progress = GetExportProgress();
		if (Progress.Stage != 0)
		{
			ImVec2 MaxSize = ImGui::GetMainViewport()->WorkSize;
			ImGui::SetNextWindowSizeConstraints(ImVec2(200, 150), MaxSize);
			ImGui::OpenPopup("Export Progress");
			if (ImGui::BeginPopupModal("Export Progress", nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				ImGui::ProgressBar(Progress.Generation, ImVec2(-FLT_MIN, 0), "Mesh Generation");
				ImGui::ProgressBar(Progress.Refinement, ImVec2(-FLT_MIN, 0), "Mesh Refinement");
				ImGui::ProgressBar(Progress.Write, ImVec2(-FLT_MIN, 0), "Saving");
				if (ImGui::Button("Good Enough"))
				{
					CancelExport(false);
				}
				ImGui::SameLine();
				if (ImGui::Button("Halt"))
				{
					CancelExport(true);
				}
				ImGui::EndPopup();
			}
		}
		else if (ShowExportOptions)
		{
			static bool AdvancedOptions = false;
			ImVec2 MaxSize = ImGui::GetMainViewport()->WorkSize;
			ImGui::SetNextWindowSizeConstraints(ImVec2(250, 150), MaxSize);
			ImGui::OpenPopup("Export Options");
			if (ImGui::BeginPopupModal("Export Options", nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				if (AdvancedOptions)
				{
					ImGui::InputFloat3("Voxel Size", ExportSplitStep);
					ImGui::Checkbox("Skip Refinement", &ExportSkipRefine);
					if (!ExportSkipRefine)
					{
						ImGui::InputInt("Refinement Steps", &ExportRefinementSteps);
					}
				}
				else
				{
					ImGui::InputFloat("Voxel Size", &ExportStepSize);
				}
				if (ImGui::Button("Start"))
				{
					if (AdvancedOptions)
					{
						glm::vec3 VoxelSize = glm::vec3(
							ExportSplitStep[0],
							ExportSplitStep[1],
							ExportSplitStep[2]);
						int RefinementSteps = ExportSkipRefine ? 0 : ExportRefinementSteps;
						MeshExport(TreeEvaluator, ModelBounds.Min, ModelBounds.Max, VoxelSize, RefinementSteps);
					}
					else
					{
						glm::vec3 VoxelSize = glm::vec3(ExportStepSize);
						MeshExport(TreeEvaluator, ModelBounds.Min, ModelBounds.Max, VoxelSize, DefaultExportRefinementSteps);
					}
					ShowExportOptions = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
				{
					ShowExportOptions = false;
				}
				ImGui::SameLine();
				if (ImGui::Checkbox("Advanced Options", &AdvancedOptions) && AdvancedOptions)
				{
					for (int i = 0; i < 3; ++i)
					{
						ExportSplitStep[i] = ExportStepSize;
					}
				}
				ImGui::EndPopup();
			}
		}
	}

	if (RacketErrors.size() > 0)
	{
		std::string RacketError = RacketErrors[RacketErrors.size() - 1];
		{
			ImVec2 TextSize = ImGui::CalcTextSize(RacketError.c_str(), nullptr);
			TextSize.x += 40.0;
			TextSize.y += 100.0;
			ImVec2 MaxSize = ImGui::GetMainViewport()->WorkSize;
			ImVec2 MinSize(
				TextSize.x < MaxSize.x ? TextSize.x : MaxSize.x,
				TextSize.y < MaxSize.y ? TextSize.y : MaxSize.y);
			ImGui::SetNextWindowSizeConstraints(MinSize, MaxSize);
		}
		{
			ImVec2 Center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(Center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		}
		ImGui::OpenPopup("Error");
		if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_NoSavedSettings))
		{
			{
				ImVec2 Size = ImGui::GetContentRegionAvail();
				Size.y -= 24.0f;
				if (ImGui::BeginChild("ErrorText", Size, false, ImGuiWindowFlags_HorizontalScrollbar))
				{
					ImGui::TextUnformatted(RacketError.c_str(), nullptr);
				}
				ImGui::EndChild();
			}

			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				RacketErrors.pop_back();
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy Error", ImVec2(120, 0)))
			{
				SDL_SetClipboardText(RacketError.c_str());
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
				900, 900,
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
		{
			io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0);
			static ImWchar Ranges[] = { 0x1, 0x1FFFF, 0 };
			static ImFontConfig Config;
			Config.OversampleH = 1;
			Config.OversampleV = 1;
			Config.MergeMode = true;
			Config.FontBuilderFlags = 0;
			io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 16.0, &Config, Ranges);
		}
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
			BeginEvent("Frame");
			SDL_Event Event;
			MouseMotionX = 0;
			MouseMotionY = 0;
			MouseMotionZ = 0;
			BeginEvent("Process Input");
			while (SDL_PollEvent(&Event))
			{
				ImGui_ImplSDL2_ProcessEvent(&Event);
				if (Event.type == SDL_QUIT ||
					(Event.type == SDL_WINDOWEVENT && Event.window.event == SDL_WINDOWEVENT_CLOSE && Event.window.windowID == SDL_GetWindowID(Window)))
				{
					Live = false;
					break;
				}
				static bool Dragging = false;
				if (!io.WantCaptureMouse)
				{
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
				else if (Dragging && RacketErrors.size() > 0)
				{
					Dragging = false;
					SDL_SetRelativeMouseMode(SDL_FALSE);
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
			EndEvent();
			{
				BeginEvent("Update UI");
				RenderUI(Window, Live);
				ImGui::Render();
				EndEvent();
			}
			{
				int ScreenWidth;
				int ScreenHeight;
				SDL_GetWindowSize(Window, &ScreenWidth, &ScreenHeight);
				RenderFrame(ScreenWidth, ScreenHeight);
			}
			{
				BeginEvent("Dear ImGui Draw");
				glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Dear ImGui");
				glBeginQuery(GL_TIME_ELAPSED, UiTimeQuery);
				ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
				glEndQuery(GL_TIME_ELAPSED);
				glPopDebugGroup();
				EndEvent();
			}
			{
				BeginEvent("Present");
				SDL_GL_SwapWindow(Window);
				EndEvent();
			}
			{
				BeginEvent("Query Results");
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
				EndEvent();
			}
			EndEvent();
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
