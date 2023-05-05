
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
#include "sdf_model.h"
#include <fmt/format.h>
#include <surface_nets.h>
#include <atomic>


void MeshingJob::Enqueue(SodapopDrawable* Painter)
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

		auto Eval = [&](float X, float Y, float Z) -> float
		{
			return Painter->Evaluator->Eval(glm::vec3(X, Y, Z));
		};

		isosurface::mesh Mesh;
		isosurface::surface_nets(Eval, Grid, Mesh, Scheduler::GetState());

		Painter->Positions.reserve(Mesh.vertices_.size());
		Painter->Indices.reserve(Mesh.faces_.size() * 3);

		for (const isosurface::point_t& ExtractedVertex : Mesh.vertices_)
		{
			glm::vec4 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z, 1.0);
			Painter->Positions.push_back(Vertex);
		}

		for (const isosurface::shared_vertex_mesh::triangle_t& ExtractedTriangle : Mesh.faces_)
		{
			Painter->Indices.push_back(ExtractedTriangle.v0);
			Painter->Indices.push_back(ExtractedTriangle.v1);
			Painter->Indices.push_back(ExtractedTriangle.v2);
		}

		Painter->Colors.reserve(Painter->Positions.size());
		for (const glm::vec4& Position : Painter->Positions )
		{
			glm::vec3 Color = Painter->Evaluator->Sample(Position.xyz);
			Painter->Colors.push_back(glm::vec4(Color, 1.0));
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


#endif // RENDERER_SODAPOP