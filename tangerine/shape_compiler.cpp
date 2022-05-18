
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


int MaxIterations = 50;
void OverrideMaxIterations(int MaxIterationsOverride)
{
	if (MaxIterationsOverride > 1)
	{
		MaxIterations = MaxIterationsOverride;
	}
}


bool Interpreted = false;
void UseInterpreter()
{
	Interpreted = true;
}


bool RoundStackSize = false;
void UseRoundedStackSize()
{
	RoundStackSize = true;
}


// Iterate over a voxel grid and return bounded subtrees.
extern "C" TANGERINE_API void VoxelCompiler(void* Handle, const float VoxelSize)
{
	BeginEvent("VoxelFinder");
	SDFNode* Evaluator = (SDFNode*)Handle;
	AABB Limits = Evaluator->Bounds();
	SetTreeEvaluator(Evaluator, Limits);

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
			uint32_t StackSize = 1;
			std::string GLSL = Leaf.Evaluator->Compile(Interpreted, Params, StackSize, Point, 1);
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

		size_t ShaderIndex = EmitShader(BoilerPlate, VariantInfo.Pretty, VariantInfo.LeafCount);
		for (auto& [Params, Instances] : VariantInfo.Params)
		{
			EmitParameters(ShaderIndex, SubtreeIndex++, Params);
			for (const AABB& Bounds : Instances)
			{
				EmitVoxel(Bounds);
			}
		}
	}
	EndEvent();

	EndEvent();
}
