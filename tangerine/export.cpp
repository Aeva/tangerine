
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
#ifndef MINIMAL_DLL
#include <nfd.h>
#endif
#include <fmt/format.h>
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


void WriteSTL(SDFOctree* Octree, std::string Path, std::vector<vec3> Vertices, std::vector<ivec4> Quads)
{
	std::ofstream OutFile;
	OutFile.open(Path, std::ios::out | std::ios::binary);

	// Write 80 bytes for the header.
	for (int i = 0; i < 80; ++i)
	{
		OutFile << '\0';
	}

	SecondaryCount.store(Quads.size());
	std::vector<glm::vec3> Normals;
	Normals.reserve(Quads.size());
	for (ivec4& Quad : Quads)
	{
		if (ExportState.load() != 3 || !ExportActive.load())
		{
			break;
		}
		SecondaryProgress.fetch_add(1);
		vec3 Center = (Vertices[Quad.x] + Vertices[Quad.y] + Vertices[Quad.z] + Vertices[Quad.w]) / vec3(4.0);
		vec3 Normal = Octree->Gradient(Center);
		Normals.push_back(Normal);
	}

	WriteCount.store(Quads.size());
	uint32_t Triangles = Quads.size() * 2;
	OutFile.write(reinterpret_cast<char*>(&Triangles), 4);
	for (int q = 0; q < Quads.size(); ++q)
	{
		if (ExportState.load() != 3 || !ExportActive.load())
		{
			break;
		}
		WriteProgress.fetch_add(1);
		ivec4& Quad = Quads[q];
		vec3& Normal = Normals[q];
		{
			OutFile.write(reinterpret_cast<char*>(&Normal), 12);

			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.x]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.y]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.z]), 12);

			uint16_t Attributes = 0;
			OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
		}
		{
			OutFile.write(reinterpret_cast<char*>(&Normal), 12);

			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.x]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.z]), 12);
			OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.w]), 12);

			uint16_t Attributes = 0;
			OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
		}
	}

	// Align to 4 bytes for good luck.
	size_t Written = 84 + 100 * Quads.size();
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


std::string MonochromePlyHeader(size_t VertexCount, size_t TriangleCount)
{
	static const std::string EndianName = GetEndianName();
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
		"element face {}\n"
		"property list uchar uint vertex_indices\n"
		"end_header\n",
		EndianName,
		VertexCount,
		TriangleCount);
}


std::string ColorPlyHeader(size_t VertexCount, size_t TriangleCount)
{
	static const std::string EndianName = GetEndianName();
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
		"property float red\n"
		"property float green\n"
		"property float blue\n"
		"element face {}\n"
		"property list uchar uint vertex_indices\n"
		"end_header\n",
		EndianName,
		VertexCount,
		TriangleCount);
}


void WritePLY(SDFOctree* Octree, std::string Path, std::vector<vec3> Vertices, std::vector<ivec4> Quads)
{
	const bool ExportColor = Octree->Evaluator->HasPaint();
	std::string Header;
	{
		size_t VertexCount = Vertices.size();
		size_t TriangleCount = Quads.size() * 2;

		if (ExportColor)
		{
			Header = ColorPlyHeader(VertexCount, TriangleCount);
		}
		else
		{
			Header = MonochromePlyHeader(VertexCount, TriangleCount);
		}
	}

	// Populate vertex attributes.
	SecondaryCount.store(Vertices.size());
	std::vector<glm::vec3> Normals;
	std::vector<glm::vec3> Colors;
	Normals.reserve(Vertices.size());
	Colors.reserve(Vertices.size());
	for (int v = 0; v < Vertices.size(); ++v)
	{
		SecondaryProgress.fetch_add(1);
		Normals.push_back(Octree->Gradient(Vertices[v]));
		if (ExportColor)
		{
			Colors.push_back(Octree->Sample(Vertices[v]));
		}
	}

	// Write vertex data.
	WriteCount.store(Vertices.size() + Quads.size());
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
			OutFile.write(reinterpret_cast<char*>(&Colors[v]), 12);
		}
	}

	// Write face data.
	const int8_t FaceVerts = 3;
	for (int q = 0; q < Quads.size(); ++q)
	{
		WriteProgress.fetch_add(1);
		{
			ivec3 FaceA = Quads[q].xyz;
			OutFile.write(reinterpret_cast<const char*>(&FaceVerts), 1);
			OutFile.write(reinterpret_cast<char*>(&FaceA), 12);
		}
		{
			ivec3 FaceB = Quads[q].xzw;
			OutFile.write(reinterpret_cast<const char*>(&FaceVerts), 1);
			OutFile.write(reinterpret_cast<char*>(&FaceB), 12);
		}
	}

	OutFile.close();
}


void MeshExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, std::string Path, ExportFormat Format)
{
	const vec3 Half = Step / vec3(2.0);
	const float Diagonal = length(Half);
	SDFOctree* Octree = SDFOctree::Create(Evaluator, 0.25);

	std::vector<vec3> Vertices;
	std::map<vec3, int, Vec3Less> VertMemo;
	std::mutex VerticesCS;

	auto NewVert = [&](vec3 Vertex) -> int
	{
		VerticesCS.lock();
		auto Found = VertMemo.find(Vertex);
		if (Found != VertMemo.end())
		{
			VerticesCS.unlock();
			return Found->second;
		}
		else
		{
			int Next = Vertices.size();
			VertMemo[Vertex] = Next;
			Vertices.push_back(Vertex);
			VerticesCS.unlock();
			return Next;
		}
	};

	std::vector<ivec4> Quads;
	std::mutex QuadsCS;

	{
		const vec3 Start = ModelMin;
		const vec3 Stop = ModelMax + Step;
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

					vec4 Dist;
					{
						float Coarse = Octree->Eval(vec3(X, Y, Z));
						if (Coarse > Diagonal * 2.0)
						{
							continue;
						}
						Dist.x = Octree->Eval(Cursor - vec3(Step.x, 0.0, 0.0));
						Dist.y = Octree->Eval(Cursor - vec3(0.0, Step.y, 0.0));
						Dist.z = Octree->Eval(Cursor - vec3(0.0, 0.0, Step.z));
						Dist.w = Octree->Eval(Cursor);
					}

					if (sign(Dist.w) != sign(Dist.x))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, 1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0, 1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.x))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist.w) != sign(Dist.y))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, 1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, 1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.y))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
					}

					if (sign(Dist.w) != sign(Dist.z))
					{
						ivec4 Quad(
							NewVert(Half * vec3(-1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, -1.0, -1.0) + Cursor),
							NewVert(Half * vec3(1.0, 1.0, -1.0) + Cursor),
							NewVert(Half * vec3(-1.0, 1.0, -1.0) + Cursor));
						if (sign(Dist.w) < sign(Dist.z))
						{
							Quad = Quad.wzyx;
						}
						QuadsCS.lock();
						Quads.push_back(Quad);
						QuadsCS.unlock();
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

					float Coarse = Octree->Eval(Vertex);
					if (Diagonal > Diagonal)
					{
						continue;
					}

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

	if (Format == ExportFormat::STL)
	{
		WriteSTL(Octree, Path, Vertices, Quads);
	}
	else if (Format == ExportFormat::PLY)
	{
		WritePLY(Octree, Path, Vertices, Quads);
	}

	ExportState.store(0);
	delete Octree;
}


#ifndef MINIMAL_DLL
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


void MeshExport(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, ExportFormat Format)
{
	if (ExportState.load() == 0)
	{
		nfdchar_t* Path = nullptr;
		nfdresult_t Result;
		if (Format == ExportFormat::STL)
		{
			Result = NFD_SaveDialog("stl", "model.stl", &Path);
		}
		else if (Format == ExportFormat::PLY)
		{
			Result = NFD_SaveDialog("ply", "model.ply", &Path);
		}
		if (Result == NFD_OKAY)
		{
			ExportActive.store(true);
			ExportState.store(0);
			GenerationProgress.store(0);
			RefinementProgress.store(0);
			SecondaryProgress.store(0);
			WriteProgress.store(0);
			ExportState.store(1);

			std::thread ExportThread(MeshExportThread, Evaluator, ModelMin, ModelMax, Step, RefineIterations, std::string(Path), Format);
			ExportThread.detach();
		}
	}
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
#endif


void ExportCommon(SDFNode* Evaluator, float GridSize, int RefineIterations, const char* Path, ExportFormat Format)
{
	AABB Bounds = Evaluator->Bounds();
	float Step = 1.0 / GridSize;

	ExportActive.store(true);
	ExportState.store(0);
	GenerationProgress.store(0);
	RefinementProgress.store(0);
	WriteProgress.store(0);
	ExportState.store(1);
	MeshExportThread(Evaluator, Bounds.Min, Bounds.Max, vec3(Step), RefineIterations, std::string(Path), Format);
}


extern "C" TANGERINE_API void ExportSTL(SDFNode* Evaluator, float GridSize, int RefineIterations, const char* Path)
{
	ExportCommon(Evaluator, GridSize, RefineIterations, Path, ExportFormat::STL);
}


extern "C" TANGERINE_API void ExportPLY(SDFNode * Evaluator, float GridSize, int RefineIterations, const char* Path)
{
	ExportCommon(Evaluator, GridSize, RefineIterations, Path, ExportFormat::PLY);
}
