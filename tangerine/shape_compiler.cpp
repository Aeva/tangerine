
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


// Iterate over a voxel grid and return bounded subtrees.
extern "C" TANGERINE_API void VoxelCompiler(void* Handle, const float VoxelSize)
{
	BeginEvent("VoxelFinder");
	SDFNode* Evaluator = (SDFNode*)Handle;
	AABB Limits = Evaluator->Bounds();
	SetTreeEvaluator(Evaluator, Limits);

	VariantsMap Voxels;

	{
		BeginEvent("Build Octree");
		SDFOctree* Octree = SDFOctree::Create(Evaluator, VoxelSize);
		EndEvent();

		SDFOctree::CallbackType Thunk = [&](SDFOctree& Leaf)
		{
			std::vector<float> Params;
			std::string Point = "Point";
			std::string GLSL = Leaf.Evaluator->Compile(Params, Point);
			std::string Pretty = Leaf.Evaluator->Pretty();

			ShaderInfo VariantInfo = { ParamsMap(), Pretty, Leaf.LeafCount };
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
	int SubtreeIndex = 0;
	for (auto& [Source, VariantInfo] : Voxels)
	{
		std::string BoilerPlate = fmt::format(
			"#define MAX_ITERATIONS {}\n"
			"layout(std430, binding = 0)\n"
			"restrict readonly buffer SubtreeParameterBlock\n"
			"{{\n"
			"\tfloat PARAMS[];\n"
			"}};\n\n"
			"const uint SubtreeIndex = {};\n\n"
			"MaterialDist ClusterDist(vec3 Point)\n"
			"{{\n"
			"\treturn TreeRoot({});\n"
			"}}\n",
			MaxIterations,
			SubtreeIndex++,
			Source);

		size_t ShaderIndex = EmitShader(BoilerPlate, VariantInfo.Pretty, VariantInfo.LeafCount);
		for (auto& [Params, Instances] : VariantInfo.Params)
		{
			EmitParameters(ShaderIndex, Params);
			for (const AABB& Bounds : Instances)
			{
				EmitVoxel(Bounds);
			}
		}
	}
	EndEvent();

	EndEvent();
}
