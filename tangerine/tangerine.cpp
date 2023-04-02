
// Copyright 2022 Aeva Palecek
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

#ifdef MINIMAL_DLL
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_clipboard.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>
#include <ImFileDialog.h>

#include "events.h"
#include "sdf_evaluator.h"
#include "sdf_rendering.h"
#include "sdf_model.h"
#include "shape_compiler.h"
#include "export.h"
#include "magica.h"
#include "extern.h"

#include "lua_env.h"
#include "racket_env.h"
#include "tangerine.h"

#include <iostream>
#include <iterator>
#include <cstring>
#include <map>
#include <vector>
#include <chrono>
#include <regex>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>

#include "profiling.h"

#include "errors.h"
#include "gl_boilerplate.h"
#include "gl_async.h"
#include "gl_debug.h"
#include "../shaders/defines.h"
#include "installation.h"

// TODO: These were originally defined as a cross-platform compatibility hack for code
// in this file that was using min/max macros from a Windows header.  The header is no
// longer in use, but the code remains.
#define min(a, b) (a < b ? a : b)
#define max(a, b) (a > b ? a : b)


#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2

#define ASYNC_SHADER_COMPILE 0

using Clock = std::chrono::high_resolution_clock;


bool HeadlessMode;


ScriptEnvironment* MainEnvironment = nullptr;


SDFNode* TreeEvaluator = nullptr;


TangerinePaths Installed;
std::filesystem::path LastOpenDir;


void ClearTreeEvaluator()
{
	if (TreeEvaluator != nullptr)
	{
		TreeEvaluator->Release();
		TreeEvaluator = nullptr;
	}
}


AABB ModelBounds = { glm::vec3(0.0), glm::vec3(0.0) };
void SetTreeEvaluator(SDFNode* InTreeEvaluator)
{
	ClearTreeEvaluator();
	TreeEvaluator = InTreeEvaluator;
	TreeEvaluator->Hold();
	ModelBounds = TreeEvaluator->Bounds();
}


ShaderProgram PaintShader;
ShaderProgram NoiseShader;
ShaderProgram BgShader;
ShaderProgram GatherDepthShader;
ShaderProgram ResolveOutputShader;
ShaderProgram OctreeDebugShader;

Buffer ViewInfo("ViewInfo Buffer");
Buffer OutlinerOptions("Outliner Options Buffer");

Buffer DepthTimeBuffer("Subtree Heatmap Buffer");
GLuint DepthPass;
GLuint ColorPass;
GLuint FinalPass = 0;

GLuint DepthBuffer;
GLuint PositionBuffer;
GLuint NormalBuffer;
GLuint SubtreeBuffer;
GLuint MaterialBuffer;
GLuint ColorBuffer;
GLuint FinalBuffer;

TimingQuery DepthTimeQuery;
TimingQuery GridBgTimeQuery;
TimingQuery OutlinerTimeQuery;
TimingQuery UiTimeQuery;

const glm::vec3 DefaultBackgroundColor = glm::vec3(.6);
glm::vec3 BackgroundColor = DefaultBackgroundColor;


#if ENABLE_OCCLUSION_CULLING
struct DepthPyramidSliceUpload
{
	int Width;
	int Height;
	int Level;
	int Unused;
};

GLuint DepthPyramidBuffer;
std::vector<Buffer> DepthPyramidSlices;

#if DEBUG_OCCLUSION_CULLING
GLuint OcclusionDebugBuffer;
#endif

void UpdateDepthPyramid(int ScreenWidth, int ScreenHeight)
{
	glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth Pyramid");
	glDisable(GL_DEPTH_TEST);
	GatherDepthShader.Activate();
	glBindTextureUnit(3, DepthBuffer);

	int Level = 0;
	int LevelWidth = ScreenWidth;
	int LevelHeight = ScreenHeight;
	for (Buffer& DepthPyramidSlice : DepthPyramidSlices)
	{
		DepthPyramidSlice.Bind(GL_UNIFORM_BUFFER, 2);
		if (Level > 0)
		{
			glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			glBindImageTexture(4, DepthPyramidBuffer, Level - 1, false, 0, GL_READ_ONLY, GL_R32F);
		}
		glBindImageTexture(5, DepthPyramidBuffer, Level, false, 0, GL_WRITE_ONLY, GL_R32F);
		glDispatchCompute(DIV_UP(LevelWidth, TILE_SIZE_X), DIV_UP(LevelHeight, TILE_SIZE_Y), 1);

		LevelWidth = max(LevelWidth / 2, 1);
		LevelHeight = max(LevelHeight / 2, 1);
		++Level;
	}
	glPopDebugGroup();
}
#endif


