
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

#include "errors.h"
#include "scheduler.h"
#include "sdf_model.h"
#include <fmt/format.h>
#include <surface_nets.h>
#include <atomic>
#include <random>


static std::default_random_engine RNGesus;
static std::uniform_real_distribution<double> Roll(-1.0, std::nextafter(1.0, DBL_MAX));


struct MeshingJob : AsyncTask
{
	SodapopDrawable* Painter;
	virtual void Run();
	virtual void Done();
	virtual void Abort();
	virtual ~MeshingJob();
};


void Sodapop::Populate(SodapopDrawable* Painter)
{
	Assert(Painter != nullptr);
	Painter->Hold();

	MeshingJob* Task = new MeshingJob();
	Task->Painter = Painter;

	Scheduler::Enqueue(Task);
}


void MeshingJob::Run()
{
	AABB Bounds = Painter->Evaluator->Bounds();

	const glm::vec3 ModelExtent = Bounds.Extent();
	const float ModelVolume = ModelExtent.x * ModelExtent.y * ModelExtent.z;

	if (ModelVolume > 0)
	{
		isosurface::regular_grid_t Grid;

		float Refinement = 1.0;
		const int Limit = 128;

		float Guess = 15.0;
		const int Target = 10000;

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

		auto Eval = [&](float X, float Y, float Z) -> float
		{
			return Painter->Evaluator->Eval(glm::vec3(X, Y, Z));
		};

		isosurface::mesh Mesh;
		isosurface::surface_nets(Eval, Grid, Mesh, Scheduler::GetState());

		Painter->Positions.reserve(Mesh.vertices_.size());
		Painter->Normals.reserve(Mesh.vertices_.size());
		Painter->Indices.reserve(Mesh.faces_.size() * 3);

		for (const isosurface::point_t& ExtractedVertex : Mesh.vertices_)
		{
			glm::vec4 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z, 1.0);
			Painter->Positions.push_back(Vertex);
			Painter->Normals.push_back(glm::vec4(0.0, 0.0, 0.0, 0.0));
		}

		for (const isosurface::shared_vertex_mesh::triangle_t& Face : Mesh.faces_)
		{
			Painter->Indices.push_back(Face.v0);
			Painter->Indices.push_back(Face.v1);
			Painter->Indices.push_back(Face.v2);

			glm::vec3 A = Painter->Positions[Face.v0].xyz();
			glm::vec3 B = Painter->Positions[Face.v1].xyz();
			glm::vec3 C = Painter->Positions[Face.v2].xyz();
			glm::vec3 AB = glm::normalize(A - B);
			glm::vec3 AC = glm::normalize(A - C);
			glm::vec4 N = glm::vec4(glm::normalize(glm::cross(AB, AC)), 1);

			if (!glm::any(glm::isnan(N)))
			{
				Painter->Normals[Face.v0] += N;
				Painter->Normals[Face.v1] += N;
				Painter->Normals[Face.v2] += N;
			}
		}

		for (glm::vec4& Normal : Painter->Normals)
		{
			Normal = glm::vec4(glm::normalize(Normal.xyz() / Normal.w), 1.0);
		}

		glm::vec3 JitterSpan = glm::vec3(Grid.dx, Grid.dy, Grid.dz) * glm::vec3(0.5);

		Painter->Colors.reserve(Painter->Positions.size());
		for (const glm::vec4& Position : Painter->Positions )
		{
			// HACK taking a random average within the approximate voxel bounds of a vert
			// goes a long ways to improve the readability of some models.  However, the
			// correct thing to do here might be a little more like how we calculate normals.
			// For each triangle, sample randomly within the triangle, and accumualte the
			// samples into vertex buckets.  Although, the accumulated values should probably
			// be weighted by their barycentric coordinates or something like that.

			const int Count = 20;
			std::vector<glm::vec3> Samples;
			Samples.reserve(Count);
			glm::vec3 Average = glm::vec3(0.0);
			for (int i = 0; i < Count; ++i)
			{
				glm::vec3 Jitter = JitterSpan * glm::vec3(Roll(RNGesus), Roll(RNGesus), Roll(RNGesus));
				glm::vec3 Tmp = Position.xyz();
				glm::vec3 Sample = Painter->Evaluator->Sample(Tmp + Jitter);
				if (!glm::any(glm::isnan(Sample)))
				{
					Samples.push_back(Sample);
					Average += Sample;
				}
			}
			Average /= glm::vec3(float(Samples.size()));
			Painter->Colors.push_back(glm::vec4(Average, 1.0));
		}

		Painter->MeshReady.store(true);
	}
}


void MeshingJob::Done()
{
	void* JobPtr = (void*)this;
	size_t Triangles = Painter->Indices.size() / 3;
	size_t Vertices = Painter->Positions.size();
	fmt::print("[{}] Created {} triangles with {} vertices.\n", JobPtr, Triangles, Vertices);
	SetTreeEvaluator(Painter->Evaluator);
}


void MeshingJob::Abort()
{
	fmt::print("[{}] Job cancelled.\n", (void*)this);
}


MeshingJob::~MeshingJob()
{
	Painter->Release();
}


std::map<size_t, SDFModel*> AttachedModels;


struct AttachModelTask : AsyncTask
{
	SDFModel* Instance;
	virtual void Run()
	{
		size_t Key = (size_t)Instance;
		auto Result = AttachedModels.insert(std::pair{Key, Instance});

		fmt::print("[{}] Attached model.\n", (void*)this);
	};
	virtual void Done() {}
	virtual void Abort() {}
};


struct DetachModelTask : AsyncTask
{
	SDFModel* Instance;
	virtual void Run()
	{
		size_t Key = (size_t)Instance;
		AttachedModels.erase(Key);

		fmt::print("[{}] Detached model.\n", (void*)this);
	};
	virtual void Done()
	{
		Instance->Release();
	};
	virtual void Abort() {}
};


void Sodapop::Attach(SDFModel* Instance)
{
	Instance->Hold();
	AttachModelTask* Task = new AttachModelTask();
	Task->Instance = Instance;
	//

	Scheduler::Enqueue(Task, true);
}


void Sodapop::Detach(SDFModel* Instance)
{
	DetachModelTask* Task = new DetachModelTask();
	Task->Instance = Instance;
	//

	Scheduler::Enqueue(Task, true);
}


void Sodapop::Hammer()
{

}


#endif // RENDERER_SODAPOP