
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

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <iostream>
#include <vector>
#include <chrono>

#include <chezscheme.h>
#include <racketcs.h>
#include <generated.hpp>

#include "errors.h"
#include "gl_boilerplate.h"
#include "../shaders/defines.h"


#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2


GLuint NullVAO;
std::vector<ShaderPipeline> ClusterCullingShaders;
ShaderPipeline ClusterTallyShader;
#if VISUALIZE_CLUSTER_COVERAGE
ShaderPipeline ClusterCoverageShader;
#endif
std::vector<ShaderPipeline> ClusterDepthShaders;
ShaderPipeline PaintShader;
ShaderPipeline NoiseShader;

Buffer ViewInfo("ViewInfo Buffer");

std::vector<Buffer> TileDrawArgs;
Buffer TileHeapInfo("Tile Draw Heap Info");
Buffer TileHeap("Tile Draw Heap");
GLuint DepthPass;
GLuint DepthBuffer;
GLuint PositionBuffer;
GLuint NormalBuffer;
const GLuint FinalPass = 0;


std::vector<int> ClusterCounts;


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

		glCreateFramebuffers(1, &DepthPass);
		glObjectLabel(GL_FRAMEBUFFER, DepthPass, -1, "DepthPass");
		glNamedFramebufferTexture(DepthPass, GL_DEPTH_ATTACHMENT, DepthBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT0, PositionBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT1, NormalBuffer, 0);
		GLenum ColorAttachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glNamedFramebufferDrawBuffers(DepthPass, 2, ColorAttachments);
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
	float CurrentTime;
	float Padding[3] = { 0 };
};


struct TileDrawArgsUpload
{
	GLuint PrimitiveCount;
	GLuint InstanceCount;
	GLuint First;
	GLuint BaseIntstance;
	GLuint InstanceOffset; // Not a draw param.
};


struct TileHeapInfoUpload
{
	GLuint HeapSize;
	GLuint SegmentStart;
	GLuint StackPtr;
};


struct TileHeapEntry
{
	GLuint TileID;
	GLuint ClusterID;
};


StatusCode CompileGeneratedShaders(std::string& ClusterDist, std::string& ClusterData)
{
	ShaderPipeline CullingShader;
	StatusCode Result = CullingShader.Setup(
		{ {GL_COMPUTE_SHADER, GeneratedShader("shaders/math.glsl", ClusterData + ClusterDist, "shaders/cluster_cull.cs.glsl")} },
		"Cluster Culling Shader");
	if (Result == StatusCode::FAIL)
	{
		CullingShader.Reset();
		return Result;
	}

	ShaderPipeline DepthShader;
	Result = DepthShader.Setup(
		{ {GL_VERTEX_SHADER, GeneratedShader("shaders/math.glsl", ClusterData + ClusterDist, "shaders/test.vs.glsl")},
		  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", ClusterDist, "shaders/test.fs.glsl")} },
		"Generated Shader");
	if (Result == StatusCode::FAIL)
	{
		CullingShader.Reset();
		DepthShader.Reset();
		return Result;
	}

	ClusterCullingShaders.push_back(CullingShader);
	ClusterDepthShaders.push_back(DepthShader);
	return StatusCode::PASS;
}


// Renderer setup.
StatusCode SetupRenderer()
{
	// For drawing without a VBO bound.
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

	std::string NullClusterDist = \
		"float ClusterDist(vec3 Point)\n"
		"{\n"
		"	return 0.0;\n"
		"}\n";

	std::string NullClusterData = \
		"const uint ClusterCount = 1;\n"
		"AABB ClusterData[ClusterCount] = { AABB(vec3(0.0), vec3(0.0)) };\n";

	ClusterCounts.push_back(0);

	RETURN_ON_FAIL(CompileGeneratedShaders(NullClusterDist, NullClusterData));

	RETURN_ON_FAIL(ClusterTallyShader.Setup(
		{ {GL_COMPUTE_SHADER, ShaderSource("shaders/cluster_tally.cs.glsl", true)} },
		"Cluster Tally Shader"));

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
#endif

	RETURN_ON_FAIL(NoiseShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		 {GL_FRAGMENT_SHADER, ShaderSource("shaders/noise.fs.glsl", true)} },
		"Noise Shader"));

	glDisable(GL_MULTISAMPLE);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	glDepthRange(1.0, 0.0);
	glDepthFunc(GL_GREATER);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepth(0.0);

	return StatusCode::PASS;
}