void AllocateRenderTargets(int ScreenWidth, int ScreenHeight)
{
	static bool Initialized = false;
	if (Initialized)
	{
		glDeleteFramebuffers(1, &DepthPass);
		glDeleteFramebuffers(1, &ColorPass);
		glDeleteTextures(1, &DepthBuffer);
		glDeleteTextures(1, &PositionBuffer);
		glDeleteTextures(1, &NormalBuffer);
		glDeleteTextures(1, &SubtreeBuffer);
		glDeleteTextures(1, &MaterialBuffer);
		glDeleteTextures(1, &ColorBuffer);
		if (HeadlessMode)
		{
			glDeleteFramebuffers(1, &FinalPass);
			glDeleteTextures(1, &FinalBuffer);
		}
#if ENABLE_OCCLUSION_CULLING
		{
			glDeleteTextures(1, &DepthPyramidBuffer);
			for (Buffer& Slice : DepthPyramidSlices)
			{
				Slice.Release();
			}
			DepthPyramidSlices.clear();
		}
#if DEBUG_OCCLUSION_CULLING
		glDeleteTextures(1, &OcclusionDebugBuffer);
#endif
#endif
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
		glTextureStorage2D(MaterialBuffer, 1, GL_RGB8, ScreenWidth, ScreenHeight);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(MaterialBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, MaterialBuffer, -1, "Material ID");

#if DEBUG_OCCLUSION_CULLING
		glCreateTextures(GL_TEXTURE_2D, 1, &OcclusionDebugBuffer);
		glTextureStorage2D(OcclusionDebugBuffer, 1, GL_RGBA32F, ScreenWidth, ScreenHeight);
		glTextureParameteri(OcclusionDebugBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(OcclusionDebugBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(OcclusionDebugBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(OcclusionDebugBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, OcclusionDebugBuffer, -1, "Occlusion Debug");
#endif

		glCreateFramebuffers(1, &DepthPass);
		glObjectLabel(GL_FRAMEBUFFER, DepthPass, -1, "Depth Pass");
		glNamedFramebufferTexture(DepthPass, GL_DEPTH_ATTACHMENT, DepthBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT0, PositionBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT1, NormalBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT2, SubtreeBuffer, 0);
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT3, MaterialBuffer, 0);
#if DEBUG_OCCLUSION_CULLING
		glNamedFramebufferTexture(DepthPass, GL_COLOR_ATTACHMENT4, OcclusionDebugBuffer, 0);
		GLenum ColorAttachments[5] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4 };
		glNamedFramebufferDrawBuffers(DepthPass, 5, ColorAttachments);
#else
		GLenum ColorAttachments[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
		glNamedFramebufferDrawBuffers(DepthPass, 4, ColorAttachments);
#endif
	}

	// Depth pyramid.
#if ENABLE_OCCLUSION_CULLING
	{
		const int Levels = (int)min(
			max(floor(log2(double(ScreenWidth))), 1.0),
			max(floor(log2(double(ScreenHeight))), 1.0)) + 1;

		glCreateTextures(GL_TEXTURE_2D, 1, &DepthPyramidBuffer);
		glTextureStorage2D(DepthPyramidBuffer, Levels, GL_R32F, ScreenWidth, ScreenHeight);
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, DepthPyramidBuffer, -1, "Depth Pyramid");

		DepthPyramidSlices.resize(Levels);

		DepthPyramidSliceUpload BufferData = \
		{
			ScreenWidth,
			ScreenHeight,
			0,
			0
		};

		for (int Level = 0; Level < Levels; ++Level)
		{
			DepthPyramidSlices[Level].Upload((void*)&BufferData, sizeof(BufferData));
			BufferData.Width = max(BufferData.Width / 2, 1);
			BufferData.Height = max(BufferData.Height / 2, 1);
			BufferData.Level++;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, DepthPass);
		glClear(GL_DEPTH_BUFFER_BIT);
		UpdateDepthPyramid(ScreenWidth, ScreenHeight);
	}
#endif

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
		glObjectLabel(GL_FRAMEBUFFER, ColorPass, -1, "Color Pass");
		glNamedFramebufferTexture(ColorPass, GL_COLOR_ATTACHMENT0, ColorBuffer, 0);
		GLenum ColorAttachments[1] = { GL_COLOR_ATTACHMENT0 };
		glNamedFramebufferDrawBuffers(ColorPass, 1, ColorAttachments);
	}

	// Final pass
	if (HeadlessMode)
	{
		glCreateTextures(GL_TEXTURE_2D, 1, &FinalBuffer);
		glTextureStorage2D(FinalBuffer, 1, GL_RGB8, int(ScreenWidth), int(ScreenHeight));
		glTextureParameteri(FinalBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(FinalBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(FinalBuffer, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(FinalBuffer, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glObjectLabel(GL_TEXTURE, FinalBuffer, -1, "FinalBuffer");

		glCreateFramebuffers(1, &FinalPass);
		glObjectLabel(GL_FRAMEBUFFER, FinalPass, -1, "FinalPass");
		glNamedFramebufferTexture(FinalPass, GL_COLOR_ATTACHMENT0, FinalBuffer, 0);
		GLenum ColorAttachments[1] = { GL_COLOR_ATTACHMENT0 };
		glNamedFramebufferDrawBuffers(FinalPass, 1, ColorAttachments);
	}
}


void DumpFrameBuffer(int ScreenWidth, int ScreenHeight, std::vector<unsigned char>& PixelData)
{
	const size_t Channels = 3;
	PixelData.resize(size_t(ScreenWidth) * size_t(ScreenHeight) * Channels);
	glNamedFramebufferReadBuffer(FinalPass, GL_COLOR_ATTACHMENT0);
	glReadPixels(0, 0, GLsizei(ScreenWidth), GLsizei(ScreenHeight), GL_RGB, GL_UNSIGNED_BYTE, PixelData.data());
}


void EncodeBase64(std::vector<unsigned char>& Bytes, std::vector<char>& Encoded)
{
	const std::string Base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	// Every three input bytes produces 4 output characters.  To keep things simple,
	// if we are short a few, the input sequence will be 0-padded to fill out the remainder.

	const size_t Words = (Bytes.size() + 2) / 3; // Divide by 3, rounding up.
	const size_t BytesPerWord = 3;
	const size_t GlyphsPerWord = 4;
	const size_t BitsPerGlyph = 6;

	Encoded.reserve(Words * GlyphsPerWord);
	for (int w = 0; w < Words; ++w)
	{
		uint32_t Chunk = 0;
		for (int b = 0; b < BytesPerWord; ++b)
		{
			Chunk <<= 8;
			const int i = BytesPerWord * w + b;
			if (i < Bytes.size())
			{
				Chunk |= Bytes[i];
			}
		}

		int Shift = GlyphsPerWord * BitsPerGlyph;
		for (int g = 0; g < GlyphsPerWord; ++g)
		{
			const int Inverse = (GlyphsPerWord - 1) - g;
			const size_t Glyph = (Chunk >> (Inverse * BitsPerGlyph)) & 63;
			if (Glyph < Base64.size())
			{
				Encoded.push_back(Base64[Glyph]);
			}
		}
	}
}


struct ViewInfoUpload
{
	glm::mat4 WorldToLastView = glm::identity<glm::mat4>();
	glm::mat4 WorldToView = glm::identity<glm::mat4>();
	glm::mat4 ViewToWorld = glm::identity<glm::mat4>();
	glm::mat4 ViewToClip = glm::identity<glm::mat4>();
	glm::mat4 ClipToView = glm::identity<glm::mat4>();
	glm::vec4 CameraOrigin = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ScreenSize = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ModelMin = glm::vec4(0.0, 0.0, 0.0, 0.0);
	glm::vec4 ModelMax = glm::vec4(0.0, 0.0, 0.0, 0.0);
	float CurrentTime = -1.0;
	bool Perspective = true;
	float Padding[2] = { 0 };
};


struct OutlinerOptionsUpload
{
	GLuint OutlinerFlags;
	GLuint Unused1;
	GLuint Unused2;
	GLuint Unused3;
};


void SetPipelineDefaults()
{
	// For drawing without a VBO bound.
	GLuint NullVAO;
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

	glDisable(GL_DITHER);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
	glDepthRange(1.0, 0.0);
}


// Renderer setup.
StatusCode SetupRenderer()
{
	SetPipelineDefaults();

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepth(0.0);

#if VISUALIZE_CLUSTER_COVERAGE
	RETURN_ON_FAIL(ClusterCoverageShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("cluster_coverage.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("cluster_coverage.fs.glsl", true)} },
		"Cluster Coverage Shader"));
#else

	RETURN_ON_FAIL(PaintShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("outliner.fs.glsl", true)} },
		"Outliner Shader"));

	RETURN_ON_FAIL(BgShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("bg.fs.glsl", true)} },
		"Background Shader"));
#endif

	RETURN_ON_FAIL(GatherDepthShader.Setup(
		{ {GL_COMPUTE_SHADER, ShaderSource("gather_depth.cs.glsl", true)} },
		"Depth Pyramid Shader"));

	RETURN_ON_FAIL(ResolveOutputShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("resolve.fs.glsl", true)} },
		"Resolve BackBuffer Shader"));

	RETURN_ON_FAIL(NoiseShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("noise.fs.glsl", true)} },
		"Noise Shader"));

	RETURN_ON_FAIL(OctreeDebugShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("cluster_draw.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, GeneratedShader("math.glsl", "", "octree_debug.fs.glsl")} },
		"Octree Debug Shader"));

	DepthTimeQuery.Create();
	GridBgTimeQuery.Create();
	OutlinerTimeQuery.Create();
	UiTimeQuery.Create();

	return StatusCode::PASS;
}


