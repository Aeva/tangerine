
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
#include "transform.h"
#include "material.h"

#include "sodapop.h"
#include <atomic>
#include <mutex>

#include <string>
#include <chrono>
#include <map>

using Clock = std::chrono::high_resolution_clock;

struct SDFModel;
using SDFModelShared = std::shared_ptr<SDFModel>;
using SDFModelWeakRef = std::weak_ptr<SDFModel>;


enum class VisibilityStates : int
{
	Invisible = 0,
	Imminent,
	Visible
};


struct Drawable
{
	std::string Name = "unknown";

	SDFNodeShared Evaluator = nullptr;

	Buffer IndexBuffer;
	Buffer PositionBuffer;

	std::vector<uint32_t> Indices;
	std::vector<glm::vec4> Positions;
	std::vector<glm::vec4> Normals;
	std::vector<glm::vec4> Colors;

	struct MeshingScratch* Scratch = nullptr;
	std::atomic_bool MeshReady = false;
	bool MeshUploaded = false;

	MeshingAlgorithms MeshingAlgorithm = MeshingAlgorithms::NaiveSurfaceNets;

	// These are populated during the meshing process, but may be safely used after the mesh is ready.
	SDFOctreeShared EvaluatorOctree = nullptr;
	std::vector<MaterialVertexGroup> MaterialSlots;
	std::mutex MaterialSlotsCS;
	std::map<MaterialShared, size_t> SlotLookup;

	Clock::time_point MeshingStart;
	Clock::time_point MeshingComplete;
	std::chrono::duration<double, std::milli> ReadyDelay;

	Drawable(const std::string& InName, SDFNodeShared& InEvaluator);

	void DrawGL4(
		glm::vec3 CameraOrigin,
		SDFModel* Instance);

	void DrawES2(
		glm::vec3 CameraOrigin,
		const int PositionBinding,
		const int ColorBinding,
		SDFModel* Instance);

	~Drawable();
};

using DrawableShared = std::shared_ptr<Drawable>;
using DrawableWeakRef = std::weak_ptr<Drawable>;


struct InstanceColoringGroup
{
	MaterialVertexGroup* VertexGroup;
	size_t IndexStart; // Offset within vertex group
	size_t IndexRange; // Number of indices to process
	std::vector<glm::vec4> Colors;
	std::mutex ColorCS;

	InstanceColoringGroup(MaterialVertexGroup* InVertexGroup, size_t InIndexStart, size_t InIndexRange)
		: VertexGroup(InVertexGroup)
		, IndexStart(InIndexStart)
		, IndexRange(InIndexRange)
	{
	}
};

using InstanceColoringGroupUnique = std::unique_ptr<InstanceColoringGroup>;


struct SDFModel
{
	SDFNodeShared Evaluator = nullptr;
	DrawableShared Painter = nullptr;

	std::atomic<VisibilityStates> Visibility = VisibilityStates::Visible;
	Transform LocalToWorld;
	Buffer TransformBuffer;
	std::atomic<glm::mat4> AtomicWorldToLocal;

	int MouseListenFlags = 0;

	std::string Name = "";

	std::atomic_bool Dirty = false;
	glm::vec3 CameraOrigin = glm::vec3(0.0, 0.0, 0.0);
	std::vector<glm::vec4> Colors;
	Buffer ColorBuffer;

	std::vector<InstanceColoringGroupUnique> ColoringGroups;

	void UpdateColors();

	void DrawGL4(glm::vec3 CameraOrigin);

	void DrawES2(
		glm::vec3 CameraOrigin,
		const int LocalToWorldBinding,
		const int PositionBinding,
		const int ColorBinding);

	RayHit RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations = 1000, float Epsilon = 0.001);

	static SDFModelShared Create(SDFNodeShared& InEvaluator, const std::string& InName, const float VoxelSize = 0.25, const float MeshingDensityPush = 0.0);
	SDFModel(SDFModel&& Other) = delete;
	virtual ~SDFModel();

	virtual void OnMouseEvent(MouseEvent& Event, bool Picked) {};

protected:
	static void RegisterNewModel(SDFModelShared& NewModel);
	SDFModel(SDFNodeShared& InEvaluator, const std::string& InName, const float VoxelSize, const float MeshingDensityPush);
};


std::vector<SDFModelWeakRef>& GetLiveModels();
std::vector<std::pair<size_t, DrawableWeakRef>>& GetDrawableCache();
void UnloadAllModels();
void MeshReady(DrawableShared Painter);

void GetIncompleteModels(std::vector<SDFModelWeakRef>& Incomplete);
void GetRenderableModels(std::vector<SDFModelWeakRef>& Renderable);

bool DeliverMouseMove(glm::vec3 Origin, glm::vec3 RayDir, int MouseX, int MouseY);
bool DeliverMouseButton(MouseEvent Event);
bool DeliverMouseScroll(glm::vec3 Origin, glm::vec3 RayDir, int ScrollX, int ScrollY);

void ClearTreeEvaluator();
void SetTreeEvaluator(SDFNodeShared& InTreeEvaluator);
