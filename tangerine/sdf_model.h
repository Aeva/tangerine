
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

#pragma once

#include "events.h"
#include "embedding.h"
#include "sdf_evaluator.h"
#include "sdf_rendering.h"

#if RENDERER_SODAPOP
#include "sodapop.h"
#include <atomic>
#endif


struct SDFModel;
using SDFModelShared = std::shared_ptr<SDFModel>;
using SDFModelWeakRef = std::weak_ptr<SDFModel>;


struct Drawable
{
	virtual void Draw(
		const bool ShowOctree,
		const bool ShowLeafCount,
		const bool ShowHeatmap,
		const bool Wireframe,
		struct ShaderProgram* DebugShader) = 0;

	virtual void Draw(
		glm::vec3 CameraOrigin,
		const int PositionBinding,
		const int ColorBinding,
		SDFModel* Instance) = 0;

	virtual ~Drawable()
	{
	}
};

using DrawableShared = std::shared_ptr<Drawable>;


#if RENDERER_COMPILER
struct VoxelDrawable final : Drawable
{
	std::map<std::string, size_t> ProgramTemplateSourceMap;
	std::vector<ProgramTemplate> ProgramTemplates;

	std::vector<size_t> PendingShaders;
	std::vector<ProgramTemplate*> CompiledTemplates;

	bool HasPendingShaders();
	bool HasCompleteShaders();
	void CompileNextShader();

	void Compile(SDFNodeShared& Evaluator, const float VoxelSize);

	virtual void Draw(
		const bool ShowOctree,
		const bool ShowLeafCount,
		const bool ShowHeatmap,
		const bool Wireframe,
		struct ShaderProgram* DebugShader);

	virtual void Draw(
		glm::vec3 CameraOrigin,
		const int PositionBinding,
		const int ColorBinding,
		SDFModel* Instance)
	{
		// Unused
	};

	virtual ~VoxelDrawable();

private:
	size_t AddProgramTemplate(std::string Source, std::string Pretty, int LeafCount);
	void AddProgramVariant(size_t ShaderIndex, uint32_t SubtreeIndex, const std::vector<float>& Params, const std::vector<AABB>& Voxels);
	ProgramBuffer* PendingVoxels = nullptr;
};

using VoxelDrawableShared = std::shared_ptr<VoxelDrawable>;
#endif // RENDERER_COMPILER


#if RENDERER_SODAPOP
struct SodapopDrawable final : Drawable
{
	SDFNodeShared Evaluator = nullptr;

	Buffer IndexBuffer;
	Buffer PositionBuffer;
	Buffer ColorBuffer;

	std::vector<uint32_t> Indices;
	std::vector<glm::vec4> Positions;
	std::vector<glm::vec4> Normals;
	std::vector<glm::vec4> Colors;

	std::atomic_bool MeshReady;
	bool MeshUploaded = false;

	SodapopDrawable(SDFNodeShared& InEvaluator)
	{
		Evaluator = InEvaluator;
		MeshReady.store(false);
	}

	virtual void Draw(
		const bool ShowOctree,
		const bool ShowLeafCount,
		const bool ShowHeatmap,
		const bool Wireframe,
		struct ShaderProgram* DebugShader);

	virtual void Draw(
		glm::vec3 CameraOrigin,
		const int PositionBinding,
		const int ColorBinding,
		SDFModel* Instance);

	virtual ~SodapopDrawable();
};

using SodapopDrawableShared = std::shared_ptr<SodapopDrawable>;
#endif // RENDERER_SODAPOP


struct SDFModel
{
	SDFNodeShared Evaluator = nullptr;
	DrawableShared Painter = nullptr;

	bool Visible = true;
	TransformMachine Transform;
	Buffer TransformBuffer;

	int MouseListenFlags = 0;

#if RENDERER_SODAPOP
	glm::vec3 LastCameraOrigin = glm::vec3(0.0, 0.0, 0.0);
	std::vector<glm::vec4> Colors;
#endif // RENDERER_SODAPOP

	void Draw(
		const bool ShowOctree,
		const bool ShowLeafCount,
		const bool ShowHeatmap,
		const bool Wireframe,
		struct ShaderProgram* DebugShader);

	void Draw(
		glm::vec3 CameraOrigin,
		const int LocalToWorldBinding,
		const int PositionBinding,
		const int ColorBinding);

	RayHit RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations = 1000, float Epsilon = 0.001);

	static SDFModelShared Create(SDFNodeShared& InEvaluator, const float VoxelSize = 0.25);
	SDFModel(SDFModel&& Other) = delete;
	virtual ~SDFModel();

	virtual void OnMouseEvent(MouseEvent& Event, bool Picked) {};

protected:
	static void RegisterNewModel(SDFModelShared& NewModel);
	SDFModel(SDFNodeShared& InEvaluator, const float VoxelSize);
};


std::vector<SDFModelWeakRef>& GetLiveModels();
void UnloadAllModels();

void GetIncompleteModels(std::vector<SDFModelShared>& Incomplete);
void GetRenderableModels(std::vector<SDFModelShared>& Renderable);

bool DeliverMouseMove(glm::vec3 Origin, glm::vec3 RayDir, int MouseX, int MouseY);
bool DeliverMouseButton(MouseEvent Event);
bool DeliverMouseScroll(glm::vec3 Origin, glm::vec3 RayDir, int ScrollX, int ScrollY);

void ClearTreeEvaluator();
void SetTreeEvaluator(SDFNodeShared& InTreeEvaluator);
