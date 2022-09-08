
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

#include "sdf_rendering.h"
#include "sdf_model.h"
#include "profiling.h"


extern ShaderProgram CullingShader;


struct OctreeDebugOptionsUpload
{
	GLuint OutlinerFlags;
	GLuint Wireframe;
	GLuint Unused1;
	GLuint Unused2;
};


Buffer OctreeDebugOptions("Octree Debug Options Buffer");


ProgramBuffer::ProgramBuffer(uint32_t ShaderIndex, uint32_t SubtreeIndex, size_t ParamCount, const float* InParams, const std::vector<AABB>& InVoxels)
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

	ParamsBuffer.DebugName = "Shape Program Buffer";
	ParamsBuffer.Upload((void*)Params.data(), UploadSize * sizeof(float));

	Voxels.reserve(InVoxels.size());
	for (const AABB& Bounds : InVoxels)
	{
		Voxels.emplace_back(Bounds);
	}
	VoxelsBuffer.Upload(Voxels.data(), sizeof(VoxelUpload) * Voxels.size());

	std::vector<DrawArraysIndirectCommand> Draws;
	Draws.resize(InVoxels.size());
	uint32_t Index = 0;
	for (DrawArraysIndirectCommand& Draw : Draws)
	{
		Draw.Count = 36;
		Draw.InstanceCount = 1;
		Draw.First = 36 * Index++;
		Draw.BaseInstance = 0;
	}
	DrawsBuffer.Upload(Draws.data(), sizeof(DrawArraysIndirectCommand) * Draws.size());
}

void ProgramBuffer::Release()
{
	ParamsBuffer.Release();
	VoxelsBuffer.Release();
}




ProgramTemplate::ProgramTemplate(ProgramTemplate&& Old)
	: DebugName(Old.DebugName)
	, PrettyTree(Old.PrettyTree)
	, DistSource(Old.DistSource)
	, LeafCount(Old.LeafCount)
{
	std::swap(Compiled, Old.Compiled);
	std::swap(DepthQuery, Old.DepthQuery);
	std::swap(ProgramVariants, Old.ProgramVariants);
}

ProgramTemplate::ProgramTemplate(std::string InDebugName, std::string InPrettyTree, std::string InDistSource, int InLeafCount)
	: LeafCount(InLeafCount)
{
	Compiled.reset(new ShaderEnvelope);
	DebugName = InDebugName;
	PrettyTree = InPrettyTree;
	DistSource = InDistSource;
}

void ProgramTemplate::StartCompile()
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

ShaderProgram* ProgramTemplate::GetCompiledShader()
{
	return Compiled->Access();
}

void ProgramTemplate::Reset()
{
	for (ProgramBuffer& ProgramVariant : ProgramVariants)
	{
		ProgramVariant.Release();
	}
	ProgramVariants.clear();
}

void ProgramTemplate::Release()
{
	Reset();
	Compiled.reset();
	DepthQuery.Release();
}


struct TransformUpload
{
	glm::mat4 LocalToWorld;
	glm::mat4 WorldToLocal;
};


void SDFModel::Draw(
	const bool ShowOctree,
	const bool ShowLeafCount,
	const bool ShowHeatmap,
	const bool Wireframe,
	ShaderProgram* DebugShader)
{
	if (!Visible)
	{
		return;
	}

	int NextOctreeID = 0;

	Transform.Fold();
	TransformUpload TransformData = {
		Transform.LastFold,
		Transform.LastFoldInverse
	};
	TransformBuffer.Upload((void*)&TransformData, sizeof(TransformUpload));
	TransformBuffer.Bind(GL_UNIFORM_BUFFER, 1);

	const bool DebugView = ShowOctree || ShowLeafCount || Wireframe;
	if (!DebugView)
	{
		// Clear the debug options buffer if we don't need it, so we don't persist wireframe mode.
		OctreeDebugOptionsUpload BufferData = {
			0,
			0,
			0,
			0
		};
		OctreeDebugOptions.Upload((void*)&BufferData, sizeof(BufferData));
		OctreeDebugOptions.Bind(GL_UNIFORM_BUFFER, 3);
	}

	for (ProgramTemplate* ProgramFamily : CompiledTemplates)
	{
		ShaderProgram* Shader = DebugShader ? DebugShader : ProgramFamily->GetCompiledShader();
		if (!Shader)
		{
			continue;
		}

		BeginEvent("Draw Drawable");
		GLsizei DebugNameLen = ProgramFamily->DebugName.size() < 100 ? -1 : 100;
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, DebugNameLen, ProgramFamily->DebugName.c_str());
		if (ShowHeatmap)
		{
			ProgramFamily->DepthQuery.Start();
		}

#if ENABLE_OCCLUSION_CULLING
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Occlusion Culling");
		CullingShader.Activate();

		for (ProgramBuffer& ProgramVariant : ProgramFamily->ProgramVariants)
		{
			const int DrawCount = ProgramVariant.Voxels.size();
			ProgramVariant.VoxelsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			ProgramVariant.DrawsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 3);

			glDispatchCompute(DrawCount, 1, 1);
		}

		glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
		glPopDebugGroup();
#endif

		Shader->Activate();

		for (ProgramBuffer& ProgramVariant : ProgramFamily->ProgramVariants)
		{
			const int DrawCount = ProgramVariant.Voxels.size();
			ProgramVariant.ParamsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 0);
			ProgramVariant.VoxelsBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			ProgramVariant.DrawsBuffer.Bind(GL_DRAW_INDIRECT_BUFFER);

			if (DebugView)
			{
				int UploadValue;
				if (ShowLeafCount)
				{
					UploadValue = ProgramFamily->LeafCount;
				}
				else
				{
					UploadValue = NextOctreeID;
					NextOctreeID += DrawCount;
				}
				OctreeDebugOptionsUpload BufferData = {
					(GLuint)UploadValue,
					Wireframe,
					0,
					0
				};
				OctreeDebugOptions.Upload((void*)&BufferData, sizeof(BufferData));
				OctreeDebugOptions.Bind(GL_UNIFORM_BUFFER, 3);
			}
			glMultiDrawArraysIndirect(GL_TRIANGLES, 0, DrawCount, 0);
		}
		if (ShowHeatmap)
		{
			ProgramFamily->DepthQuery.Stop();
		}
		glPopDebugGroup();
		EndEvent();
	}
}
