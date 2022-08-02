
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

#include "sdf_model.h"
#include "profiling.h"


std::vector<SDFModel*> LiveModels;


std::vector<SDFModel*>& GetLiveModels()
{
	return LiveModels;
}


void UnloadAllModels()
{
	for (SDFModel* Model : LiveModels)
	{
		Model->Release();
	}
	LiveModels.clear();
}


void GetIncompleteModels(std::vector<SDFModel*>& Incomplete)
{
	Incomplete.clear();
	Incomplete.reserve(LiveModels.size());

	for (SDFModel* Model : LiveModels)
	{
		if (Model->HasPendingShaders())
		{
			Incomplete.push_back(Model);
		}
	}
}


void GetRenderableModels(std::vector<SDFModel*>& Renderable)
{
	Renderable.clear();
	Renderable.reserve(LiveModels.size());

	for (SDFModel* Model : LiveModels)
	{
		if (Model->HasCompleteShaders())
		{
			Renderable.push_back(Model);
		}
	}
}


bool SDFModel::HasPendingShaders()
{
	return PendingShaders.size() > 0;
}


bool SDFModel::HasCompleteShaders()
{
	return CompiledTemplates.size() > 0;
}


void SDFModel::CompileNextShader()
{
	BeginEvent("Compile Shader");

	size_t TemplateIndex = PendingShaders.back();
	PendingShaders.pop_back();

	ProgramTemplate& ProgramFamily = ProgramTemplates[TemplateIndex];
	ProgramFamily.StartCompile();
	if (ProgramFamily.ProgramVariants.size() > 0)
	{
		CompiledTemplates.push_back(&ProgramFamily);
	}

	EndEvent();
}


SDFModel::SDFModel(SDFNode* InEvaluator, const float VoxelSize)
{
	InEvaluator->Hold();
	Evaluator = InEvaluator;
	Compile(VoxelSize);

	LocalToWorld = glm::identity<glm::mat4>();
	TransformBuffer.DebugName = "Instance Transforms Buffer";

	this->Hold();
	LiveModels.push_back(this);
}


SDFModel::SDFModel(SDFModel&& Other)
{
	Evaluator = Other.Evaluator;
	Other.Evaluator = nullptr;

	ProgramTemplateSourceMap = std::move(Other.ProgramTemplateSourceMap);
	ProgramTemplates = std::move(Other.ProgramTemplates);
	PendingShaders = std::move(Other.PendingShaders);
}


SDFModel::~SDFModel()
{
	Assert(RefCount == 0);

	// If the Evaluator is nullptr, then this SDFModel was moved into a new instance, and less stuff needs to be deleted.
	if (Evaluator)
	{
		Evaluator->Release();

		for (ProgramTemplate& ProgramFamily : ProgramTemplates)
		{
			ProgramFamily.Release();
		}
	}

	ProgramTemplates.clear();
	ProgramTemplateSourceMap.clear();
	PendingShaders.clear();
	CompiledTemplates.clear();
}
