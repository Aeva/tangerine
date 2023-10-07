
// Copyright 2023 Aeva Palecek
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
#include "perf.h"
#include <iostream>


struct OctreeDebugOptionsUpload
{
	GLuint OutlinerFlags;
	GLuint Wireframe;
	GLuint Unused1;
	GLuint Unused2;
};


struct TransformUpload
{
	glm::mat4 LocalToWorld;
	glm::mat4 WorldToLocal;
};


Buffer OctreeDebugOptions("Octree Debug Options Buffer");


#if RENDERER_COMPILER
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
		{ {GL_VERTEX_SHADER, ShaderSource("cluster_draw.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, GeneratedShader("math.glsl", DistSource, "cluster_draw.fs.glsl")} },
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


void VoxelDrawable::Draw(
	const bool ShowOctree,
	const bool ShowLeafCount,
	const bool ShowHeatmap,
	const bool Wireframe,
	ShaderProgram* DebugShader)
{
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

	int NextOctreeID = 0;

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
#endif // RENDERER_COMPILER


#if RENDERER_SODAPOP
void SodapopDrawable::Draw(
	glm::vec3 CameraOrigin,
	SDFModel* Instance)
{
	if (!glm::all(glm::equal(Instance->CameraOrigin, CameraOrigin)))
	{
		Instance->CameraOrigin = CameraOrigin;
		Instance->Dirty.store(true);
	}

	if (!MeshReady.load())
	{
		return;
	}

	Instance->Drawing.store(true);
	Instance->SodapopCS.lock();

	if (!MeshUploaded)
	{
		IndexBuffer.Upload(Indices.data(), Indices.size() * sizeof(uint32_t));
		PositionBuffer.Upload(Positions.data(), Positions.size() * sizeof(glm::vec4));

		MeshUploaded = true;
	}
	{
		if (Instance->Colors.size() > 0)
		{
			Instance->ColorBuffer.Upload(Instance->Colors.data(), Instance->Colors.size() * sizeof(glm::vec4));

			IndexBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 2);
			PositionBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 3);
			Instance->ColorBuffer.Bind(GL_SHADER_STORAGE_BUFFER, 4);

			glDrawArrays(GL_TRIANGLES, 0, Indices.size());
		}
	}

	Instance->Drawing.store(false);
	Instance->SodapopCS.unlock();
}


void SodapopDrawable::Draw(
	glm::vec3 CameraOrigin,
	const int PositionBinding,
	const int ColorBinding,
	SDFModel* Instance)
{
	if (!glm::all(glm::equal(Instance->CameraOrigin, CameraOrigin)))
	{
		Instance->CameraOrigin = CameraOrigin;
		Instance->Dirty.store(true);
	}

	if (!MeshReady.load())
	{
		return;
	}

	Instance->Drawing.store(true);
	Instance->SodapopCS.lock();

	if (!MeshUploaded)
	{
		IndexBuffer.Upload(GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, Indices.data(), Indices.size() * sizeof(uint32_t));
		PositionBuffer.Upload(GL_ARRAY_BUFFER, GL_STATIC_DRAW, Positions.data(), Positions.size() * sizeof(glm::vec4));

		MeshUploaded = true;
	}
	{
		if (Instance->Colors.size() > 0)
		{
			IndexBuffer.Bind(GL_ELEMENT_ARRAY_BUFFER);

			PositionBuffer.Bind(GL_ARRAY_BUFFER);
			glVertexAttribPointer(PositionBinding, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

			Instance->ColorBuffer.Upload(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW, Instance->Colors.data(), Instance->Colors.size() * sizeof(glm::vec4));
			Instance->ColorBuffer.Bind(GL_ARRAY_BUFFER);
			glVertexAttribPointer(ColorBinding, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

			glDrawElements(GL_TRIANGLES, Indices.size(), GL_UNSIGNED_INT, nullptr);
		}
	}

	Instance->Drawing.store(false);
	Instance->SodapopCS.unlock();
}
#endif //RENDERER_SODAPOP


void SDFModel::Draw(
	const bool ShowOctree,
	const bool ShowLeafCount,
	const bool ShowHeatmap,
	const bool Wireframe,
	ShaderProgram* DebugShader)
{
	if (!Visible || !Painter)
	{
		return;
	}

	Transform.Fold();
	TransformUpload TransformData = {
		Transform.LastFold,
		Transform.LastFoldInverse
	};
	TransformBuffer.Upload((void*)&TransformData, sizeof(TransformUpload));
	TransformBuffer.Bind(GL_UNIFORM_BUFFER, 1);

	Painter->Draw(ShowOctree, ShowLeafCount, ShowHeatmap, Wireframe, DebugShader);
}


void SDFModel::Draw(
	glm::vec3 CameraOrigin)
{
	if (!Visible || !Painter)
	{
		return;
	}

	Transform.Fold();
	TransformUpload TransformData = {
		Transform.LastFold,
		Transform.LastFoldInverse
	};
	TransformBuffer.Upload((void*)&TransformData, sizeof(TransformUpload));
	TransformBuffer.Bind(GL_UNIFORM_BUFFER, 1);

	Painter->Draw(CameraOrigin, this);
}


void SDFModel::Draw(
	glm::vec3 CameraOrigin,
	const int LocalToWorldBinding,
	const int PositionBinding,
	const int ColorBinding)
{
	if (!Visible || !Painter)
	{
		return;
	}

	Transform.Fold();
	glUniformMatrix4fv(LocalToWorldBinding, 1, false, (const GLfloat*)(&Transform.LastFold));

	Painter->Draw(CameraOrigin, PositionBinding, ColorBinding, this);
}
