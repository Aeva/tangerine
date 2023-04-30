
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

#include "sodapop.h"
#if RENDERER_SODAPOP

#include "sdf_model.h"

#include <fmt/format.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <surface_nets.h>


static std::atomic_bool Live;
static std::vector<std::thread> ActiveThreads;


template<typename T>
struct FIFO
{
	void Push(const T& InEntry)
	{
		std::scoped_lock Lock(CS);
		Pending.push_back(InEntry);
	}

	bool TryPop(T& OutEntry)
	{
		std::scoped_lock Lock(CS);
		if (Pending.size() == 0)
		{
			return false;
		}
		else
		{
			OutEntry = Pending.front();
			Pending.pop_front();
			return true;
		}
	}

private:
	std::mutex CS;
	std::deque<T> Pending;
};


static std::atomic_int MeshingThreadsActive;


struct MeshingJob
{
	SodapopDrawable* Drawable;
	SDFNode* Evaluator;

	static void Drain(FIFO<MeshingJob>& Queue)
	{
repeat:
		MeshingJob Pending;
		if (Queue.TryPop(Pending))
		{
			Pending.Drawable->Release();
			Pending.Evaluator->Release();
			goto repeat;
		}
	}
};


static FIFO<MeshingJob> PendingMeshingJobs;
static FIFO<MeshingJob> CompletedMeshingJobs;


StatusCode Sodapop::Setup()
{
	MeshingThreadsActive.store(0);
	Live.store(true);
	return StatusCode::PASS;
}


void Sodapop::Teardown()
{
	Live.store(false);

	MeshingJob::Drain(PendingMeshingJobs);

	const int WaitingThreads = MeshingThreadsActive.load();
	if (WaitingThreads > 0)
	{
		fmt::print("Halting {} active meshing threads...\n", WaitingThreads);
	}
	for (auto& Thread : ActiveThreads)
	{
		Thread.join();
	}

	MeshingJob::Drain(CompletedMeshingJobs);

	ActiveThreads.clear();
}


void MeshingThread()
{
	while (Live.load())
	{
		MeshingJob Packet;
		if (!PendingMeshingJobs.TryPop(Packet))
		{
			break;
		}

		AABB Bounds = Packet.Evaluator->Bounds();

		const glm::vec3 ModelExtent = Bounds.Extent();
		const float ModelVolume = ModelExtent.x * ModelExtent.y * ModelExtent.z;

		if (ModelVolume > 0)
		{
			isosurface::regular_grid_t Grid;

			float Refinement = 1.0;
			const int Limit = 128;

			float Guess = 15.0;
			const int Target = 8192;

			for (int Attempt = 0; Attempt < Limit; ++Attempt)
			{
				glm::vec3 Step = ModelExtent / glm::vec3(Guess);

				glm::vec3 ModelMin = Bounds.Min;
				glm::vec3 ModelMax = Bounds.Max;

				ModelMin -= Step * glm::vec3(2.0);
				glm::ivec3 Extent = glm::ivec3(glm::ceil((ModelMax - ModelMin) / Step));

				Grid.x = ModelMin.x;
				Grid.y = ModelMin.y;
				Grid.z = ModelMin.z;
				Grid.dx = Step.x;
				Grid.dy = Step.y;
				Grid.dz = Step.z;
				Grid.sx = Extent.x;
				Grid.sy = Extent.y;
				Grid.sz = Extent.z;

				int Volume = Extent.x * Extent.y * Extent.z;

				if (Volume == Target)
				{
					break;
				}
				if (Volume < Target)
				{
					Guess += Refinement;
				}
				else if (Volume > Target)
				{
					Refinement *= 0.9;
					Guess -= Refinement;
				}
			}

			SDFOctree* Octree = SDFOctree::Create(Packet.Evaluator, 0.25);

			auto Eval = [&](float X, float Y, float Z) -> float
			{
				return Octree->Eval(glm::vec3(X, Y, Z));
			};

			isosurface::mesh Mesh;
			isosurface::surface_nets(Eval, Grid, Mesh, Live);

			Packet.Drawable->Positions.reserve(Mesh.vertices_.size());
			Packet.Drawable->Indices.reserve(Mesh.faces_.size() * 3);

			for (const isosurface::point_t& ExtractedVertex : Mesh.vertices_)
			{
				glm::vec4 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z, 1.0);
				Packet.Drawable->Positions.push_back(Vertex);
			}

			for (const isosurface::shared_vertex_mesh::triangle_t& ExtractedTriangle : Mesh.faces_)
			{
				Packet.Drawable->Indices.push_back(ExtractedTriangle.v0);
				Packet.Drawable->Indices.push_back(ExtractedTriangle.v1);
				Packet.Drawable->Indices.push_back(ExtractedTriangle.v2);
			}

			Packet.Drawable->Colors.reserve(Packet.Drawable->Positions.size());
			for (const glm::vec4& Position : Packet.Drawable->Positions )
			{
				glm::vec3 Color = Packet.Evaluator->Sample(Position.xyz);
				Packet.Drawable->Colors.push_back(glm::vec4(Color, 1.0));
			}

			delete Octree;
			Packet.Drawable->MeshReady.store(true);
		}

		CompletedMeshingJobs.Push(Packet);
	}

	--MeshingThreadsActive;
}


void Sodapop::Schedule(SodapopDrawable* Drawable, SDFNode* Evaluator)
{
	SetTreeEvaluator(Evaluator);
	Drawable->MeshReady.store(false);
	Drawable->Hold();
	Evaluator->Hold();
	{
		MeshingJob Packet = { Drawable, Evaluator };
		PendingMeshingJobs.Push(Packet);
	}

	fmt::print("[{}] New background meshing job.\n", (void*)Drawable);

	static const int MaxThreadCount = std::max(int(std::thread::hardware_concurrency()) - 1, 1);
	if (MeshingThreadsActive.load() < MaxThreadCount)
	{
		++MeshingThreadsActive;
		ActiveThreads.push_back(std::thread(MeshingThread));
	}
}


void Sodapop::Advance()
{
repeat:
	MeshingJob Completed;
	if (CompletedMeshingJobs.TryPop(Completed))
	{
		void* JobPtr = (void*)(Completed.Drawable);
		size_t Triangles = Completed.Drawable->Indices.size() / 3;
		size_t Vertices = Completed.Drawable->Positions.size();
		fmt::print("[{}] Created {} triangles with {} vertices.\n", JobPtr, Triangles, Vertices);

		Completed.Drawable->Release();
		Completed.Evaluator->Release();
		goto repeat;
	}
}


#endif // RENDERER_SODAPOP