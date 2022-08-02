
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

#include <map>
#include <unordered_map>
#include "fmt/format.h"

#include "extern.h"
#include "sdf_model.h"
#include "shape_compiler.h"
#include "profiling.h"


using namespace glm;


using ParamsVec = std::vector<float>;
using BoundsVec = std::vector<AABB>;
using ParamsMap = std::map<ParamsVec, BoundsVec>;

struct ShaderInfo
{
	ParamsMap Params;
	std::string Pretty;
	int LeafCount;
	uint32_t StackSize;
};

using VariantsMap = std::unordered_map<std::string, ShaderInfo>;


int MaxIterations = 100;
void OverrideMaxIterations(int MaxIterationsOverride)
{
	if (MaxIterationsOverride > 0)
	{
		MaxIterations = MaxIterationsOverride;
	}
}


bool Interpreted = true;
void UseInterpreter()
{
	Interpreted = true;
}


bool RoundStackSize = false;
void UseRoundedStackSize()
{
	RoundStackSize = true;
}


// Iterate over a voxel grid and generate sources and parameter buffers to populate a new model.
void SDFModel::Compile(const float VoxelSize)
{
	BeginEvent("VoxelFinder");
	SetTreeEvaluator(Evaluator);

	VariantsMap Voxels;
	uint32_t SubtreeIndex = 0;

	{
		BeginEvent("Build Octree");
		SDFOctree* Octree = SDFOctree::Create(Evaluator, VoxelSize);
		EndEvent();

		SDFOctree::CallbackType Thunk = [&](SDFOctree& Leaf)
		{
			std::vector<float> Params;
			std::string Point = "Point";
			std::string GLSL = Leaf.Evaluator->Compile(Interpreted, Params, Point);

			uint32_t StackSize = Leaf.Evaluator->StackSize();
			if (RoundStackSize)
			{
				// Align the stack size to 8 to reduce the number interpreter variants that
				// need to be compiled.  This degrades performance significantly however.
				StackSize = ((StackSize + 7) / 8) * 8;
			}

			std::string Pretty;
			if (Interpreted)
			{
				Leaf.Evaluator->AddTerminus(Params);
				Pretty = fmt::format("[SDF Interpreter {}]", StackSize);
			}
			else
			{
				Pretty = Leaf.Evaluator->Pretty();
			}

			ShaderInfo VariantInfo = { ParamsMap(), Pretty, Leaf.LeafCount, StackSize };
			auto VariantsInsert = Voxels.insert({ GLSL, VariantInfo });
			ParamsMap& Variant = (*(VariantsInsert.first)).second.Params;

			auto ParamsInsert = Variant.insert({ Params, BoundsVec() });
			BoundsVec& Instances = (*(ParamsInsert.first)).second;

			Instances.push_back(Leaf.Bounds);
		};

		BeginEvent("Walk Octree");
		if (Octree)
		{
			Octree->Walk(Thunk);
		}
		EndEvent();

		BeginEvent("Delete Octree");
		delete Octree;
		EndEvent();
	}

	BeginEvent("Emit GLSL");
	for (auto& [Source, VariantInfo] : Voxels)
	{
		std::string BoilerPlate;
		if (Interpreted)
		{
			BoilerPlate = fmt::format(
				"#define MAX_ITERATIONS {}\n"
				"#define INTERPRETED 1\n"
				"#define INTERPRETER_STACK {}\n"
				"#define ClusterDist Interpret\n"
				"layout(std430, binding = 0)\n"
				"restrict readonly buffer SubtreeParameterBlock\n"
				"{{\n"
				"\tuint SubtreeIndex;\n"
				"\tfloat PARAMS[];\n"
				"}};\n\n"
				"MaterialDist Interpret(const vec3 EvalPoint);\n",
				MaxIterations,
				VariantInfo.StackSize);
		}
		else
		{
			BoilerPlate = fmt::format(
				"#define MAX_ITERATIONS {}\n"
				"layout(std430, binding = 0)\n"
				"restrict readonly buffer SubtreeParameterBlock\n"
				"{{\n"
				"\tuint SubtreeIndex;\n"
				"\tfloat PARAMS[];\n"
				"}};\n\n"
				"MaterialDist ClusterDist(vec3 Point)\n"
				"{{\n"
				"\treturn TreeRoot({});\n"
				"}}\n",
				MaxIterations,
				Source);
		}

		size_t ShaderIndex = AddProgramTemplate(BoilerPlate, VariantInfo.Pretty, VariantInfo.LeafCount);
		for (auto& [Params, Instances] : VariantInfo.Params)
		{
			AddProgramVariant(ShaderIndex, SubtreeIndex++, Params, Instances);
		}
	}

	CompiledTemplates.reserve(PendingShaders.size());

	EndEvent();

	EndEvent();
}


size_t SDFModel::AddProgramTemplate(std::string InSource, std::string InPretty, int LeafCount)
{
	std::string& Source = InSource;
	std::string& Pretty = InPretty;
	std::string& DebugName = InSource; // TODO

	auto Found = ProgramTemplateSourceMap.find(Source);

	size_t ShaderIndex;
	if (Found == ProgramTemplateSourceMap.end())
	{
		size_t Index = ProgramTemplates.size();
		ProgramTemplates.emplace_back(DebugName, Pretty, Source, LeafCount);
		ProgramTemplateSourceMap[Source] = Index;
		PendingShaders.push_back(Index);
		ShaderIndex = Index;
	}
	else
	{
		ShaderIndex = Found->second;
	}

	return ShaderIndex;
}


void SDFModel::AddProgramVariant(size_t ShaderIndex, uint32_t SubtreeIndex, const std::vector<float>& Params, const std::vector<AABB>& Voxels)
{
	// TODO: ProgramVariants is currently a vector, but should it be a map...?
	ProgramTemplates[ShaderIndex].ProgramVariants.emplace_back(ShaderIndex, SubtreeIndex, Params.size(), Params.data());
	ProgramBuffer& Program = ProgramTemplates[ShaderIndex].ProgramVariants.back();
	for (const AABB& Bounds : Voxels)
	{
		glm::vec3 Extent = (Bounds.Max - Bounds.Min) * glm::vec3(0.5);
		glm::vec3 Center = Extent + Bounds.Min;
		Program.Voxels.emplace_back(glm::vec4(Center, 0.0), glm::vec4(Extent, 0.0));
	}
}


void CompileEvaluator(SDFNode* Evaluator, const float VoxelSize)
{
	new SDFModel(Evaluator, VoxelSize);
}


extern "C" TANGERINE_API void VoxelCompiler(void* Handle, const float VoxelSize)
{
	CompileEvaluator((SDFNode*)Handle, VoxelSize);
}
