
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
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_clipboard.h>

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_opengl3.h>

#include "sdfs.h"
#include "shape_compiler.h"
#include "export.h"
#include "extern.h"

#include <iostream>
#include <iterator>
#include <cstring>
#include <map>
#include <vector>
#include <chrono>

#define EMBED_RACKET 0
#define EMBED_LUA 1

#if EMBED_RACKET
#include <chezscheme.h>
#include <racketcs.h>
#endif

#if EMBED_LUA
#include <lua/lua.hpp>

lua_State* LuaStack = nullptr;
#endif

#if _WIN64
#include <shobjidl.h>
#else
#include <gtk/gtk.h>
#endif

#include <fmt/format.h>

#include "profiling.h"

#include "errors.h"
#include "gl_boilerplate.h"
#include "gl_async.h"
#include "gl_debug.h"
#include "../shaders/defines.h"

#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2

#define ASYNC_SHADER_COMPILE 0

using Clock = std::chrono::high_resolution_clock;


bool HeadlessMode;


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
	ModelSubtree(uint32_t ShaderIndex, uint32_t SubtreeIndex, size_t ParamCount, float* InParams)
	{
		++ParamCount;
		size_t Padding = DIV_UP(ParamCount, 4) * 4 - ParamCount;
		size_t UploadSize = ParamCount + Padding;
		Params.reserve(UploadSize);
#if 1
		// This gives a different ID per shader permutation, which is more useful for debug views.
		Params.push_back(AsFloat(ShaderIndex));
#else
		// This should give a different ID per unique GLSL generated, but for some reason this doesn't
		// produce quite the right results.  May or may not be useful for other purposes with some work,
		// but I am unsure.
		Params.push_back(AsFloat(SubtreeIndex));
#endif
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
		, LeafCount(Old.LeafCount)
	{
		std::swap(Compiled, Old.Compiled);
		std::swap(DepthQuery, Old.DepthQuery);
		std::swap(Instances, Old.Instances);
	}

	SubtreeShader(std::string InDebugName, std::string InPrettyTree, std::string InDistSource, int InLeafCount)
		: LeafCount(InLeafCount)
	{
		Compiled.reset(new ShaderEnvelope);
		DebugName = InDebugName;
		PrettyTree = InPrettyTree;
		DistSource = InDistSource;
	}

	void StartCompile()
	{
		std::unique_ptr<ShaderProgram> NewShader;
		NewShader.reset(new ShaderProgram());
		NewShader->AsyncSetup(
			{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_draw.vs.glsl", true)},
			  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", DistSource, "shaders/cluster_draw.fs.glsl")} },
			DebugName.c_str());
		AsyncCompile(std::move(NewShader), Compiled);

		// Use a very long average window for draw time queries to reduce the likelihood of strobing in the heatmap view.
		DepthQuery.Create(1000);
	}

	ShaderProgram* GetCompiledShader()
	{
		return Compiled->Access();
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
		Compiled.reset();
		DepthQuery.Release();
	}

	int LeafCount;
	std::string DebugName;
	std::string PrettyTree;
	std::string DistSource;

	std::shared_ptr<ShaderEnvelope> Compiled;

	TimingQuery DepthQuery;

	std::vector<ModelSubtree> Instances;
};


std::map<std::string, size_t> SubtreeMap;
std::vector<SubtreeShader> SubtreeShaders;
std::vector<size_t> PendingShaders;


ModelSubtree* PendingSubtree = nullptr;
size_t EmitShader(std::string InSource, std::string InPretty, int LeafCount)
{
	std::string& Source = InSource;
	std::string& Pretty = InPretty;
	std::string& DebugName = InSource; // TODO

	auto Found = SubtreeMap.find(Source);

	size_t ShaderIndex;
	if (Found == SubtreeMap.end())
	{
		size_t Index = SubtreeShaders.size();
		SubtreeShaders.emplace_back(DebugName, Pretty, Source, LeafCount);
		SubtreeMap[Source] = Index;
		PendingShaders.push_back(Index);
		ShaderIndex = Index;
	}
	else
	{
		ShaderIndex = Found->second;
	}

	return ShaderIndex;
}

void EmitParameters(size_t ShaderIndex, uint32_t SubtreeIndex, std::vector<float> Params)
{
	// TODO: Instances is currently a vector, but should it be a map...?
	SubtreeShaders[ShaderIndex].Instances.emplace_back(ShaderIndex, SubtreeIndex, Params.size(), Params.data());
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
void SetTreeEvaluator(SDFNode* InTreeEvaluator, AABB Limits)
{
	ClearTreeEvaluator();
	TreeEvaluator = InTreeEvaluator;
	ModelBounds = Limits;// TreeEvaluator->Bounds();
}


ShaderProgram PaintShader;
ShaderProgram NoiseShader;
ShaderProgram BgShader;
ShaderProgram GatherDepthShader;
ShaderProgram ResolveOutputShader;
ShaderProgram OctreeDebugShader;

Buffer ViewInfo("ViewInfo Buffer");
Buffer OutlinerOptions("Outliner Options Buffer");
Buffer OctreeDebugOptions("Octree Debug Options Buffer");

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
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(DepthPyramidBuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
	GLuint Unused1;
	GLuint Unused2;
	GLuint Unused3;
};


struct OctreeDebugOptionsUpload
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

	RETURN_ON_FAIL(GatherDepthShader.Setup(
		{ {GL_COMPUTE_SHADER, ShaderSource("shaders/gather_depth.cs.glsl", true)} },
		"Depth Pyramid Shader"));

	RETURN_ON_FAIL(ResolveOutputShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/resolve.fs.glsl", true)} },
		"Resolve BackBuffer Shader"));

	RETURN_ON_FAIL(NoiseShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/splat.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, ShaderSource("shaders/noise.fs.glsl", true)} },
		"Noise Shader"));

	RETURN_ON_FAIL(OctreeDebugShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/cluster_draw.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", "", "shaders/octree_debug.fs.glsl")} },
		"Octree Debug Shader"));

	DepthTimeQuery.Create();
	GridBgTimeQuery.Create();
	OutlinerTimeQuery.Create();
	UiTimeQuery.Create();

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

	bool NeedBarrier = false;

	while (PendingShaders.size() > 0)
	{
		{
			BeginEvent("Compile Shader");
			size_t SubtreeIndex = PendingShaders.back();
			PendingShaders.pop_back();

			SubtreeShader& Shader = SubtreeShaders[SubtreeIndex];
			Shader.StartCompile();
			if (Shader.Instances.size() > 0)
			{
				Drawables.push_back(&Shader);
			}
			NeedBarrier = true;

			EndEvent();
		}

		std::chrono::duration<double, std::milli> Delta = Clock::now() - ProcessingStart;
		ShaderCompilerConvergenceMs += Delta.count();
		if (!HeadlessMode && Delta.count() > Budget)
		{
			break;
		}
	}

	if (NeedBarrier)
	{
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	}
	EndEvent();
}