double ShaderCompilerConvergenceMs = 0.0;
Clock::time_point ShaderCompilerStart;
void CompileNewShaders(std::vector<SDFModel*>& IncompleteModels, const double LastInnerFrameDeltaMs)
{
	BeginEvent("Compile New Shaders");
	Clock::time_point ProcessingStart = Clock::now();

	double Budget = 16.6 - LastInnerFrameDeltaMs;
	Budget = fmaxf(Budget, 1.0);
	Budget = fminf(Budget, 14.0);

	for (SDFModel* Model : IncompleteModels)
	{
		while (Model->HasPendingShaders())
		{
			Model->CompileNextShader();

			std::chrono::duration<double, std::milli> Delta = Clock::now() - ProcessingStart;
			ShaderCompilerConvergenceMs += Delta.count();
			if (!HeadlessMode && Delta.count() > Budget)
			{
				goto timeout;
			}
		}
	}
timeout:

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	EndEvent();
}


bool UsePerspective = true;
float OrthoScale = 64.0;
float CameraFov = 45.0;
float CameraNear = 0.1;
float CameraFar = 1000.0;
glm::mat4 GetViewToClip(int ViewportWidth, int ViewportHeight)
{
	const float AspectRatio = float(ViewportWidth) / float(ViewportHeight);
	if (UsePerspective)
	{
		return glm::infinitePerspective(glm::radians(CameraFov), AspectRatio, CameraNear);
	}
	else
	{
		float Scale = (1.0 / OrthoScale) * .5;
		float Horizontal = ViewportWidth * Scale;
		float Vertical = ViewportHeight * Scale;
		return glm::ortho(-Horizontal, Horizontal, -Vertical, Vertical, CameraNear, CameraFar);
	}
}


int MouseMotionX = 0;
int MouseMotionY = 0;
int MouseMotionZ = 0;
int BackgroundMode = 0;
int ForegroundMode = 0;

bool FixedCamera = false;
glm::vec3 FixedOrigin = glm::vec3(0, -1, 0);
glm::vec3 FixedFocus = glm::vec3(0, 0, 0);
glm::vec3 FixedUp = glm::vec3(0, 0, 1);

