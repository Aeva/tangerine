
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


MeshGenerator MeshGenerator::ConvexBisect(glm::vec3 Pivot, glm::vec3 Normal) const
{
	float PlaneDist = glm::dot(Pivot, Normal);
	MeshGenerator Patch;

	std::vector<std::array<glm::vec3, 2>> Edges;
	glm::vec4 Center(0.f);

	auto ClipEdge = [&](uint32_t VertexA, uint32_t VertexB, int& Accepted, std::array<glm::vec3, 2>& BisectionEdge)
	{
		const glm::vec3& SegStart = Vertices[VertexA];
		const glm::vec3& SegStop = Vertices[VertexB];

		const float DistA = glm::dot(SegStart - Pivot, Normal);
		const float DistB = glm::dot(SegStop - Pivot, Normal);

		if (Accepted < 2)
		{
			if ((glm::sign(DistA) != glm::sign(DistB)) ||
				(glm::abs(DistA) < 0.001 && glm::abs(DistB) > 0.001) ||
				(glm::abs(DistB) < 0.001 && glm::abs(DistA) > 0.001))
			{
				glm::vec3 Ray = SegStop - SegStart;
				glm::vec3 RayDir = glm::normalize(Ray);
				if (glm::dot(Normal, RayDir) != 0.0f)
				{
					glm::vec3 Crossing = SegStart + RayDir * (PlaneDist - glm::dot(Normal, SegStart)) / glm::dot(Normal, RayDir);
					Center += glm::vec4(Crossing, 1.f);
					BisectionEdge[Accepted] = Crossing;
					++Accepted;
				}
			}
		}
	};

	for (uint32_t BaseIndex = 0; BaseIndex < Indices.size(); BaseIndex += 3)
	{
		int Accepted = 0;
		std::array<glm::vec3, 2> BisectionEdge;
		uint32_t A = Indices[BaseIndex + 0];
		uint32_t B = Indices[BaseIndex + 1];
		uint32_t C = Indices[BaseIndex + 2];
		ClipEdge(A, B, Accepted, BisectionEdge);
		ClipEdge(B, C, Accepted, BisectionEdge);
		ClipEdge(C, A, Accepted, BisectionEdge);
		if (Accepted == 2)
		{
			Edges.push_back(BisectionEdge);
		}
	}

	if (Edges.size() > 1)
	{
		Center /= Center.w;

		for (std::array<glm::vec3, 2>& Edge : Edges)
		{
			glm::vec3 A = normalize(Edge[0] - Center.xyz());
			glm::vec3 B = normalize(Edge[1] - Center.xyz());

			if (glm::dot(glm::cross(A, B), Normal) < 0)
			{
				Patch.Accumulate(Edge[0]);
				Patch.Accumulate(Center);
				Patch.Accumulate(Edge[1]);
			}
			else
			{
				Patch.Accumulate(Edge[1]);
				Patch.Accumulate(Center);
				Patch.Accumulate(Edge[0]);
			}
		}
	}

	return Patch;
}


RhombicDodecahedronGenerator::RhombicDodecahedronGenerator(const float Radius)
{
	const float A = Radius;
	const float B = Radius * (glm::sqrt(2.f) / 2.f);
	const float C = Radius * (glm::sqrt(2.f));
	const float Z = 0.f;

	// -X -Y -Z
	Rhombus(
		glm::vec3( Z,  Z, -C),
		glm::vec3(-A, -A, -Z),
		glm::vec3( Z, -A, -B),
		glm::vec3(-A,  Z, -B));

	// +X -Y -Z
	Rhombus(
		glm::vec3( Z,  Z, -C),
		glm::vec3(+A, -A, -Z),
		glm::vec3(+A,  Z, -B),
		glm::vec3( Z, -A, -B));

	// -X +Y -Z
	Rhombus(
		glm::vec3( Z,  Z, -C),
		glm::vec3(-A, +A,  Z),
		glm::vec3(-A,  Z, -B),
		glm::vec3( Z, +A, -B));

	// +X +Y -Z
	Rhombus(
		glm::vec3( Z,  Z, -C),
		glm::vec3(+A, +A, -Z),
		glm::vec3( Z, +A, -B),
		glm::vec3(+A,  Z, -B));

	// -Y
	Rhombus(
		glm::vec3(-A, -A,  Z),
		glm::vec3(+A, -A,  Z),
		glm::vec3(+Z, -A, -B),
		glm::vec3(+Z, -A, +B));

	// -X
	Rhombus(
		glm::vec3(-A, +A,  Z),
		glm::vec3(-A, -A,  Z),
		glm::vec3(-A,  Z, -B),
		glm::vec3(-A,  Z, +B));

	// +X
	Rhombus(
		glm::vec3(+A, -A,  Z),
		glm::vec3(+A, +A,  Z),
		glm::vec3(+A, +Z, -B),
		glm::vec3(+A, +Z, +B));

	// +Y
	Rhombus(
		glm::vec3(+A, +A, Z),
		glm::vec3(-A, +A, Z),
		glm::vec3(Z, +A, -B),
		glm::vec3(Z, +A, +B));

	// -X -Y +Z
	Rhombus(
		glm::vec3( Z,  Z, +C),
		glm::vec3(-A, -A,  Z),
		glm::vec3(-A,  Z, +B),
		glm::vec3( Z, -A, +B));

	// +X -Y +Z
	Rhombus(
		glm::vec3( Z,  Z, +C),
		glm::vec3(+A, -A,  Z),
		glm::vec3( Z, -A, +B),
		glm::vec3(+A,  Z, +B));

	// -X +Y +Z
	Rhombus(
		glm::vec3( Z,  Z, +C),
		glm::vec3(-A, +A,  Z),
		glm::vec3( Z, +A, +B),
		glm::vec3(-A,  Z, +B));

	// +X +Y +Z
	Rhombus(
		glm::vec3( Z,  Z, +C),
		glm::vec3(+A, +A,  Z),
		glm::vec3(+A,  Z, +B),
		glm::vec3( Z, +A, +B));
}


void RhombicDodecahedronGenerator::Rhombus(glm::vec3 AcuteLeft, glm::vec3 AcuteRight, glm::vec3 ObtuseBottom, glm::vec3 ObtuseTop)
{
	Accumulate(AcuteLeft);
	Accumulate(ObtuseBottom);
	Accumulate(ObtuseTop);
	Accumulate(ObtuseTop);
	Accumulate(ObtuseBottom);
	Accumulate(AcuteRight);
}