int MouseMotionX = 0;
int MouseMotionY = 0;
int MouseMotionZ = 0;
int BackgroundMode = 0;
int ForegroundMode = 0;
bool ShowSubtrees = false;
bool ShowHeatmap = false;
bool HighlightEdges = true;
bool ResetCamera = true;
bool ShowOctree = false;
bool ShowLeafCount = false;
bool RealtimeMode = false;
bool ShowStatsOverlay = false;
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

	if (Drawables.size() > 0)
	{
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
			int NextOctreeID = 0;
			for (SubtreeShader* Drawable : Drawables)
			{
				ShaderProgram* Shader = Drawable->GetCompiledShader();
				if (!Shader)
				{
					if (!ShowOctree || !ShowLeafCount)
					{
						continue;
					}
				}

				BeginEvent("Draw Drawable");
				GLsizei DebugNameLen = Drawable->DebugName.size() < 100 ? -1 : 100;
				glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, DebugNameLen, Drawable->DebugName.c_str());
				if (ShowHeatmap)
				{
					Drawable->DepthQuery.Start();
				}

				if (ShowOctree || ShowLeafCount)
				{
					OctreeDebugShader.Activate();
				}
				else
				{
					Shader->Activate();
				}

				for (ModelSubtree& Subtree : Drawable->Instances)
				{
					Subtree.ParamsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 0);
					for (SubtreeSection& Section : Subtree.Sections)
					{
						if (ShowOctree || ShowLeafCount)
						{
							int UploadValue;
							if (ShowOctree)
							{
								UploadValue = ++NextOctreeID;
							}
							else
							{
								UploadValue = Drawable->LeafCount;
							}
							OctreeDebugOptionsUpload BufferData = {
								(GLuint)UploadValue,
								0,
								0,
								0
							};
							OctreeDebugOptions.Upload((void*)&BufferData, sizeof(BufferData));
							OctreeDebugOptions.Bind(GL_UNIFORM_BUFFER, 3);
						}
						Section.SectionBuffer.Bind(GL_UNIFORM_BUFFER, 2);
						glDrawArrays(GL_TRIANGLES, 0, 36);
					}
				}
				if (ShowHeatmap)
				{
					Drawable->DepthQuery.Stop();
				}
				glPopDebugGroup();
				EndEvent();
			}
			if (!ShowHeatmap)
			{
				DepthTimeQuery.Stop();
			}
			glPopDebugGroup();
			EndEvent();
		}
