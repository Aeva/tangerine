
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

#include <functional>
#include <fstream>
#include <vector>
#include <map>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <fmt/format.h>
#include <surface_nets.h>
#include "threadpool.h"
#include "extern.h"
#include "export.h"


using TreeHandle = void*;
using namespace glm;
using namespace std::placeholders;


struct  Vec3Less
{
	bool operator() (const vec3& LHS, const vec3& RHS) const
	{
		return LHS.x < RHS.x || (LHS.x == RHS.x && LHS.y < RHS.y) || (LHS.x == RHS.x && LHS.y == RHS.y && LHS.z < RHS.z);
	}
};


std::atomic_bool ExportActive;
std::atomic_int ExportState(0);
std::atomic_int VoxelCount;
std::atomic_int GenerationProgress;
std::atomic_int VertexCount;
std::atomic_int RefinementProgress;
std::atomic_int SecondaryCount;
std::atomic_int SecondaryProgress;
std::atomic_int WriteCount;
std::atomic_int WriteProgress;


void WriteSTL(SDFOctree* Octree, std::string Path, std::vector<vec3> Vertices, std::vector<ivec3> Triangles, float Scale)
{
	std::ofstream OutFile;
	OutFile.open(Path, std::ios::out | std::ios::binary);

	// Write 80 bytes for the header.
	for (int i = 0; i < 80; ++i)
	{
		OutFile << '\0';
	}

	SecondaryCount.store(Triangles.size());
	std::vector<glm::vec3> Normals;
	Normals.reserve(Triangles.size());
	for (ivec3& Triangle : Triangles)
	{
		if (ExportState.load() != 3 || !ExportActive.load())
		{
			break;
		}
		SecondaryProgress.fetch_add(1);
		vec3 Center = (Vertices[Triangle.x] + Vertices[Triangle.y] + Vertices[Triangle.z]) / vec3(3.0);
		vec3 Normal = Octree->Gradient(Center);
		Normals.push_back(Normal);
	}

	for (glm::vec3& Vertex : Vertices)
	{
		Vertex *= Scale;
	}

	WriteCount.store(Triangles.size());
	uint32_t TriangleCount = Triangles.size();
	OutFile.write(reinterpret_cast<char*>(&TriangleCount), 4);
	for (int t = 0; t < Triangles.size(); ++t)
	{
		if (ExportState.load() != 3 || !ExportActive.load())
		{
			break;
		}
		WriteProgress.fetch_add(1);
		ivec3& Triangle = Triangles[t];
		vec3& Normal = Normals[t];
		{
			OutFile.write(reinterpret_cast<char*>(&Normal), 12);

			OutFile.write(reinterpret_cast<char*>(&Vertices[Triangle.x]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Triangle.y]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Triangle.z]), 12);

			uint16_t Attributes = 0;
			OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
		}
	}

	// Align to 4 bytes for good luck.
	size_t Written = 84 + 100 * Triangles.size();
	for (int i = 0; i < Written % 4; ++i)
	{
		OutFile << '\0';
	}

	OutFile.close();
}


std::string GetEndianName()
{
	int Num = 1;
	int8_t* Head = reinterpret_cast<int8_t*>(&Num);
	bool IsLittleEndian = *Head == Num;
	if (IsLittleEndian)
	{
		return std::string("little");
	}
	else
	{
		return std::string("big");
	}
}


std::string PlyHeader(size_t VertexCount, size_t TriangleCount, bool ExportColor)
{
	static const std::string EndianName = GetEndianName();

	std::string ColorPart = "";
	if (ExportColor)
	{
		ColorPart = \
			"property uchar red\n"
			"property uchar green\n"
			"property uchar blue\n";
	}

	std::string TrianglePart = "";
	if (TriangleCount > 0)
	{
		TrianglePart = fmt::format(
			"element face {}\n"
			"property list uchar uint vertex_indices\n",
			TriangleCount);
	}

	return fmt::format(
		"ply\n"
		"format binary_{}_endian 1.0\n"
		"comment Created by Tangerine\n"
		"element vertex {}\n"
		"property float x\n"
		"property float y\n"
		"property float z\n"
		"property float nx\n"
		"property float ny\n"
		"property float nz\n"
		"{}"
		"{}"
		"end_header\n",
		EndianName,
		VertexCount,
		ColorPart,
		TrianglePart);
}


