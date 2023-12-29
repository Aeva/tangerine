
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

#include "glm_common.h"
#include <functional>
#include <array>
#include <vector>
#include <map>


struct MeshGenerator
{
	using TriangleT = std::array<glm::vec3, 3>;
	using TriangleThunk = std::function<void(TriangleT)>;

	struct LessVec3
	{
		bool operator()(const glm::vec3& L, const glm::vec3& R) const;
	};

	std::vector<glm::vec4> Vertices;
	std::vector<uint32_t> Indices;
	std::map<glm::vec3, uint32_t, LessVec3> Memo;

	void Accumulate(glm::vec3 Vertex);

	void Accumulate(const TriangleT& Vertex);

	void Accumulate(const MeshGenerator& Generator);

	void WalkTriangles(TriangleThunk Thunk) const;

	MeshGenerator ConvexBisect(glm::vec3 Pivot, glm::vec3 Normal) const;
};


struct RhombicDodecahedronGenerator : public MeshGenerator
{
	RhombicDodecahedronGenerator(const float Radius = 1.f);

	void Rhombus(glm::vec3 AcuteLeft, glm::vec3 AcuteRight, glm::vec3 ObtuseBottom, glm::vec3 ObtuseTop);

	static const RhombicDodecahedronGenerator& GetUnitHull()
	{
		static RhombicDodecahedronGenerator UnitHull;
		return UnitHull;
	}
};
