
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
#include <random>
#include <atomic>
#include <mutex>


static std::default_random_engine RNGesus;
static std::uniform_real_distribution<double> Roll(-1.0, std::nextafter(1.0, DBL_MAX));


struct MeshingScratch : isosurface::AsyncParallelSurfaceNets
{
	std::atomic_int FirstLoopIndex = 0;
	std::atomic_int FirstLoopCompleted = 0;

	std::mutex SecondLoopIteratorCS;
	std::unordered_map<std::size_t, std::uint64_t>::iterator SecondLoopIterator;
	std::atomic_int SecondLoopCompleted = 0;
};


struct MeshingJob : AsyncTask
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;
	virtual void Run();
	virtual void Done();
	virtual void Abort();
};


struct MeshingVertexLoop : ParallelTask
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;

	virtual void Run();
	virtual ParallelTask* Fork()
	{
		return new MeshingVertexLoop(*this);
	}
};


struct MeshingFaceLoop : ParallelTask
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;

	virtual void Run();
	virtual ParallelTask* Fork()
	{
		return new MeshingFaceLoop(*this);
	}
};


void Sodapop::Populate(SodapopDrawableShared Painter)
{
	MeshingJob* Task = new MeshingJob();
	Task->PainterWeakRef = Painter;
	Task->EvaluatorWeakRef = Painter->Evaluator;

	Scheduler::Enqueue(Task);
}


void MeshingJob::Run()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();

	if (!Painter || !Evaluator)
	{
		return;
	}

	AABB Bounds = Evaluator->Bounds();

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

		Painter->Scratch = new MeshingScratch();
		Painter->Scratch->Grid = Grid;
		{
			SDFNode* EvaluatorRaw = Evaluator.get();
			Painter->Scratch->ImplicitFunction = [EvaluatorRaw](float X, float Y, float Z) -> float
			{
				return EvaluatorRaw->Eval(glm::vec3(X, Y, Z));
			};
		}
		Painter->Scratch->Setup();

		MeshingVertexLoop* Next = new MeshingVertexLoop();
		Next->PainterWeakRef = Painter;
		Next->EvaluatorWeakRef = Evaluator;
		Scheduler::Enqueue(Next);
	}
}


void MeshingJob::Done()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();
	if (Painter && Evaluator)
	{
		void* JobPtr = (void*)this;
		size_t Triangles = Painter->Indices.size() / 3;
		size_t Vertices = Painter->Positions.size();
		fmt::print("[{}] Created {} triangles with {} vertices.\n", JobPtr, Triangles, Vertices);
		SetTreeEvaluator(Painter->Evaluator);
	}
	else
	{
		Abort();
	}
}


void MeshingJob::Abort()
{
	fmt::print("[{}] Job cancelled.\n", (void*)this);
}


void MeshingVertexLoop::Run()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();

	if (Painter && Evaluator)
	{
		MeshingScratch* Scratch = Painter->Scratch;
		bool LoopExhausted = false;

		while (true)
		{
			const int ClaimedIndex = Scratch->FirstLoopIndex.fetch_add(1);
			const int Range = Scratch->FirstLoopDomain.size();
			if (ClaimedIndex < Range)
			{
				Scratch->FirstLoopThunk(*Scratch, ClaimedIndex);

				const int CompletionIndex = Scratch->FirstLoopCompleted.fetch_add(1);
				if (CompletionIndex == Range - 1)
				{
					LoopExhausted = true;
					break;
				}
			}
			else
			{
				break;
			}
		}

		if (LoopExhausted)
		{
			Scratch->SecondLoopIterator = Scratch->SecondLoopDomain.begin();
			MeshingFaceLoop* Next = new MeshingFaceLoop();
			Next->PainterWeakRef = Painter;
			Next->EvaluatorWeakRef = Evaluator;
			Scheduler::Enqueue(Next);
		}
	}
}