struct GeneratedSources
{
	int ClusterCount;
	std::string ClusterDist;
	std::string ClusterData;
};


#if 0
std::mutex NewShaderLock;
std::atomic_bool ModelLoaded = false;
std::atomic_bool NewShaderReady;
std::vector<GeneratedSources> NewClusters;
void SetupNewShader()
{
	NewShaderLock.lock();
	for (ShaderPipeline& CullingShader : ClusterCullingShaders)
	{
		CullingShader.Reset();
	}
	for (ShaderPipeline& DepthShader : ClusterDepthShaders)
	{
		DepthShader.Reset();
	}
	ClusterCullingShaders.clear();
	ClusterDepthShaders.clear();
	ClusterCounts.clear();

	for (GeneratedSources& Generated : NewClusters)
	{
		StatusCode Result = CompileGeneratedShaders(Generated.ClusterDist, Generated.ClusterData);
		if (Result == StatusCode::FAIL)
		{
			break;
		}
		else
		{
			ClusterCounts.push_back(Generated.ClusterCount);
		}
	}

	NewShaderReady.store(false);
	ModelLoaded.store(true);
	NewShaderLock.unlock();
}
#endif


void RenderFrame(int ScreenWidth, int ScreenHeight)
{
#if 0
	if (NewShaderReady.load())
	{
		SetupNewShader();
	}
#endif

	{
		static size_t LastDrawCount = 0;
		size_t DrawCount =  ClusterCullingShaders.size();
		if (LastDrawCount != DrawCount)
		{
			LastDrawCount = DrawCount;

			for (Buffer& OldBuffer : TileDrawArgs)
			{
				OldBuffer.Release();
			}

			TileDrawArgs.resize(DrawCount);

			for (int i = 0; i < DrawCount; ++i)
			{
				TileDrawArgs[i] = Buffer("Indirect Tile Drawing Arguments");
				{
					TileDrawArgsUpload BufferData = { 0, 0, 0, 0 };
					TileDrawArgs[i].Upload((void*)&BufferData, sizeof(BufferData));
				}
			}
		}
	}

	double DeltaTime;
	double CurrentTime;
	{
		using Clock = std::chrono::high_resolution_clock;
		static Clock::time_point StartTimePoint = Clock::now();
		static Clock::time_point LastTimePoint = StartTimePoint;
		Clock::time_point CurrentTimePoint = Clock::now();
		{
			std::chrono::duration<double, std::milli> FrameDelta = CurrentTimePoint - LastTimePoint;
			DeltaTime = FrameDelta.count();
		}
		{
			std::chrono::duration<double, std::milli> EpochDelta = CurrentTimePoint - StartTimePoint;
			CurrentTime = EpochDelta.count();
		}
		LastTimePoint = CurrentTimePoint;
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
		//const glm::vec3 CameraOrigin = glm::vec3(-4.0, -14.0, 4.0);
		const glm::vec3 CameraOrigin = glm::vec3(0.0, -14.0, 0.0);
		const glm::vec3 CameraFocus = glm::vec3(0.0, 0.0, 0.0);
		const glm::vec3 UpVector = glm::vec3(0.0, 0.0, 1.0);
		const glm::mat4 WorldToView = glm::lookAt(CameraOrigin, CameraFocus, UpVector);
		const glm::mat4 ViewToWorld = glm::inverse(WorldToView);

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
			float(CurrentTime),
		};
		ViewInfo.Upload((void*)&BufferData, sizeof(BufferData));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);
	}

	const unsigned int TilesX = DIV_UP(Width, TILE_SIZE_X);
	const unsigned int TilesY = DIV_UP(Height, TILE_SIZE_Y);
	{
		static unsigned int HeapSize = 0;
		const unsigned int TileCount = TilesX * TilesY * 20;

		TileHeapInfoUpload BufferData = {
			TileCount,
			0,
			0
		};
		TileHeapInfo.Upload((void*)&BufferData, sizeof(BufferData));
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		if (TileCount != HeapSize)
		{
			std::cout << "Tile heap size: " << TileCount << "\n";
			HeapSize = TileCount;
			TileHeap.Reserve(sizeof(TileHeapEntry) * HeapSize);
		}
	}