#if ENABLE_OCCLUSION_CULLING
		UpdateDepthPyramid(ScreenWidth, ScreenHeight);
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
				glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
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


void ToggleFullScreen(SDL_Window* Window)
{
	static bool FullScreen = false;
	FullScreen = !FullScreen;
	SDL_SetWindowFullscreen(Window, FullScreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}


std::vector<std::string> ScriptErrors;
#if EMBED_RACKET
extern "C" TANGERINE_API void RacketErrorCallback(const char* ErrorMessage)
{
	std::cout << ErrorMessage << "\n";
	ScriptErrors.push_back(std::string(ErrorMessage));
}
#endif


double ModelProcessingStallMs = 0.0;
template<typename T>
void LoadModelCommon(T& LoadingCallback)
{
	BeginEvent("Load Model");
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

	LoadingCallback();

	Clock::time_point EndTimePoint = Clock::now();
	std::chrono::duration<double, std::milli> Delta = EndTimePoint - StartTimePoint;
	ModelProcessingStallMs = Delta.count();

	PendingSubtree = nullptr;

	Drawables.reserve(PendingShaders.size());
	ShaderCompilerConvergenceMs = 0.0;
	ShaderCompilerStart = Clock::now();
	EndEvent();
}


void LoadModel(std::string Path)
{
	static std::string LastPath = "";
	if (Path.size() == 0)
	{
		// Reload
		Path = LastPath;
	}
	else
	{
		ResetCamera = true;
	}
	if (Path.size() > 0)
	{
		auto LoadAndProcess = [&]()
		{
#if EMBED_RACKET
			Sactivate_thread();
			ptr ModuleSymbol = Sstring_to_symbol("tangerine");
			ptr ProcSymbol = Sstring_to_symbol("renderer-load-and-process-model");
			ptr Proc = Scar(racket_dynamic_require(ModuleSymbol, ProcSymbol));
			ptr Args = Scons(Sstring(Path.c_str()), Snil);
			racket_apply(Proc, Args);
			Sdeactivate_thread();
#endif
#if EMBED_LUA
			int Error = luaL_loadfile(LuaStack, Path.c_str()) || lua_pcall(LuaStack, 0, 0, 0);
			if (Error)
			{
				std::string ErrorMessage = fmt::format("{}\n", lua_tostring(LuaStack, -1));
				ScriptErrors.push_back(std::string(ErrorMessage));
				lua_pop(LuaStack, 1);
			}
			else
			{
				lua_getglobal(LuaStack, "model");
				SDFNode** Model = (SDFNode**)luaL_checkudata(LuaStack, -1, "tangerine.sdf");
				if (Model)
				{
					CompileEvaluator(*Model);
				}
				else
				{
					std::cout << "Invalid model.\n";
				}
			}
#endif
		};

		LoadModelCommon(LoadAndProcess);

		LastPath = Path;
	}
}


void ReadInputModel()
{
#if 1
	std::istreambuf_iterator<char> begin(std::cin), end;
	std::string ModelSource(begin, end);
#else
	std::string ModelSource = "#lang s-exp tangerine/smol\n(sphere 1)\n";
#endif
	if (ModelSource.size() > 0)
	{
		std::cout << "Evaluating data from stdin.\n";
		auto EvalUntrusted = [&]()
		{
#if EMBED_RACKET
			Sactivate_thread();
			ptr ModuleSymbol = Sstring_to_symbol("tangerine");
			ptr ProcSymbol = Sstring_to_symbol("renderer-load-untrusted-model");
			ptr Proc = Scar(racket_dynamic_require(ModuleSymbol, ProcSymbol));
			ptr Args = Scons(Sstring_utf8(ModelSource.c_str(), ModelSource.size()), Snil);
			racket_apply(Proc, Args);
			Sdeactivate_thread();
#endif
#if EMBED_LUA
			int Error = luaL_loadstring(LuaStack, ModelSource.c_str()) || lua_pcall(LuaStack, 0, 0, 0);
			if (Error)
			{
				std::string ErrorMessage = fmt::format("{}\n", lua_tostring(LuaStack, -1));
				ScriptErrors.push_back(std::string(ErrorMessage));
				lua_pop(LuaStack, 1);
			}
#endif
		};
		LoadModelCommon(EvalUntrusted);
		std::cout << "Done!\n";
	}
	else
	{
		std::cout << "No data provided.\n";
	}
}


void OpenModel()
{
#if _WIN64
	char Path[MAX_PATH] = {};

	OPENFILENAMEA Dialog = { sizeof(Dialog) };
	Dialog.hwndOwner = 0;
#if EMBED_RACKET
	Dialog.lpstrFilter = "Racket Sources\0*.rkt\0All Files\0*.*\0";
#elif EMBED_LUA
	Dialog.lpstrFilter = "Lua Sources\0*.lua\0All Files\0*.*\0";
#else
	Dialog.lpstrFilter = "All Files\0*.*\0";
#endif
	Dialog.lpstrFile = Path;
	Dialog.nMaxFile = ARRAYSIZE(Path);
	Dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&Dialog))
	{
		LoadModel(std::string(Path));
	}
