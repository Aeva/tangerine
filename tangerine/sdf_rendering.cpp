
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


struct OctreeDebugOptionsUpload
{
	GLuint OutlinerFlags;
	GLuint Wireframe;
	GLuint Unused1;
	GLuint Unused2;
};


Buffer OctreeDebugOptions("Octree Debug Options Buffer");




VoxelBuffer::VoxelBuffer(glm::vec4 Center, glm::vec4 Extent)
{
	SectionData.Center = Center;
	SectionData.Extent = Extent;

	SectionBuffer.DebugName = "Voxel Buffer";
	SectionBuffer.Upload((void*)&SectionData, sizeof(VoxelUpload));
}


void VoxelBuffer::Bind(GLenum Target, GLuint BindingIndex)
{
	SectionBuffer.Bind(Target, BindingIndex);
}


void VoxelBuffer::Release()
{
	SectionBuffer.Release();
}




ProgramBuffer::ProgramBuffer(uint32_t ShaderIndex, uint32_t SubtreeIndex, size_t ParamCount, const float* InParams)
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
}

void ProgramBuffer::Bind(GLenum Target, GLuint BindingIndex)
{
	ParamsBuffer.Bind(Target, BindingIndex);
}

void ProgramBuffer::Release()
{
	for (VoxelBuffer& Voxel : Voxels)
	{
		Voxel.Release();
	}
	Voxels.clear();
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

		Shader->Activate();

		for (ProgramBuffer& ProgramVariant : ProgramFamily->ProgramVariants)
		{
			ProgramVariant.Bind(GL_SHADER_STORAGE_BUFFER, 0);
			for (VoxelBuffer& Voxel : ProgramVariant.Voxels)
			{
				if (DebugView)
				{
					int UploadValue;
					if (ShowLeafCount)
					{
						UploadValue = ProgramFamily->LeafCount;
					}
					else
					{
						UploadValue = ++NextOctreeID;
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
				Voxel.Bind(GL_UNIFORM_BUFFER, 2);
				glDrawArrays(GL_TRIANGLES, 0, 36);
			}
		}
		if (ShowHeatmap)
		{
			ProgramFamily->DepthQuery.Stop();
		}
		glPopDebugGroup();
		EndEvent();
	}
}
