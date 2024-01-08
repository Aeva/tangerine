
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

#include "mesh_generators.h"
#include "errors.h"


bool MeshGenerator::LessVec3::operator()(const glm::vec3& L, const glm::vec3& R) const
{
	if (L.z < R.z)
	{
		return true;
	}
	else if (L.z == R.z)
	{
		if (L.y < R.y)
		{
			return true;
		}
		else if (L.y == R.y)
		{
			return L.x < R.x;
		}
	}

	return false;
}


void MeshGenerator::Accumulate(glm::vec3 Vertex)
{
	auto [Index, Outcome] = Memo.insert({ Vertex, uint32_t(Vertices.size()) });
	if (Outcome)
	{
		Vertices.push_back(glm::vec4(Vertex, 1.0));
	}
	Indices.push_back(Index->second);
}


void MeshGenerator::Accumulate(const TriangleT& Triangle)
{
	for (const glm::vec3& Vertex : Triangle)
	{
		Accumulate(Vertex);
	}
}


void MeshGenerator::Accumulate(const MeshGenerator& Other)
{
	for (const uint32_t Index : Other.Indices)
	{
		Accumulate(Other.Vertices[Index]);
	}
}


void MeshGenerator::WalkTriangles(TriangleThunk Thunk) const
{
	for (uint32_t TriangleIndex = 0; TriangleIndex < Indices.size() / 3; ++TriangleIndex)
	{
		Thunk({
			Vertices[Indices[TriangleIndex * 3 + 0]],
			Vertices[Indices[TriangleIndex * 3 + 1]],
			Vertices[Indices[TriangleIndex * 3 + 2]]});
	}
}