#else
	GtkWidget* Dialog = gtk_file_chooser_dialog_new("Open File",
		nullptr,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		"_Open",
		GTK_RESPONSE_ACCEPT,
		nullptr);
	gint Result = gtk_dialog_run(GTK_DIALOG(Dialog));
	if (Result == GTK_RESPONSE_ACCEPT)
	{
		GtkFileChooser* FileChooser = GTK_FILE_CHOOSER(Dialog);
		char* Path = gtk_file_chooser_get_filename(FileChooser);
		LoadModel(std::string(Path));
		g_free(Path);
	}
	gtk_widget_destroy(Dialog);
#endif
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
	static bool ShowPrettyTrees = false;

	static bool ShowExportOptions = false;
	static float ExportStepSize;
	static float ExportSplitStep[3];
	static bool ExportSkipRefine;
	static int ExportRefinementSteps;
	static ExportFormat ExportMeshFormat;
	static bool ExportPointCloud;

	static bool ShowChangeIterations = false;
	static int NewMaxIterations = 0;

	const bool DefaultExportSkipRefine = false;
	const float DefaultExportStepSize = 0.01;
	const int DefaultExportRefinementSteps = 5;

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
				LoadModel("");
			}
			bool AnyExport = false;
			if (ImGui::MenuItem("Export PLY", nullptr, false, TreeEvaluator != nullptr))
			{
				AnyExport = true;
				ExportMeshFormat = ExportFormat::PLY;
				ExportPointCloud = true;
			}
			if (ImGui::MenuItem("Export STL", nullptr, false, TreeEvaluator != nullptr))
			{
				AnyExport = true;
				ExportMeshFormat = ExportFormat::STL;
				ExportPointCloud = false;
			}
			if (AnyExport)
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

		bool ToggleInterpreted = false;
		if (Interpreted)
		{
			if (ImGui::MenuItem("[Interpreted Shaders]", nullptr, &ToggleInterpreted))
			{
				Interpreted = false;
				LoadModel("");
			}
		}
		else
		{
			if (ImGui::MenuItem("[Compiled Shaders]", nullptr, &ToggleInterpreted))
			{
				Interpreted = true;
				LoadModel("");
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
				LoadModel("");
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
		ImGui::SetTooltip(Msg.c_str());
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
			ImGui::Text("  Processing: %.3f s\n", ModelProcessingStallMs / 1000.0);
			ImGui::Text(" Convergence: %.3f s\n", ShaderCompilerConvergenceMs / 1000.0);
		}
		ImGui::End();
	}

	if (ShowPrettyTrees && SubtreeShaders.size() > 0)
	{
		ImGuiWindowFlags WindowFlags = \
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing;

		ImGui::SetNextWindowPos(ImVec2(10.0, 32.0), ImGuiCond_Appearing, ImVec2(0.0, 0.0));
		ImGui::SetNextWindowSize(ImVec2(256, 512), ImGuiCond_Appearing);

		if (ImGui::Begin("Shader Permutations", &ShowPrettyTrees, WindowFlags))
		{
			std::string Message = fmt::format("Shader Count: {}", SubtreeShaders.size());
			ImGui::TextUnformatted(Message.c_str(), nullptr);

			bool First = true;
			for (SubtreeShader& Subtree : SubtreeShaders)
			{
				ImGui::Separator();
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
			ImGui::SetNextWindowSizeConstraints(ImVec2(250, 165), MaxSize);
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
						MeshExport(TreeEvaluator, ModelBounds.Min, ModelBounds.Max, VoxelSize, RefinementSteps, ExportMeshFormat, ExportPointCloud);
					}
					else
					{
						glm::vec3 VoxelSize = glm::vec3(ExportStepSize);
						MeshExport(TreeEvaluator, ModelBounds.Min, ModelBounds.Max, VoxelSize, DefaultExportRefinementSteps, ExportMeshFormat, ExportPointCloud);
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


int main(int argc, char* argv[])
{
	std::string ExePath(argv[0]);
	std::vector<std::string> Args;
	for (int i = 1; i < argc; ++i)
	{
		Args.push_back(argv[i]);
	}

	int WindowWidth = 900;
	int WindowHeight = 900;
	HeadlessMode = false;
	bool LoadFromStandardIn = false;
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
			else if (Args[Cursor] == "--cin")
			{
				LoadFromStandardIn = true;
				Cursor += 1;
				continue;
			}
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
				return 0;
			}
		}
	}

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
			}
			Window = SDL_CreateWindow(
				"Tangerine",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				WindowWidth, WindowHeight,
				WindowFlags);
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
	ConnectDebugCallback(0);
	{
#if EMBED_RACKET
		std::cout << "Setting up Racket CS... ";
		racket_boot_arguments_t BootArgs;
		memset(&BootArgs, 0, sizeof(BootArgs));
		BootArgs.boot1_path = "./racket/petite.boot";
		BootArgs.boot2_path = "./racket/scheme.boot";
		BootArgs.boot3_path = "./racket/racket.boot";
		BootArgs.exec_file = "tangerine.exe";
		BootArgs.collects_dir = "./racket/collects";
		BootArgs.config_dir = "./racket/etc";
		racket_boot(&BootArgs);
		std::cout << "Done!\n";
#endif
#if EMBED_LUA
		LuaStack = luaL_newstate();
		luaL_openlibs(LuaStack);
		luaL_requiref(LuaStack, "tangerine", LuaOpenSDF, 1);
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
		return 0;
	}
	{
		StartWorkerThreads();
	}

	if (LoadFromStandardIn)
	{
		ReadInputModel();
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
			RenderFrame(WindowWidth, WindowHeight);
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
	else
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
				bool RequestDraw = RealtimeMode || ShowStatsOverlay || Drawables.size() == 0;
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
							LoadModel("");
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
				if (RequestDraw)
				{
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
							const size_t QueryCount = Drawables.size();
							float Range = 0.0;
							std::vector<float> Upload(QueryCount, 0.0);
							for (int i = 0; i < QueryCount; ++i)
							{
								double ElapsedTimeMs = Drawables[i]->DepthQuery.ReadMs();
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
				}
				EndEvent();
			}
		}
		std::cout << "Shutting down...\n";
	}
#if EMBED_LUA
	{
		lua_close(LuaStack);
	}
#endif
	{
		JoinWorkerThreads();
		for (SubtreeShader& Shader : SubtreeShaders)
		{
			Shader.Release();
		}
		if (!HeadlessMode)
		{
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();
		}
		SDL_GL_DeleteContext(Context);
		SDL_DestroyWindow(Window);
	}
	return 0;
}