#if 0
	if (ModelLoaded.load())
	{
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Cluster Culling Pass");
			// Each lane is a tile, so we have to tile the tiles...
			const unsigned int GroupX = DIV_UP(TilesX, TILE_SIZE_X);
			const unsigned int GroupY = DIV_UP(TilesY, TILE_SIZE_Y);
			int i = 0;
			for (ShaderPipeline& CullingShader : ClusterCullingShaders)
			{
				glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Subtree");
				{
					CullingShader.Activate();
					TileHeap.Bind(GL_SHADER_STORAGE_BUFFER, 0);
					TileHeapInfo.Bind(GL_SHADER_STORAGE_BUFFER, 1);
					glDispatchCompute(GroupX, GroupY, ClusterCounts[i]);
					glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
				}
				{
					ClusterTallyShader.Activate();
					TileDrawArgs[i].Bind(GL_SHADER_STORAGE_BUFFER, 0);
					TileHeapInfo.Bind(GL_SHADER_STORAGE_BUFFER, 1);
					glDispatchCompute(1, 1, 1);
					glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
				}
				++i;
				glPopDebugGroup();
			}
			glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
			glPopDebugGroup();
		}

#if VISUALIZE_CLUSTER_COVERAGE
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Visualize Cluster Coverage");
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
			ClusterCoverageShader.Activate();
			glClear(GL_COLOR_BUFFER_BIT);
			TileDrawArgs.Bind(GL_DRAW_INDIRECT_BUFFER);
			TileHeap.Bind(GL_SHADER_STORAGE_BUFFER, 0);
			TileHeapInfo.Bind(GL_SHADER_STORAGE_BUFFER, 1);
			glDrawArraysIndirect(GL_TRIANGLES, 0);
			glPopDebugGroup();
		}
#else
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth");
			glBindFramebuffer(GL_FRAMEBUFFER, DepthPass);
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
			glClear(GL_DEPTH_BUFFER_BIT);
			TileHeap.Bind(GL_SHADER_STORAGE_BUFFER, 0);
			TileHeapInfo.Bind(GL_SHADER_STORAGE_BUFFER, 1);
			int i = 0;
			for (ShaderPipeline& DepthShader : ClusterDepthShaders)
			{
				DepthShader.Activate();
				TileDrawArgs[i].Bind(GL_DRAW_INDIRECT_BUFFER);
				TileDrawArgs[i].Bind(GL_SHADER_STORAGE_BUFFER, 3);
				glDrawArraysIndirect(GL_TRIANGLES, 0);
				++i;
			}
			glPopDebugGroup();
		}

		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Paint");
			glDepthMask(GL_FALSE);
			glDisable(GL_DEPTH_TEST);
			glBindFramebuffer(GL_FRAMEBUFFER, FinalPass);
			glBindTextureUnit(1, DepthBuffer);
			glBindTextureUnit(2, PositionBuffer);
			glBindTextureUnit(3, NormalBuffer);
			PaintShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glPopDebugGroup();
		}
#endif
	}
	else
#endif
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


#if 0
void LockShaders()
{
	NewShaderLock.lock();
	NewClusters.clear();
	ModelLoaded.store(false);
}


void PostShader(int ClusterCount, const char* ClusterDist, const char* ClusterData)
{
	NewClusters.push_back({ ClusterCount, std::string(ClusterDist), std::string(ClusterData) });
}


void UnlockShaders()
{
	NewShaderReady.store(true);
	NewShaderLock.unlock();
}
#endif


int main(int argc, char* argv[])
{
	SDL_Window* Window = nullptr;
	SDL_GLContext Context = nullptr;
	{
		std::cout << "Setting up SDL2... ";
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO) == 0)
		{
			Window = SDL_CreateWindow(
				"Tangerine",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				800, 600,
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
		declare_modules();
		std::cout << "Done!\n";
	}
	std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";
	if (SetupRenderer() == StatusCode::FAIL)
	{
		return 0;
	}
	{
		while (!SDL_QuitRequested())
		{
			int ScreenWidth;
			int ScreenHeight;
			SDL_GetWindowSize(Window, &ScreenWidth, &ScreenHeight);
			RenderFrame(ScreenWidth, ScreenHeight);
			SDL_GL_SwapWindow(Window);
		}
	}
	{
		std::cout << "Shutting down...\n";
	}
	return 0;
}