void WritePLY(SDFOctree* Octree, std::string Path, std::vector<vec3> Vertices, std::vector<ivec3> Triangles, float Scale)
{
	const bool ExportColor = Octree->Evaluator->HasPaint();
	std::string Header;
	{
		size_t VertexCount = Vertices.size();
		size_t TriangleCount = Triangles.size();
		Header = PlyHeader(VertexCount, TriangleCount, ExportColor);
	}

	// Populate vertex attributes.
	SecondaryCount.store(Vertices.size());
	std::vector<glm::vec3> Normals;
	std::vector<uint8_t> Colors;
	Normals.reserve(Vertices.size());
	if (ExportColor)
	{
		Colors.reserve(Vertices.size() * 3);
	}

	for (int v = 0; v < Vertices.size(); ++v)
	{
		SecondaryProgress.fetch_add(1);
		Normals.push_back(Octree->Gradient(Vertices[v]));
		if (ExportColor)
		{
			vec3 Color = Octree->Sample(Vertices[v]);
			Colors.push_back(0xFF * Color.r);
			Colors.push_back(0xFF * Color.g);
			Colors.push_back(0xFF * Color.b);
		}
		Vertices[v] *= Scale;
	}

	// Write vertex data.
	WriteCount.store(Vertices.size() + Triangles.size());
	std::ofstream OutFile;
	OutFile.open(Path, std::ios::out | std::ios::binary);
	OutFile.write(Header.c_str(), Header.size());
	for (int v = 0; v < Vertices.size(); ++v)
	{
		WriteProgress.fetch_add(1);
		OutFile.write(reinterpret_cast<char*>(&Vertices[v]), 12);
		OutFile.write(reinterpret_cast<char*>(&Normals[v]), 12);
		if (ExportColor)
		{
			OutFile.write(reinterpret_cast<char*>(&Colors[v * 3]), 3);
		}
	}

	// Write face data.
	const int8_t FaceVerts = 3;
	for (ivec3& Triangle : Triangles)
	{
		WriteProgress.fetch_add(1);
		{
			OutFile.write(reinterpret_cast<const char*>(&FaceVerts), 1);
			OutFile.write(reinterpret_cast<char*>(&Triangle), 12);
		}
	}

	OutFile.close();
}


void MeshExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, std::string Path, ExportFormat Format, float Scale)
{
	SDFOctree* Octree = SDFOctree::Create(Evaluator, 0.25);

	// The lower bound needs a margin to prevent clipping.
	ModelMin -= Step * vec3(2.0);
	ivec3 Extent = ivec3(ceil((ModelMax - ModelMin) / Step));

	isosurface::regular_grid_t Grid;
	Grid.x = ModelMin.x;
	Grid.y = ModelMin.y;
	Grid.z = ModelMin.z;
	Grid.dx = Step.x;
	Grid.dy = Step.y;
	Grid.dz = Step.z;
	Grid.sx = Extent.x;
	Grid.sy = Extent.y;
	Grid.sz = Extent.z;

	auto Eval = [&](float X, float Y, float Z) -> float
	{
		return Octree->Eval(vec3(X, Y, Z));
	};

	isosurface::mesh ExtractedMesh;
	isosurface::par_surface_nets(Eval, Grid, ExtractedMesh, ExportActive, ExportState, GenerationProgress, VoxelCount);

	ExportState.store(2);

	std::vector<vec3> Vertices;
	Vertices.reserve(ExtractedMesh.vertices_.size());

	std::vector<ivec3> Triangles;
	Triangles.reserve(ExtractedMesh.faces_.size());

	for (const isosurface::point_t& ExtractedVertex : ExtractedMesh.vertices_)
	{
		vec3 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z);
		Vertices.push_back(Vertex);
	}

	for (const isosurface::shared_vertex_mesh::triangle_t& ExtractedTriangle : ExtractedMesh.faces_)
	{
		ivec3 Triangle(ExtractedTriangle.v0, ExtractedTriangle.v1, ExtractedTriangle.v2);
		Triangles.push_back(Triangle);
	}

	ExportState.store(3);

	if (Format == ExportFormat::STL)
	{
		WriteSTL(Octree, Path, Vertices, Triangles, Scale);
	}
	else if (Format == ExportFormat::PLY)
	{
		WritePLY(Octree, Path, Vertices, Triangles, Scale);
	}

	delete Octree;

	ExportState.store(0);
}


void PointCloudExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, std::string Path, ExportFormat Format, float Scale)
{
	const vec3 Half = Step / vec3(2.0);
	const float Diagonal = length(Half);
	SDFOctree* Octree = SDFOctree::Create(Evaluator, 0.25);

	std::vector<glm::vec3> Vertices;
	std::mutex VerticesCS;

	{
		const vec3 Start = ModelMin;
		const vec3 Stop = ModelMax;
		const ivec3 Iterations = ivec3(ceil((Stop - Start) / Step));
		const int Slice = Iterations.x * Iterations.y;
		const int TotalCells = Iterations.x * Iterations.y * Iterations.z;
		VoxelCount.store(TotalCells);

		Pool([&]() \
		{
			while (ExportState.load() == 1 && ExportActive.load())
			{
				int i = GenerationProgress.fetch_add(1);
				if (i < TotalCells)
				{
					float Z = float(i / Slice) * Step.z + Start.z;
					float Y = float((i % Slice) / Iterations.x) * Step.y + Start.y;
					float X = float(i % Iterations.x) * Step.x + Start.x;

					vec3 Cursor = vec3(X, Y, Z) + Half;

					float Dist = Octree->Eval(Cursor);
					if (abs(Dist) < Diagonal)
					{
						VerticesCS.lock();
						Vertices.push_back(Cursor);
						VerticesCS.unlock();
					}
				}
				else
				{
					break;
				}
			}
		});
	}

	ExportState.store(2);
	VertexCount.store(Vertices.size());

	if (RefineIterations > 0)
	{
		Pool([&]() \
		{
			while (ExportState.load() == 2 && ExportActive.load())
			{
				int i = RefinementProgress.fetch_add(1);
				if (i < Vertices.size())
				{
					vec3& Vertex = Vertices[i];
					vec3 Low = Vertex - vec3(Half);
					vec3 High = Vertex + vec3(Half);

					vec3 Cursor = Vertex;
					for (int r = 0; r < RefineIterations; ++r)
					{
						vec3 RayDir = Octree->Gradient(Cursor);
						float Dist = Octree->Eval(Cursor) * -1.0;
						Cursor += RayDir * Dist;
					}
					Cursor = clamp(Cursor, Low, High);

					if (distance(Cursor, Vertex) <= Diagonal)
					{
						// TODO: despite the above clamp, some times the Cursor will end up on 0,0,0 when it would be
						// well outside a half voxel distance.  This branch should at least prevent that, but there is
						// probably a problem with the Gradient function that is causing this.
						Vertex = Cursor;
					}
				}
				else
				{
					break;
				}
			}
		});
	}

	ExportState.store(3);

	if (Format == ExportFormat::PLY)
	{
		std::vector<ivec3> NoTriangles;
		WritePLY(Octree, Path, Vertices, NoTriangles, Scale);
	}

	ExportState.store(0);
	delete Octree;
}


ExportProgress GetExportProgress()
{
	ExportProgress Progress;
	Progress.Stage = ExportState.load();
	Progress.Generation = float(GenerationProgress.load() - 1) / float(VoxelCount.load());
	Progress.Refinement = float(RefinementProgress.load() - 1) / float(VertexCount.load());
	Progress.Secondary = float(SecondaryProgress.load() - 1) / float(SecondaryCount.load());
	Progress.Write = float(WriteProgress.load() - 1) / float(WriteCount.load());
	return Progress;
}


void MeshExport(SDFNode* Evaluator, std::string Path, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, ExportFormat Format, bool ExportPointCloud, float Scale)
{
	ExportActive.store(true);
	ExportState.store(0);
	GenerationProgress.store(0);
	RefinementProgress.store(0);
	SecondaryProgress.store(0);
	WriteProgress.store(0);
	ExportState.store(1);

	auto Thunk = ExportPointCloud ? PointCloudExportThread : MeshExportThread;
	std::thread ExportThread(Thunk, Evaluator, ModelMin, ModelMax, Step, RefineIterations, Path, Format, Scale);
	ExportThread.detach();
}


void CancelExport(bool Halt)
{
	if (Halt)
	{
		ExportActive.store(false);
	}
	else
	{
		ExportState.fetch_add(1);
	}
}


void ExportCommon(SDFNode* Evaluator, float GridSize, int RefineIterations, const char* Path, ExportFormat Format, float Scale = 1.0)
{
	AABB Bounds = Evaluator->Bounds();
	float Step = 1.0 / GridSize;

	ExportActive.store(true);
	ExportState.store(0);
	GenerationProgress.store(0);
	RefinementProgress.store(0);
	WriteProgress.store(0);
	ExportState.store(1);
	MeshExportThread(Evaluator, Bounds.Min, Bounds.Max, vec3(Step), RefineIterations, std::string(Path), Format, Scale);
}


extern "C" TANGERINE_API void ExportSTL(SDFNode* Evaluator, float GridSize, int RefineIterations, const char* Path)
{
	ExportCommon(Evaluator, GridSize, RefineIterations, Path, ExportFormat::STL);
}


extern "C" TANGERINE_API void ExportPLY(SDFNode * Evaluator, float GridSize, int RefineIterations, const char* Path)
{
	ExportCommon(Evaluator, GridSize, RefineIterations, Path, ExportFormat::PLY);
}
