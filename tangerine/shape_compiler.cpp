
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
#include <format>

#include "shape_compiler.h"
#include "profiling.h"


using namespace glm;


// Iterate over a voxel grid and return bounded subtrees.
extern "C" TANGERINE_API void VoxelCompiler(void* Handle, const float VoxelSize)
{
	BeginEvent("VoxelFinder");
	SDFNode* Evaluator = (SDFNode*)Handle;
	AABB Limits = Evaluator->Bounds();
	SetTreeEvaluator(Evaluator, Limits);

	AABB Volume;
	{
		vec3 Extent = Limits.Max - Limits.Min;
		vec3 VoxelCount = ceil(Extent / VoxelSize);
		vec3 Padding = (VoxelSize * VoxelCount - Extent) * vec3(0.5);
		Volume.Min = Limits.Min - Padding;
		Volume.Max = Limits.Max + Padding;
	}

	const float HalfSize = VoxelSize * 0.5;
	const float Radius = length(vec3(HalfSize));

	const vec3 Start = Volume.Min + vec3(HalfSize);
	const vec3 Stop = Volume.Max;
	const vec3 Step = vec3(VoxelSize);

	using ParamsVec = std::vector<float>;
	using BoundsVec = std::vector<AABB>;
	using ParamsMap = std::map<ParamsVec, BoundsVec>;
	using VariantsMap = std::unordered_map<std::string, ParamsMap>;

	VariantsMap Voxels;

	for (float z = Start.z; z < Stop.z; z += Step.z)
	{
		for (float y = Start.y; y < Stop.y; y += Step.y)
		{
			for (float x = Start.x; x < Stop.x; x += Step.x)
			{
				const vec3 Cursor = vec3(x, y, z);
				BeginEvent("Clip");
				SDFNode* Clipped = Evaluator->Clip(Cursor, Radius);
				EndEvent();
				if (Clipped)
				{
					if (abs(Clipped->Eval(Cursor)) <= Radius)
					{
						std::vector<float> Params;
						std::string Point = "Point";
						std::string GLSL = Clipped->Compile(Params, Point);

						auto VariantsInsert = Voxels.insert({ GLSL, ParamsMap() });
						ParamsMap& Variant = (*(VariantsInsert.first)).second;

						auto ParamsInsert = Variant.insert({ Params, BoundsVec() });
						BoundsVec& Instances = (*(ParamsInsert.first)).second;

						AABB Bounds = { Cursor - vec3(HalfSize), Cursor + vec3(HalfSize) };
						Instances.push_back(Bounds);
					}

					delete Clipped;
				}
			}
		}
	}

	BeginEvent("Emit GLSL");
	int SubtreeIndex = 0;
	for (auto& [Source, Variant] : Voxels)
	{
		std::string Boilerplated = std::format(
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
			SubtreeIndex++,
			Source);

		size_t ShaderIndex = EmitShader(Boilerplated);
		for (auto& [Params, Instances] : Variant)
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