void MeshingFaceLoop::Run()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();

	if (Painter && Evaluator)
	{
		MeshingScratch* Scratch = Painter->Scratch;
		bool LoopExhausted = false;
		while (true)
		{
			bool ValidIteration = false;
			std::unordered_map<std::size_t, std::uint64_t>::iterator Cursor;
			{
				Scratch->SecondLoopIteratorCS.lock();
				Cursor = Scratch->SecondLoopIterator;
				ValidIteration = Cursor != Scratch->SecondLoopDomain.end();
				if (ValidIteration)
				{
					Scratch->SecondLoopIterator++;
				}
				Scratch->SecondLoopIteratorCS.unlock();
			}
			if (ValidIteration)
			{
				Scratch->SecondLoopThunk(*Scratch, *Cursor);

				const int Range = Scratch->SecondLoopDomain.size();
				const int CompletionIndex = Scratch->SecondLoopCompleted.fetch_add(1);
				if (CompletionIndex == Range - 1)
				{
					LoopExhausted = true;
					break;
				}
			}
			else
			{
				break;
			}
		}

		if (LoopExhausted)
		{
			isosurface::mesh& Mesh = Scratch->OutputMesh;

			// TODO: this should be broken up into several more parallel tasks, the last of which schedules the recurring lighting task

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

			glm::vec3 JitterSpan = glm::vec3(Scratch->Grid.dx, Scratch->Grid.dy, Scratch->Grid.dz) * glm::vec3(0.5);

			Painter->Colors.reserve(Painter->Positions.size());
			for (const glm::vec4& Position : Painter->Positions)
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
					glm::vec3 Sample = Evaluator->Sample(Tmp + Jitter);
					if (!glm::any(glm::isnan(Sample)))
					{
						Samples.push_back(Sample);
						Average += Sample;
					}
				}
				Average /= glm::vec3(float(Samples.size()));
				Painter->Colors.push_back(glm::vec4(Average, 1.0));
			}

			delete Painter->Scratch;
			Painter->Scratch = nullptr;

			Painter->MeshReady.store(true);
		}
	}
}


struct ShaderTask : public ContinuousTask
{
	SDFModelWeakRef ModelWeakRef;
	SodapopDrawableWeakRef PainterWeakRef;

	bool Run();
};


void Sodapop::Attach(SDFModelShared& Instance)
{
	SodapopDrawableShared SodapopPainter = std::dynamic_pointer_cast<SodapopDrawable>(Instance->Painter);

	if (SodapopPainter)
	{
		ShaderTask* Task = new ShaderTask();
		Task->ModelWeakRef = Instance;
		Task->PainterWeakRef = SodapopPainter;
		Scheduler::Enqueue(Task);
	}
}


bool ShaderTask::Run()
{
	SDFModelShared Instance = ModelWeakRef.lock();
	SodapopDrawableShared Painter = PainterWeakRef.lock();

	if (Instance && Painter)
	{
		if (Instance->Dirty.load() && Painter->MeshReady.load())
		{
			// The model instance requested a repaint and the painter is ready for drawing.

			if (Instance->Colors.size() == 0)
			{
				Instance->Colors.resize(Painter->Colors.size());
			}

			glm::vec4 LocalEye = Instance->Transform.LastFoldInverse * glm::vec4(Instance->CameraOrigin, 1.0);

			for (int n = 0; n < Instance->Colors.size(); ++n)
			{
				if (Instance->Drawing.load())
				{
					break;
				}
				const int i = Instance->NextUpdate % Instance->Colors.size();
				Instance->NextUpdate = i + 1;

				glm::vec3 BaseColor = Painter->Colors[i].xyz();

				// Palecek 2022, "PBR Based Rendering"
				glm::vec3 V = glm::normalize(LocalEye.xyz() - Painter->Positions[i].xyz());
				glm::vec3 N = Painter->Normals[i].xyz();
				float D = glm::pow(glm::max(glm::dot(N, glm::normalize(N * 0.75f + V)), 0.0f), 2.0f);
				float F = 1.0 - glm::max(glm::dot(N, V), 0.0f);
				float BSDF = D + F * 0.25;
				Instance->Colors[i] = glm::vec4(BaseColor * BSDF, 1.0);
			}

			// TODO: This needs some way to determine if the instance has converged since it was last marked dirty.
			//Instance->Dirty.store(false);
			
		}
		return true;
	}
	else
	{
		// One or both of the model instance and painter are invalid now, so kill the task.
		return false;
	}
}


#endif // RENDERER_SODAPOP