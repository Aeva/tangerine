
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
#include <nfd.h>
#include "export.h"


using TreeHandle = void*;
using namespace glm;
using namespace std::placeholders;


inline int max(int LHS, int RHS)
{
	return LHS >= RHS ? LHS : RHS;
}


struct  Vec3Less
{
	bool operator() (const vec3& LHS, const vec3& RHS) const
	{
		return LHS.x < RHS.x || (LHS.x == RHS.x && LHS.y < RHS.y) || (LHS.x == RHS.x && LHS.y == RHS.y && LHS.z < RHS.z);
	}
};


inline void Pool(const std::function<void()>& Thunk)
{
	static const int ThreadCount = max(std::thread::hardware_concurrency(), 2);
	std::vector<std::thread> Threads;
	Threads.reserve(ThreadCount);
	for (int i = 0; i < ThreadCount; ++i)
	{
		Threads.push_back(std::thread(Thunk));
	}
	for (auto& Thread : Threads)
	{
		Thread.join();
	}
}


std::atomic_bool ExportActive;
std::atomic_int ExportState(0);
std::atomic_int VoxelCount;
std::atomic_int GenerationProgress;
std::atomic_int VertexCount;
std::atomic_int RefinementProgress;
std::atomic_int QuadCount;
std::atomic_int WriteProgress;
void MeshExportThread(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations, std::string Path)
{
	const vec3 Half = Step / vec3(2.0);
	const float Diagonal = length(Half);

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
						SDFNode* Region = Evaluator->Clip(vec3(X, Y, Z), Diagonal * 2.0);
						if (Region == nullptr)
						{
							continue;
						}
						Dist.x = Region->Eval(Cursor - vec3(Step.x, 0.0, 0.0));
						Dist.y = Region->Eval(Cursor - vec3(0.0, Step.y, 0.0));
						Dist.z = Region->Eval(Cursor - vec3(0.0, 0.0, Step.z));
						Dist.w = Region->Eval(Cursor);
						delete Region;
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
	QuadCount.store(Quads.size());

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

					SDFNode* Subtree = Evaluator->Clip(Vertex, Diagonal);
					if (!Subtree)
					{
						continue;
					}

					vec3 Cursor = Vertex;
					for (int r = 0; r < RefineIterations; ++r)
					{
						vec3 RayDir = Subtree->Gradient(Cursor);
						float Dist = Subtree->Eval(Cursor) * -1.0;
						Cursor += RayDir * Dist;
					}
					delete Subtree;
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

	{
		std::ofstream OutFile;
		OutFile.open(Path, std::ios::out | std::ios::binary);

		// Write 80 bytes for the header.
		for (int i = 0; i < 80; ++i)
		{
			OutFile << '\0';
		}

		uint32_t Triangles = Quads.size() * 2;
		OutFile.write(reinterpret_cast<char*>(&Triangles), 4);

		for (ivec4& Quad : Quads)
		{
			if (ExportState.load() != 3 || !ExportActive.load())
			{
				break;
			}
			WriteProgress.fetch_add(1);
			{
				vec3 Center = (Vertices[Quad.x] + Vertices[Quad.y] + Vertices[Quad.z]) / vec3(3.0);
				vec3 Normal = Evaluator->Gradient(Center);
				OutFile.write(reinterpret_cast<char*>(&Normal), 12);

				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.x]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.y]), 12);
				OutFile.write(reinterpret_cast<char*>(&Vertices[Quad.z]), 12);

				uint16_t Attributes = 0;
				OutFile.write(reinterpret_cast<char*>(&Attributes), 2);
			}
			{
				vec3 Center = (Vertices[Quad.x] + Vertices[Quad.z] + Vertices[Quad.w]) / vec3(3.0);
				vec3 Normal = Evaluator->Gradient(Center);
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

	ExportState.store(0);
}


ExportProgress GetExportProgress()
{
	ExportProgress Progress;
	Progress.Stage = ExportState.load();
	Progress.Generation = float(GenerationProgress.load() - 1) / float(VoxelCount.load());
	Progress.Refinement = float(RefinementProgress.load() - 1) / float(VertexCount.load());
	Progress.Write = float(WriteProgress.load() - 1) / float(QuadCount.load());
	return Progress;
}


void MeshExport(SDFNode* Evaluator, vec3 ModelMin, vec3 ModelMax, vec3 Step, int RefineIterations)
{
	if (ExportState.load() == 0)
	{
		nfdchar_t* Path = nullptr;
		nfdresult_t Result = NFD_SaveDialog("stl", "model.stl", &Path);
		if (Result == NFD_OKAY)
		{
			ExportActive.store(true);
			ExportState.store(0);
			GenerationProgress.store(0);
			RefinementProgress.store(0);
			WriteProgress.store(0);
			ExportState.store(1);

			std::thread ExportThread(MeshExportThread, Evaluator, ModelMin, ModelMax, Step, RefineIterations, std::string(Path));
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
