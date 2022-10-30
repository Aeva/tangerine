
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

#pragma once

#include "sdf_evaluator.h"
#include "sdf_rendering.h"

#define MOUSE_ENTER  1 << 0
#define MOUSE_EXIT   1 << 1
#define MOUSE_MOVE   1 << 2
#define MOUSE_DOWN   1 << 3
#define MOUSE_UP     1 << 4
#define MOUSE_SCROLL 1 << 5

struct SDFModel
{
	SDFNode* Evaluator = nullptr;

	bool Visible = true;
	TransformMachine Transform;
	Buffer TransformBuffer;

	std::map<std::string, size_t> ProgramTemplateSourceMap;
	std::vector<ProgramTemplate> ProgramTemplates;

	std::vector<size_t> PendingShaders;
	std::vector<ProgramTemplate*> CompiledTemplates;

	int MouseListenFlags = 0;

	bool HasPendingShaders();
	bool HasCompleteShaders();
	void CompileNextShader();

	void Draw(
		const bool ShowOctree,
		const bool ShowLeafCount,
		const bool ShowHeatmap,
		const bool Wireframe,
		struct ShaderProgram* DebugShader);

	RayHit RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations = 1000, float Epsilon = 0.001);

	SDFModel(SDFNode* InEvaluator, const float VoxelSize);
	SDFModel(SDFModel&& Other);
	~SDFModel();

	void Hold()
	{
		++RefCount;
	}

	void Release()
	{
		Assert(RefCount > 0);
		--RefCount;
		if (RefCount == 0)
		{
			TransformBuffer.Release();
			delete this;
		}
	}

private:
	void Compile(const float VoxelSize);
	size_t AddProgramTemplate(std::string Source, std::string Pretty, int LeafCount);
	void AddProgramVariant(size_t ShaderIndex, uint32_t SubtreeIndex, const std::vector<float>& Params, const std::vector<AABB>& Voxels);
	ProgramBuffer* PendingVoxels = nullptr;

protected:
	size_t RefCount = 0;
};


std::vector<SDFModel*>& GetLiveModels();
void UnloadAllModels();

void GetIncompleteModels(std::vector<SDFModel*>& Incomplete);
void GetRenderableModels(std::vector<SDFModel*>& Renderable);

bool DeliverMouseMove(glm::vec3 Origin, glm::vec3 RayDir, int MouseX, int MouseY);
bool DeliverMouseButton(glm::vec3 Origin, glm::vec3 RayDir, int MouseX, int MouseY, bool Press, bool Release, int Button, int Clicks);
bool DeliverMouseScroll(glm::vec3 Origin, glm::vec3 RayDir, int ScrollX, int ScrollY);

void ClearTreeEvaluator();
void SetTreeEvaluator(SDFNode* InTreeEvaluator);