bool ShowSubtrees = false;
bool ShowHeatmap = false;
bool HighlightEdges = true;
bool ResetCamera = true;
bool ShowOctree = false;
bool ShowLeafCount = false;
bool ShowWireframe = false;
bool FreezeCulling = false;
bool RealtimeMode = false;
bool ShowStatsOverlay = false;
float PresentFrequency = 0.0;
float PresentDeltaMs = 0.0;
double LastInnerFrameDeltaMs = 0.0;
glm::vec3 CameraFocus = glm::vec3(0.0, 0.0, 0.0);
void RenderFrame(int ScreenWidth, int ScreenHeight, std::vector<SDFModel*>& RenderableModels, ViewInfoUpload& UploadedView, bool FullRedraw = true)
{
	BeginEvent("RenderFrame");
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


	static glm::mat4 WorldToLastView = glm::identity<glm::mat4>();

	if (FixedCamera)
	{
		const glm::mat4 WorldToView = glm::lookAt(FixedOrigin, FixedFocus, FixedUp);
		const glm::mat4 ViewToWorld = glm::inverse(WorldToView);

		const glm::mat4 ViewToClip = GetViewToClip(Width, Height);
		const glm::mat4 ClipToView = inverse(ViewToClip);

		UploadedView = {
			WorldToLastView,
			WorldToView,
			ViewToWorld,
			ViewToClip,
			ClipToView,
			glm::vec4(FixedOrigin, 1.0f),
			glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height),
			glm::vec4(ModelBounds.Min, 1.0),
			glm::vec4(ModelBounds.Max, 1.0),
			float(CurrentTime),
			UsePerspective,
		};
		ViewInfo.Upload((void*)&UploadedView, sizeof(UploadedView));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);

		if (!FreezeCulling)
		{
			WorldToLastView = WorldToView;
		}
	}
	else
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

		const glm::mat4 ViewToClip = GetViewToClip(Width, Height);
		const glm::mat4 ClipToView = inverse(ViewToClip);

		UploadedView = {
			WorldToLastView,
			WorldToView,
			ViewToWorld,
			ViewToClip,
			ClipToView,
			glm::vec4(CameraOrigin, 1.0f),
			glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height),
			glm::vec4(ModelBounds.Min, 1.0),
			glm::vec4(ModelBounds.Max, 1.0),
			float(CurrentTime),
			UsePerspective,
		};
		ViewInfo.Upload((void*)&UploadedView, sizeof(UploadedView));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);

		if (!FreezeCulling)
		{
			WorldToLastView = WorldToView;
		}
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
		if (HighlightEdges)
		{
			OutlinerFlags |= 1 << 2;
		}
		if (ShowOctree)
		{
			OutlinerFlags |= 1 | 1 << 3;
		}
		if (ShowLeafCount)
		{
			OutlinerFlags |= 1 << 4;
		}
		if (ForegroundMode == 1)
		{
			OutlinerFlags |= 1 << 5;
		}
		if (ForegroundMode == 2)
		{
			OutlinerFlags |= 1 << 6;
		}
		OutlinerOptionsUpload BufferData = {
			OutlinerFlags,
			0,
			0,
			0
		};
		OutlinerOptions.Upload((void*)&BufferData, sizeof(BufferData));
	}

	if (RenderableModels.size() > 0)
	{
		if (FullRedraw)
		{
			BeginEvent("Depth");
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Depth");
			DepthTimeQuery.Start();
			glBindFramebuffer(GL_FRAMEBUFFER, DepthPass);
#if ENABLE_OCCLUSION_CULLING
			glBindTextureUnit(1, DepthPyramidBuffer);
#endif
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_GREATER);
			glClear(GL_DEPTH_BUFFER_BIT);
			if (ShowLeafCount)
			{
				glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			if (ShowHeatmap)
			{
				DepthTimeQuery.Stop();
			}

			{
				struct ShaderProgram* DebugShader = nullptr;
				if (ShowOctree || ShowLeafCount)
				{
					DebugShader = &OctreeDebugShader;
				}
				for (SDFModel* Model : RenderableModels)
				{
					Model->Draw(ShowOctree, ShowLeafCount, ShowHeatmap, ShowWireframe, DebugShader);
				}
			}

			if (!ShowHeatmap)
			{
				DepthTimeQuery.Stop();
			}
			glPopDebugGroup();
			EndEvent();
		}
#if ENABLE_OCCLUSION_CULLING
		if (!FreezeCulling)
		{
			UpdateDepthPyramid(ScreenWidth, ScreenHeight);
		}
#endif
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Background");
			glBindFramebuffer(GL_FRAMEBUFFER, ColorPass);
			GridBgTimeQuery.Start();
			glEnable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glDepthFunc(GL_EQUAL);
			switch (BackgroundMode)
			{
			case 0:
				BgShader.Activate();
				glDrawArrays(GL_TRIANGLES, 0, 3);
				break;
			default:
				BackgroundMode = -1;
				glClearColor(BackgroundColor.r, BackgroundColor.g, BackgroundColor.b, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			GridBgTimeQuery.Stop();
			glPopDebugGroup();
		}
		{
			glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Paint");
			OutlinerTimeQuery.Start();
			glBindTextureUnit(1, DepthBuffer);
			glBindTextureUnit(2, PositionBuffer);
			glBindTextureUnit(3, NormalBuffer);
			glBindTextureUnit(4, SubtreeBuffer);
			glBindTextureUnit(5, MaterialBuffer);
			OutlinerOptions.Bind(GL_UNIFORM_BUFFER, 2);
			DepthTimeBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			PaintShader.Activate();
			glDrawArrays(GL_TRIANGLES, 0, 3);
			OutlinerTimeQuery.Stop();
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


void SetClearColor(glm::vec3& Color)
{
	BackgroundMode = 1;
	BackgroundColor = Color;
}


void SetOutline(bool OutlinerState)
{
	HighlightEdges = OutlinerState;
}


void SetFixedCamera(glm::vec3& Origin, glm::vec3& Focus, glm::vec3& Up)
{
	FixedCamera = true;
	FixedOrigin = Origin;
	FixedFocus = Focus;
	FixedUp = Up;
}


void ToggleFullScreen(SDL_Window* Window)
{
	static bool FullScreen = false;
	FullScreen = !FullScreen;
	SDL_SetWindowFullscreen(Window, FullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}


std::vector<std::string> ScriptErrors;
void PostScriptError(std::string ErrorMessage)
{
	std::cout << ErrorMessage << "\n";
	ScriptErrors.push_back(ErrorMessage);
}


double ModelProcessingStallMs = 0.0;
void LoadModelCommon(std::function<void()> LoadingCallback)
{
	BeginEvent("Load Model");
	UnloadAllModels();

	ClearTreeEvaluator();
	FixedCamera = false;

	BackgroundColor = DefaultBackgroundColor;

	Clock::time_point StartTimePoint = Clock::now();

	LoadingCallback();

	Clock::time_point EndTimePoint = Clock::now();
	std::chrono::duration<double, std::milli> Delta = EndTimePoint - StartTimePoint;
	ModelProcessingStallMs = Delta.count();
	ShaderCompilerConvergenceMs = 0.0;
	ShaderCompilerStart = Clock::now();
	EndEvent();
}


void CreateScriptEnvironment(const Language Runtime)
{
	switch (Runtime)
	{
	case Language::Lua:
#if EMBED_LUA
		delete MainEnvironment;
		MainEnvironment = new LuaEnvironment();
#else
		ScriptErrors.push_back(std::string("The Lua language runtime is not available in this build :(\n"));
#endif
		break;

	case Language::Racket:
#if EMBED_RACKET
		delete MainEnvironment;
		MainEnvironment = new RacketEnvironment();
#else
		ScriptErrors.push_back(std::string("The Racket language runtime is not available in this build :(\n"));
#endif
		break;

	default:
		ScriptErrors.push_back(std::string("Unknown source language.\n"));
	}
}


void LoadModel(std::string Path, Language Runtime)
{
	static std::string LastPath = "";
	if (Path.size() == 0)
	{
		// Reload
		Path = LastPath;
		Runtime = MainEnvironment->GetLanguage();
	}
	else
	{
		ResetCamera = true;
	}
	if (Path.size() > 0)
	{
		CreateScriptEnvironment(Runtime);
		LastPath = Path;
		MainEnvironment->LoadFromPath(Path);
	}
}


void ReloadModel()
{
	LoadModel("", Language::Unknown);
}


void ReadInputModel(Language Runtime)
{
	std::istreambuf_iterator<char> begin(std::cin), end;
	std::string Source(begin, end);

	if (Source.size() > 0)
	{
		std::cout << "Evaluating data from stdin.\n";
		CreateScriptEnvironment(Runtime);
		MainEnvironment->LoadFromString(Source);
		std::cout << "Done!\n";
	}
	else
	{
		std::cout << "No data provided.\n";
	}
}


Language LanguageForPath(std::string Path)
{
	const std::regex LuaFile(".*?\\.(lua)$", std::regex::icase);
	const std::regex RacketFile(".*?\\.(rkt)$", std::regex::icase);

	if (std::regex_match(Path, LuaFile))
	{
		return Language::Lua;
	}
	else if (std::regex_match(Path, RacketFile))
	{
		return Language::Racket;
	}
	else
	{
		return Language::Unknown;
	}
}


ExportFormat ExportFormatForPath(std::string Path)
{
	const std::regex PlyFile(".*?\\.(ply)$", std::regex::icase);
	const std::regex StlFile(".*?\\.(stl)$", std::regex::icase);
	const std::regex VoxFile(".*?\\.(vox)$", std::regex::icase);

	if (std::regex_match(Path, PlyFile))
	{
		return ExportFormat::PLY;
	}
	else if (std::regex_match(Path, StlFile))
	{
		return ExportFormat::STL;
	}
	else if (std::regex_match(Path, VoxFile))
	{
		return ExportFormat::VOX;
	}
	else
	{
		Assert(false);
		return ExportFormat::Unknown;
	}
}


void OpenModel()
{
	std::string Filter = "";
	{
		std::string Separator = "";

#if EMBED_MULTI
		// TODO: This will need to be revised if--Madoka help me--I decide to embed another optional language runtime.
		Filter = fmt::format("{}{}Tangerines (*.lua; *.rkt){{.lua,.rkt}}", Filter, Separator);
		Separator = ",";
#endif

#if EMBED_LUA
		Filter = fmt::format("{}{}Lua Sources (*.lua){{.lua}}", Filter, Separator);
		Separator = ",";
#endif

#if EMBED_RACKET
		Filter = fmt::format("{}{}Racket Sources (*.rkt){{.rkt}}", Filter, Separator);
		Separator = ",";
#endif

		Filter = fmt::format("{}{}.*", Filter, Separator);
	}

	ifd::FileDialog::Instance().Open("OpenModelDialog", "Open a model", Filter, false, LastOpenDir.string());
}


void GetMouseStateForGL(SDL_Window* Window, int& OutMouseX, int& OutMouseY)
{
	int TmpMouseY;
	SDL_GetMouseState(&OutMouseX, &TmpMouseY);
	int WindowWidth;
	int WindowHeight;
	SDL_GetWindowSize(Window, &WindowWidth, &WindowHeight);
	OutMouseY = WindowHeight - TmpMouseY - 1;
}


void WorldSpaceRay(const ViewInfoUpload& View, int ScreenX, int ScreenY, int ScreenWidth, int ScreenHeight, glm::vec3& Origin, glm::vec3& Direction)
{
	glm::vec4 ViewPosition;

	if (UsePerspective)
	{
		glm::vec4 ClipPosition(
			glm::clamp(float(ScreenX) / float(ScreenWidth), 0.0f, 1.0f) * 2.0 - 1.0,
			glm::clamp(float(ScreenHeight - ScreenY) / float(ScreenHeight), 0.0f, 1.0f) * 2.0 - 1.0,
			-1.0f,
			1.0f);
		ViewPosition = View.ClipToView * ClipPosition;

		Origin = View.CameraOrigin;
	}
	else
	{
		glm::vec4 ClipPosition(
			glm::clamp(float(ScreenX) / float(ScreenWidth), 0.0f, 1.0f) * 2.0 - 1.0,
			glm::clamp(float(ScreenHeight - ScreenY) / float(ScreenHeight), 0.0f, 1.0f) * 2.0 - 1.0,
			1.0f,
			1.0f);
		ViewPosition = View.ClipToView * ClipPosition;

		glm::vec4 ViewOrigin = glm::vec4(ViewPosition.xy, 0.0, ViewPosition.w);
		glm::vec4 WorldOrigin = View.ViewToWorld * ViewOrigin;
		Origin = glm::vec3(WorldOrigin.xyz) / WorldOrigin.w;
	}

	{
		glm::vec4 WorldPosition = View.ViewToWorld * ViewPosition;
		WorldPosition /= WorldPosition.w;
		glm::vec3 Ray = glm::vec3(WorldPosition.xyz) - glm::vec3(Origin);
		Direction = glm::normalize(Ray);
	}
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

	static bool ShowLicenses = false;

	static bool ShowFocusOverlay = false;
	static bool ShowPrettyTrees = false;

	static bool ShowExportOptions = false;
	static float ExportStepSize;
	static float ExportSplitStep[3];
	static float ExportScale;
	static bool ExportSkipRefine;
	static int ExportRefinementSteps;
	static ExportFormat ExportMeshFormat;
	static bool ExportPointCloud = false;
	static float MagicaGridSize = 1.0;
	static int MagicaColorIndex = 0;
	static std::string ExportPath;

	static bool ShowChangeIterations = false;
	static int NewMaxIterations = 0;

	const bool DefaultExportSkipRefine = false;
	const float DefaultExportStepSize = 0.05;
	const float DefaultExportScale = 1.0;
	const int DefaultExportRefinementSteps = 5;
	const float DefaultMagicaGridSize = 0.05;

	if (!HeadlessMode && ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open", "Ctrl+O"))
			{
				OpenModel();
			}
			if (ImGui::MenuItem("Reload", "Ctrl+R"))
			{
				ReloadModel();
			}
			if (ImGui::MenuItem("Export As...", nullptr, false, TreeEvaluator != nullptr))
			{
				ifd::FileDialog::Instance().Save(
					"ModelExportDialog",
					"Export Model",
					"PLY Model (*.ply){.ply},"
					"STL Model (*.stl){.stl},"
					"Magica Voxel (*.vox){.vox},");
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
				if (ImGui::MenuItem("Solid Color", nullptr, BackgroundMode == -1))
				{
					BackgroundMode = -1;
				}
				if (ImGui::MenuItem("Test Grid", nullptr, BackgroundMode == 0))
				{
					BackgroundMode = 0;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Foreground"))
			{
				if (ImGui::MenuItem("PBRBR", nullptr, ForegroundMode == 0))
				{
					ForegroundMode = 0;
				}
				if (ImGui::MenuItem("Metalic", nullptr, ForegroundMode == 1))
				{
					ForegroundMode = 1;
				}
				if (ImGui::MenuItem("Vaporwave", nullptr, ForegroundMode == 2))
				{
					ForegroundMode = 2;
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Highlight Edges", nullptr, &HighlightEdges))
			{
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
		if (ImGui::BeginMenu("Debug"))
		{
			bool DebugOff = !(ShowSubtrees || ShowHeatmap || ShowOctree || ShowLeafCount);
			if (ImGui::MenuItem("Off", nullptr, &DebugOff))
			{
				ShowSubtrees = false;
				ShowOctree = false;
				ShowHeatmap = false;
				ShowLeafCount = false;
			}
			if (ImGui::MenuItem("Shader Groups", nullptr, &ShowSubtrees))
			{
				ShowOctree = false;
				ShowHeatmap = false;
				ShowLeafCount = false;
			}
			if (ImGui::MenuItem("Shader Heatmap", nullptr, &ShowHeatmap))
			{
				ShowOctree = false;
				ShowSubtrees = false;
				ShowLeafCount = false;
			}
			if (ImGui::MenuItem("Octree", nullptr, &ShowOctree))
			{
				ShowHeatmap = false;
				ShowSubtrees = false;
				ShowLeafCount = false;
			}
			if (ImGui::MenuItem("CSG Leaf Count", nullptr, &ShowLeafCount))
			{
				ShowOctree = false;
				ShowHeatmap = false;
				ShowSubtrees = false;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Wireframe", nullptr, &ShowWireframe))
			{
			}
			if (ImGui::MenuItem("Freeze Culling", nullptr, &FreezeCulling))
			{
			}
			if (ImGui::MenuItem("Force Redraw", nullptr, &RealtimeMode))
			{
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
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Open Source Licenses", nullptr, &ShowLicenses))
			{
			}


			ImGui::EndMenu();
		}

		if (ifd::FileDialog::Instance().IsDone("OpenModelDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				const std::vector<std::filesystem::path>& Results = ifd::FileDialog::Instance().GetResults();
				Assert(Results.size() == 1);
				const std::string Path = Results[0].string();
				LoadModel(Path, LanguageForPath(Path));
				LastOpenDir =  Results[0].parent_path();
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("ModelExportDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				const std::vector<std::filesystem::path>& Results = ifd::FileDialog::Instance().GetResults();
				Assert(Results.size() == 1);
				ExportPath = Results[0].string();
				ExportMeshFormat = ExportFormatForPath(ExportPath);

				ExportPointCloud = ExportMeshFormat == ExportFormat::PLY;

				ShowExportOptions = true;

				ExportStepSize = DefaultExportStepSize;
				for (int i = 0; i < 3; ++i)
				{
					ExportSplitStep[i] = ExportStepSize;
				}
				MagicaGridSize = MagicaGridSize;
				ExportScale = DefaultExportScale;
				ExportSkipRefine = DefaultExportSkipRefine;
				ExportRefinementSteps = DefaultExportRefinementSteps;
			}
			ifd::FileDialog::Instance().Close();
		}

		bool ToggleInterpreted = false;
		if (Interpreted)
		{
			if (ImGui::MenuItem("[Interpreted Shaders]", nullptr, &ToggleInterpreted))
			{
				Interpreted = false;
				ReloadModel();
			}
		}
		else
		{
			if (ImGui::MenuItem("[Compiled Shaders]", nullptr, &ToggleInterpreted))
			{
				Interpreted = true;
				ReloadModel();
			}
		}

		bool ChangeIterations = false;
		std::string IterationsLabel = fmt::format("[Max Iterations: {}]", MaxIterations);
		if (ImGui::MenuItem(IterationsLabel.c_str(), nullptr, &ChangeIterations))
		{
			ShowChangeIterations = true;
			NewMaxIterations = MaxIterations;
		}

		ImGui::EndMainMenuBar();
	}

	if (ShowLicenses)
	{
		int Margin = 0;
		const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(MainViewport->WorkPos.x + Margin, MainViewport->WorkPos.y + Margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(MainViewport->WorkSize.x - Margin * 2, MainViewport->WorkSize.y - Margin * 2), ImGuiCond_Always);

		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_AlwaysVerticalScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;

		if (ImGui::Begin("Open Source Licenses", &ShowLicenses, WindowFlags))
		{
			ImGuiTabBarFlags TabBarFlags = ImGuiTabBarFlags_None;
			if (ImGui::BeginTabBar("Open Source Licenses", TabBarFlags))
			{
#include "licenses.inl"
			}
		}
		ImGui::End();
	}

	if (ShowChangeIterations)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin("Change Ray Marching Iterations", &ShowChangeIterations, WindowFlags))
		{
			ImGui::Text("MaxIterations");
			ImGui::SameLine();
			ImGui::InputInt("##MaxIterations", &NewMaxIterations, 10);
			if (NewMaxIterations < 1)
			{
				NewMaxIterations = 1;
			}
			if (ImGui::Button("Apply"))
			{
				ShowChangeIterations = false;
				OverrideMaxIterations(NewMaxIterations);
				ReloadModel();
			}
		}
		ImGui::End();
	}

	if (ShowLeafCount)
	{
		int MouseX;
		int MouseY;
		GetMouseStateForGL(Window, MouseX, MouseY);

		int LeafCount = 0;
		glBindFramebuffer(GL_READ_FRAMEBUFFER, DepthPass);
		glReadBuffer(GL_COLOR_ATTACHMENT2);
		glReadPixels(MouseX, MouseY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &LeafCount);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

		std::string Msg = fmt::format("CSG Leaf Count: {}\n", LeafCount);
		ImGui::SetTooltip("%s", Msg.c_str());
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

			ImGui::Checkbox("Perspective", &UsePerspective);

			ImGui::Text("NearPlane:\n");
			ImGui::InputFloat("##CameraNear", &CameraNear, CameraNear * 0.5);
			CameraNear = max(CameraNear, 0.001);

			if (UsePerspective)
			{
				ImGui::Text("Field of View:\n");
				ImGui::InputFloat("##CameraFov", &CameraFov, 1.0f);
				CameraFov = min(max(CameraFov, 0.001), 180);
			}
			else
			{
				ImGui::Text("FarPlane:\n");
				ImGui::InputFloat("##CameraFar", &CameraFar, CameraFar * 0.5);
				CameraFar = max(CameraFar, CameraNear + 1.0);

				ImGui::Text("Orthographic Scale:\n");
				ImGui::InputFloat("##OrthoScale", &OrthoScale, 16.0);
				OrthoScale = max(OrthoScale, 1.0);
			}
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
			ImGui::Text("  Processing: %.3f s\n", ModelProcessingStallMs / 1000.0);
			ImGui::Text(" Convergence: %.3f s\n", ShaderCompilerConvergenceMs / 1000.0);
		}
		ImGui::End();
	}

	std::vector<SDFModel*>& LiveModels = GetLiveModels();
	if (ShowPrettyTrees && LiveModels.size() > 0)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing;

		ImGui::SetNextWindowPos(ImVec2(10.0, 32.0), ImGuiCond_Appearing, ImVec2(0.0, 0.0));
		ImGui::SetNextWindowSize(ImVec2(256, 512), ImGuiCond_Appearing);

		if (ImGui::Begin("Shader Permutations", &ShowPrettyTrees, WindowFlags))
		{
			std::vector<ProgramTemplate*> AllProgramTemplates;
			for (SDFModel* LiveModels : LiveModels)
			{
				for (ProgramTemplate& ProgramFamily : LiveModels->ProgramTemplates)
				{
					AllProgramTemplates.push_back(&ProgramFamily);
				}
			}

			std::string Message = fmt::format("Shader Count: {}", AllProgramTemplates.size());
			ImGui::TextUnformatted(Message.c_str(), nullptr);

			bool First = true;
			for (ProgramTemplate* ProgramFamily : AllProgramTemplates)
			{
				ImGui::Separator();
				ImGui::TextUnformatted(ProgramFamily->PrettyTree.c_str(), nullptr);
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
				ImGui::ProgressBar(Progress.Secondary, ImVec2(-FLT_MIN, 0), "Vertex Attributes");
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
			ImGui::SetNextWindowSizeConstraints(ImVec2(250, 190), MaxSize);
			ImGui::OpenPopup("Export Options");
			if (ImGui::BeginPopupModal("Export Options", nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				if (ExportMeshFormat == ExportFormat::VOX)
				{
					ImGui::InputFloat("Voxel Size", &MagicaGridSize);
					ImGui::InputInt("Color Index", &MagicaColorIndex, 1, 10);

					if (ImGui::Button("Start"))
					{
						VoxExport(TreeEvaluator, ExportPath, 1.0 / MagicaGridSize, MagicaColorIndex);
						ShowExportOptions = false;
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel"))
					{
						ShowExportOptions = false;
					}
				}
				else
				{
					if (AdvancedOptions)
					{
						ImGui::InputFloat3("Voxel Size", ExportSplitStep);
						ImGui::InputFloat("Unit Scale", &ExportScale);
						ImGui::Checkbox("Skip Refinement", &ExportSkipRefine);
						if (!ExportSkipRefine)
						{
							ImGui::InputInt("Refinement Steps", &ExportRefinementSteps);
						}
					}
					else
					{
						ImGui::InputFloat("Voxel Size", &ExportStepSize);
						ImGui::InputFloat("Unit Scale", &ExportScale);
					}
					if (ExportMeshFormat == ExportFormat::PLY)
					{
						ImGui::Checkbox("Point Cloud Only", &ExportPointCloud);
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
							MeshExport(TreeEvaluator, ExportPath, ModelBounds.Min, ModelBounds.Max, VoxelSize, RefinementSteps, ExportMeshFormat, ExportPointCloud, ExportScale);
						}
						else
						{
							glm::vec3 VoxelSize = glm::vec3(ExportStepSize);
							MeshExport(TreeEvaluator, ExportPath, ModelBounds.Min, ModelBounds.Max, VoxelSize, DefaultExportRefinementSteps, ExportMeshFormat, ExportPointCloud, ExportScale);
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
				}
				ImGui::EndPopup();
			}
		}
	}

	if (ScriptErrors.size() > 0)
	{
		std::string ScriptError = ScriptErrors[ScriptErrors.size() - 1];
		{
			ImVec2 TextSize = ImGui::CalcTextSize(ScriptError.c_str(), nullptr);
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
					ImGui::TextUnformatted(ScriptError.c_str(), nullptr);
				}
				ImGui::EndChild();
			}

			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
				ScriptErrors.pop_back();
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy Error", ImVec2(120, 0)))
			{
				SDL_SetClipboardText(ScriptError.c_str());
			}

			ImGui::EndPopup();
		}
	}
}


void LoadBookmarks()
{
	std::filesystem::path BookmarksPath =
                // FIXME might be read-only
                Installed.ExecutableDir / "bookmarks.txt";
	if (std::filesystem::is_regular_file(BookmarksPath))
	{
		std::ifstream BookmarksFile;
		BookmarksFile.open(BookmarksPath);
		std::string Bookmark;
	repeat:
		std::getline(BookmarksFile, Bookmark);
		if (Bookmark.size() > 0)
		{
			if (std::filesystem::is_directory(Bookmark))
			{
				ifd::FileDialog::Instance().AddFavorite(Bookmark);
			}
			goto repeat;
		}
		BookmarksFile.close();
	}
}


void SaveBookmarks()
{
	std::filesystem::path BookmarksPath =
		// FIXME might be read-only
		Installed.ExecutableDir / "bookmarks.txt";
	const std::vector<std::string>& Bookmarks = ifd::FileDialog::Instance().GetFavorites();
	if (Bookmarks.size() > 0)
	{
		std::ofstream BookmarksFile;
		BookmarksFile.open(BookmarksPath);
		for (const std::string& Bookmark : Bookmarks)
		{
			BookmarksFile << Bookmark << std::endl;
		}
		BookmarksFile.close();
	}
}


SDL_Window* Window = nullptr;
SDL_GLContext Context = nullptr;

StatusCode Boot(int argc, char* argv[])
{
	RETURN_ON_FAIL(Installed.PopulateInstallationPaths());
	LastOpenDir = Installed.ModelsDir;
	LoadBookmarks();

	std::vector<std::string> Args;
	for (int i = 1; i < argc; ++i)
	{
		Args.push_back(argv[i]);
	}

	int WindowWidth = 900;
	int WindowHeight = 900;
	HeadlessMode = false;
	bool LoadFromStandardIn = false;
	Language PipeRuntime = Language::Unknown;
	{
		int Cursor = 0;
		while (Cursor < Args.size())
		{
			if (Args[Cursor] == "--headless" && (Cursor + 2) < Args.size())
			{
				HeadlessMode = true;
				WindowWidth = atoi(Args[Cursor + 1].c_str());
				WindowHeight = atoi(Args[Cursor + 2].c_str());
				Cursor += 3;
				continue;
			}
#if EMBED_LUA
			else if (Args[Cursor] == "--lua")
			{
				LoadFromStandardIn = true;
				PipeRuntime = Language::Lua;
				Cursor += 1;
				continue;
			}
#endif
#if EMBED_RACKET
			else if (Args[Cursor] == "--racket")
			{
				LoadFromStandardIn = true;
				PipeRuntime = Language::Racket;
				Cursor += 1;
				continue;
			}
#endif
			else if (Args[Cursor] == "--iterations" && (Cursor + 1) < Args.size())
			{
				const int MaxIterations = atoi(Args[Cursor + 1].c_str());
				OverrideMaxIterations(MaxIterations);
				Cursor += 2;
				continue;
			}
			else if (Args[Cursor] == "--use-rounded-stack")
			{
				// Implies "--interpreted"
				UseInterpreter();
				UseRoundedStackSize();
				Cursor += 1;
				continue;
			}
			else
			{
				std::cout << "Invalid commandline arg(s).\n";
				return StatusCode::FAIL;
			}
		}
	}

	{
		std::cout << "Setting up SDL2... ";
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, MINIMUM_VERSION_MAJOR);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, MINIMUM_VERSION_MINOR);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#if ENABLE_DEBUG_CONTEXTS
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
			Uint32 WindowFlags = SDL_WINDOW_OPENGL;
			if (HeadlessMode)
			{
				WindowFlags |= SDL_WINDOW_HIDDEN;
			}
			else
			{
				WindowFlags |= SDL_WINDOW_RESIZABLE;

				SDL_DisplayMode DisplayMode;
				SDL_GetCurrentDisplayMode(0, &DisplayMode);
				const int MinDisplaySize = min(DisplayMode.w, DisplayMode.h);
				const int MaxWindowSize = max(480, MinDisplaySize - 128);
				WindowWidth = min(WindowWidth, MaxWindowSize);
				WindowHeight = min(WindowHeight, MaxWindowSize);
			}
			Window = SDL_CreateWindow(
				"Tangerine",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				WindowWidth, WindowHeight,
				WindowFlags);
		}
		else
		{
			std::cout << "Failed to initialize SDL2.\n";
			return StatusCode::FAIL;
		}
		if (Window == nullptr)
		{
			std::cout << "Failed to create SDL2 window.\n";
			return StatusCode::FAIL;
		}
		else
		{
			Context = SDL_GL_CreateContext(Window);
			if (Context == nullptr)
			{
				std::cout << "Failed to create SDL2 OpenGL Context.\n";
				return StatusCode::FAIL;
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
	ConnectDebugCallback(0);
	{
		MainEnvironment = new NullEnvironment();
#if EMBED_RACKET
		BootRacket();
#endif
	}
	{
		std::cout << "Setting up Dear ImGui... ";
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGui::StyleColorsLight();
		ImGuiStyle& Style = ImGui::GetStyle();
		Style.FrameBorderSize = 1.0f;
		ImGui_ImplSDL2_InitForOpenGL(Window, Context);
		ImGui_ImplOpenGL3_Init("#version 130");

		// Required by ImFileDialog
		{
			ifd::FileDialog::Instance().CreateTexture = [](uint8_t* Data, int Width, int Height, char Format) -> void*
			{
				GLuint Texture;
				glGenTextures(1, &Texture);
				glBindTexture(GL_TEXTURE_2D, Texture);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, (Format == 0) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, Data);
				glGenerateMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
				return (void*)Texture;
			};
			ifd::FileDialog::Instance().DeleteTexture = [](void* OpaqueHandle)
			{
				GLuint Texture = (GLuint)((uintptr_t)OpaqueHandle);
				glDeleteTextures(1, &Texture);
			};
		}

#if _WIN64
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
#endif
		std::cout << "Done!\n";
	}
	std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";

	if (SetupRenderer() == StatusCode::FAIL)
	{
		std::cout << "Failed to initialize the renderer.\n";
		return StatusCode::FAIL;
	}
	{
		StartWorkerThreads();
	}

	if (LoadFromStandardIn)
	{
		ReadInputModel(PipeRuntime);
	}

	if (HeadlessMode)
	{
		// There's a frame of delay before an error message would appear, so just process the DearImGui events twice.
		for (int i = 0; i < 2; ++i)
		{
			bool Ignore = true;
			RenderUI(Window, Ignore);
			ImGui::Render();
		}

		// Draw the requested frame or relevant error message.
		{
			MouseMotionX = 45;
			MouseMotionY = 45;

			std::vector<SDFModel*> IncompleteModels;
			GetIncompleteModels(IncompleteModels);
			if (IncompleteModels.size() > 0)
			{
				CompileNewShaders(IncompleteModels, LastInnerFrameDeltaMs);
			}
			std::vector<SDFModel*> RenderableModels;
			GetRenderableModels(RenderableModels);

			ViewInfoUpload UploadedView;
			RenderFrame(WindowWidth, WindowHeight, RenderableModels, UploadedView);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
			glFinish();
		}

		// Base64 encode the rendered image and dump it to stdout.
		{
			std::vector<unsigned char> PixelData;
			DumpFrameBuffer(WindowWidth, WindowHeight, PixelData);

			std::vector<char> Encoded;
			EncodeBase64(PixelData, Encoded);

			std::cout << "BEGIN RAW IMAGE";
			size_t i = 0;
			for (char& Byte : Encoded)
			{
				std::cout << Byte;
			}
		}
	}
	return StatusCode::PASS;
}


void Teardown()
{
	std::cout << "Shutting down...\n";

	if (MainEnvironment)
	{
		delete MainEnvironment;
	}
	{
		JoinWorkerThreads();
		UnloadAllModels();
		if (Context)
		{
			if (!HeadlessMode)
			{
				SaveBookmarks();
				ImGui_ImplOpenGL3_Shutdown();
				ImGui_ImplSDL2_Shutdown();
				ImGui::DestroyContext();
			}
			SDL_GL_DeleteContext(Context);
		}
		if (Window)
		{
			SDL_DestroyWindow(Window);
		}
	}
}


void MainLoop()
{
	Assert(!HeadlessMode);
	{
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

				int ScreenWidth;
				int ScreenHeight;
				SDL_GetWindowSize(Window, &ScreenWidth, &ScreenHeight);

				const bool HasMouseFocus = Window == SDL_GetMouseFocus();
				static int MouseX = 0;
				static int MouseY = 0;
				if (HasMouseFocus)
				{
					SDL_GetMouseState(&MouseX, &MouseY);
				}

				static ViewInfoUpload LastView;
				static std::vector<SDFModel*> IncompleteModels;
				static std::vector<SDFModel*> RenderableModels;

				static glm::vec3 MouseRay(0.0, 1.0, 0.0);
				static glm::vec3 RayOrigin(0.0, 0.0, 0.0);
				if (HasMouseFocus)
				{
					WorldSpaceRay(LastView, MouseX, MouseY, ScreenWidth, ScreenHeight, RayOrigin, MouseRay);
				}

				static bool LastExportState = false;
				bool ExportInProgress = GetExportProgress().Stage != 0;

				bool RequestDraw = RealtimeMode || ShowStatsOverlay || RenderableModels.size() == 0 || IncompleteModels.size() > 0 || LastExportState != ExportInProgress;
				LastExportState = ExportInProgress;

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
					else
					{
						RequestDraw = true;
					}
					static bool Dragging = false;
					if (!io.WantCaptureMouse && HasMouseFocus && RenderableModels.size() > 0)
					{
						switch (Event.type)
						{
						case SDL_MOUSEMOTION:
							if (Dragging)
							{
								MouseMotionX = Event.motion.xrel;
								MouseMotionY = Event.motion.yrel;
							}
							else
							{
								DeliverMouseMove(RayOrigin, MouseRay, Event.motion.x, Event.motion.y);
							}
							break;
						case SDL_MOUSEBUTTONDOWN:
							if (DeliverMouseButton(MouseEvent(Event.button, RayOrigin, MouseRay)))
							{
								Dragging = true;
								SDL_SetRelativeMouseMode(SDL_TRUE);
							}
							break;
						case SDL_MOUSEBUTTONUP:
							if (Dragging)
							{
								Dragging = false;
								SDL_SetRelativeMouseMode(SDL_FALSE);
							}
							else
							{
								DeliverMouseButton(MouseEvent(Event.button, RayOrigin, MouseRay));
							}
							break;
						case SDL_MOUSEWHEEL:
							if (DeliverMouseScroll(RayOrigin, MouseRay, Event.wheel.x, Event.wheel.y))
							{
								MouseMotionZ = Event.wheel.y;
							}
							break;
						}
					}
					else if (Dragging && ScriptErrors.size() > 0)
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
							ReloadModel();
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

				if (MainEnvironment->CanAdvance)
				{
					BeginEvent("Advance");
					double DeltaTime;
					double ElapsedTime;
					{
						Clock::time_point CurrentTimePoint = Clock::now();
						static Clock::time_point OriginTimePoint = CurrentTimePoint;
						static Clock::time_point LastTimePoint = CurrentTimePoint;
						{
							std::chrono::duration<double, std::milli> FrameDelta = CurrentTimePoint - LastTimePoint;
							DeltaTime = double(FrameDelta.count());
						}
						{
							std::chrono::duration<double, std::milli> EpochDelta = CurrentTimePoint - OriginTimePoint;
							ElapsedTime = double(EpochDelta.count());
						}
						LastTimePoint = CurrentTimePoint;
					}
					MainEnvironment->Advance(DeltaTime, ElapsedTime);
					RequestDraw = true;
					EndEvent();
				}

				if (RequestDraw || ExportInProgress)
				{
					{
						BeginEvent("Update UI");
						RenderUI(Window, Live);
						ImGui::Render();
						EndEvent();
					}
					{
						GetIncompleteModels(IncompleteModels);
						if (IncompleteModels.size() > 0)
						{
							CompileNewShaders(IncompleteModels, LastInnerFrameDeltaMs);
						}
						GetRenderableModels(RenderableModels);
					}
					RenderFrame(ScreenWidth, ScreenHeight, RenderableModels, LastView, RequestDraw);
					{
						BeginEvent("Dear ImGui Draw");
						glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Dear ImGui");
						UiTimeQuery.Start();
						ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
						UiTimeQuery.Stop();
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
						DepthElapsedTimeMs = DepthTimeQuery.ReadMs();
						GridBgElapsedTimeMs = GridBgTimeQuery.ReadMs();
						OutlinerElapsedTimeMs = OutlinerTimeQuery.ReadMs();
						UiElapsedTimeMs = UiTimeQuery.ReadMs();

						if (ShowHeatmap)
						{
							float Range = 0.0;
							std::vector<float> Upload;
							for (SDFModel* Model : RenderableModels)
							{
								for (ProgramTemplate* CompiledTemplate : Model->CompiledTemplates)
								{
									double ElapsedTimeMs = CompiledTemplate->DepthQuery.ReadMs();
									Upload.push_back(float(ElapsedTimeMs));
									DepthElapsedTimeMs += ElapsedTimeMs;
									Range = fmax(Range, float(ElapsedTimeMs));
								}
							}
							for (float& ElapsedTimeMs : Upload)
							{
								ElapsedTimeMs /= Range;
							}

							DepthTimeBuffer.Upload(Upload.data(), Upload.size() * sizeof(float));
						}
						EndEvent();
					}
				}
				EndEvent();
			}
		}
	}
}


#ifndef MINIMAL_DLL
int main(int argc, char* argv[])
{
	if (Boot(argc, argv) == StatusCode::PASS)
	{
		MainLoop();
	}
	Teardown();
	return 0;
}
#endif
