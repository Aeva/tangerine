
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
	SDFOctreeShared Evaluator;
	std::mutex SecondLoopIteratorCS;
	std::unordered_map<std::size_t, std::uint64_t>::iterator SecondLoopIterator;

	std::atomic_int ThirdLoopIndex = 0;
	std::mutex ThirdLoopIteratorCS;
};


struct MeshingJob : AsyncTask
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFOctreeWeakRef EvaluatorWeakRef;
	virtual void Run();
	virtual void Done();
	virtual void Abort();
};


struct ParallelTaskChain : ParallelTask
{
	ParallelTask* NextTask = nullptr;

	~ParallelTaskChain()
	{
		if (NextTask)
		{
			delete NextTask;
		}
	}
};


template<typename ContainerT>
struct MeshingVectorTask : ParallelTaskChain
{
	using SharedT = std::shared_ptr<MeshingVectorTask<ContainerT>>;
	using ElementT = typename ContainerT::value_type;

	SodapopDrawableWeakRef PainterWeakRef;
	SDFOctreeWeakRef EvaluatorWeakRef;

	std::atomic_int NextIndex = 0;

	ContainerT* Domain;

	MeshingVectorTask(SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(&InDomain)
	{
	}

	virtual void Loop(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int ElementIndex)
	{
	}

	virtual void Done(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
	}

	virtual void Run()
	{
		SodapopDrawableShared Painter = PainterWeakRef.lock();
		SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();
		if (Painter && Evaluator)
		{
			while (true)
			{
				const int ClaimedIndex = NextIndex.fetch_add(1);
				const int Range = Domain->size();
				if (ClaimedIndex < Range)
				{
					auto Cursor = std::next(Domain->begin(), ClaimedIndex);
					Loop(Painter, Evaluator, *Cursor, ClaimedIndex);
				}
				else
				{
					break;
				}
			}
		}
	}

	virtual void Exhausted()
	{
		SodapopDrawableShared Painter = PainterWeakRef.lock();
		SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();
		if (Painter && Evaluator)
		{
			Done(Painter, Evaluator);
			if (NextTask)
			{
				Scheduler::EnqueueParallel(NextTask);
				NextTask = nullptr;
			}
		}
	}
};


//template<typename ContainerT, typename LoopThunkT, typename DoneThunkT>
template<typename ContainerT>
struct MeshingVectorLambdaTask : MeshingVectorTask<ContainerT>
{
	using SharedT = std::shared_ptr<MeshingVectorTask<ContainerT>>;
	using ElementT = typename ContainerT::value_type;

	using LoopThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&, ElementT&, const int)>;
	using DoneThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&)>;

	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	MeshingVectorLambdaTask(SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: MeshingVectorTask<ContainerT>(InPainter, InEvaluator, InDomain)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
	{
	}

	virtual void Loop(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int ElementIndex)
	{
		LoopThunk(Painter, Evaluator, Element, ElementIndex);
	}

	virtual void Done(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		DoneThunk(Painter, Evaluator);
	}
};


struct MeshingFaceLoop : ParallelTaskChain
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFOctreeWeakRef EvaluatorWeakRef;

	virtual void Run();

	virtual void Exhausted();
};


void Sodapop::Populate(SodapopDrawableShared Painter)
{
	Painter->Scratch = new MeshingScratch();
	Painter->Scratch->Evaluator = SDFOctree::Create(Painter->Evaluator);

	MeshingJob* Task = new MeshingJob();
	Task->PainterWeakRef = Painter;
	Task->EvaluatorWeakRef = Painter->Scratch->Evaluator;

	Scheduler::Enqueue(Task);
}


void MeshingJob::Run()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();

	if (!Painter || !Evaluator)
	{
		return;
	}

	AABB Bounds = Evaluator->Bounds;

	const glm::vec3 ModelExtent = Bounds.Extent();
	const float ModelVolume = ModelExtent.x * ModelExtent.y * ModelExtent.z;

	if (ModelVolume > 0)
	{
		Painter->MeshingStart = Clock::now();
		isosurface::regular_grid_t Grid;

		float Refinement = 1.0;
		const int Limit = 128;

		glm::vec3 Guess = glm::normalize(ModelExtent) * glm::vec3(15.0);
		const int Target = 100000;

		for (int Attempt = 0; Attempt < Limit; ++Attempt)
		{
			glm::vec3 Step = ModelExtent / Guess;

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

		Painter->Scratch->Grid = Grid;
		{
			SDFOctree* EvaluatorRaw = Evaluator.get();
			Painter->Scratch->ImplicitFunction = [EvaluatorRaw](float X, float Y, float Z) -> float
			{
				return EvaluatorRaw->Eval(glm::vec3(X, Y, Z));
			};
		}
		Painter->Scratch->Setup();


		ParallelTaskChain* MeshingVertexLoopTask;
		{
			using TaskT = MeshingVectorLambdaTask<std::vector<std::size_t>>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const std::size_t& Element, const int Ignore)
			{
				MeshingScratch* Scratch = Painter->Scratch;
				Scratch->FirstLoopThunk(*Scratch, Element);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				MeshingScratch* Scratch = Painter->Scratch;

				if (Scratch->SecondLoopDomain.size() > 0)
				{
					Scratch->SecondLoopIterator = Scratch->SecondLoopDomain.begin();
				}
			};

			MeshingVertexLoopTask = new TaskT(Painter, Evaluator, Painter->Scratch->FirstLoopDomain, LoopThunk, DoneThunk);
		}

		MeshingFaceLoop* MeshingFaceLoopTask;
		{
			MeshingFaceLoopTask = new MeshingFaceLoop();
			MeshingFaceLoopTask->PainterWeakRef = Painter;
			MeshingFaceLoopTask->EvaluatorWeakRef = Evaluator;

			MeshingVertexLoopTask->NextTask = MeshingFaceLoopTask;
		}

		ParallelTaskChain* MeshingNormalLoopTask;
		{
			using FaceT = isosurface::mesh::triangle_t;
			using TaskT = MeshingVectorLambdaTask<isosurface::mesh::faces_container_type>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const FaceT& Face, const int Index)
			{
				isosurface::mesh& Mesh = Painter->Scratch->OutputMesh;

				Painter->Indices[Index * 3 + 0] = Face.v0;
				Painter->Indices[Index * 3 + 1] = Face.v1;
				Painter->Indices[Index * 3 + 2] = Face.v2;

				glm::vec3 A = Painter->Positions[Face.v0].xyz();
				glm::vec3 B = Painter->Positions[Face.v1].xyz();
				glm::vec3 C = Painter->Positions[Face.v2].xyz();
				glm::vec3 AB = glm::normalize(A - B);
				glm::vec3 AC = glm::normalize(A - C);
				glm::vec4 N = glm::vec4(glm::normalize(glm::cross(AB, AC)), 1);

				if (!glm::any(glm::isnan(N)))
				{
					Painter->Scratch->ThirdLoopIteratorCS.lock();
					Painter->Normals[Face.v0] += N;
					Painter->Normals[Face.v1] += N;
					Painter->Normals[Face.v2] += N;
					Painter->Scratch->ThirdLoopIteratorCS.unlock();
				}
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
			};

			MeshingNormalLoopTask = new TaskT(Painter, Evaluator, Painter->Scratch->OutputMesh.faces_, LoopThunk, DoneThunk);
			MeshingFaceLoopTask->NextTask = MeshingNormalLoopTask;
		}

		ParallelTaskChain* MeshingAverageNormalLoopTask;
		{
			using TaskT = MeshingVectorLambdaTask<std::vector<glm::vec4>>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, glm::vec4& Normal, const int Index)
			{
				Normal = glm::vec4(glm::normalize(Normal.xyz() / Normal.w), 1.0);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				Painter->Colors.resize(Painter->Positions.size(), glm::vec4(0.0, 0.0, 0.0, 1.0));
			};

			MeshingAverageNormalLoopTask = new TaskT(Painter, Evaluator, Painter->Normals, LoopThunk, DoneThunk);
			MeshingNormalLoopTask->NextTask = MeshingAverageNormalLoopTask;
		}

		ParallelTaskChain* MeshingJitterLoopTask;
		{
			using TaskT = MeshingVectorLambdaTask<std::vector<glm::vec4>>;

			glm::vec3 JitterSpan = glm::vec3(Grid.dx, Grid.dy, Grid.dz) * glm::vec3(0.5);

			TaskT::LoopThunkT LoopThunk = [JitterSpan](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const glm::vec4& Position, const int Index)
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
				Painter->Colors[Index] = glm::vec4(Average, 1.0);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				delete Painter->Scratch;
				Painter->Scratch = nullptr;

				Painter->MeshingComplete = Clock::now();
				Painter->ReadyDelay = Painter->MeshingComplete - Painter->MeshingStart;

				Painter->MeshReady.store(true);
			};

			MeshingJitterLoopTask = new TaskT(Painter, Evaluator, Painter->Positions, LoopThunk, DoneThunk);
			MeshingAverageNormalLoopTask->NextTask = MeshingJitterLoopTask;
		}

		Scheduler::EnqueueParallel(MeshingVertexLoopTask);
	}
}


void MeshingJob::Done()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();
	if (Painter && Evaluator)
	{
		void* JobPtr = (void*)this;
		fmt::print("[{}] Parallel tasks started.\n", JobPtr);
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


void MeshingFaceLoop::Run()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();

	if (Painter && Evaluator)
	{
		MeshingScratch* Scratch = Painter->Scratch;

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
			}
			else
			{
				break;
			}
		}
	}
}


void MeshingFaceLoop::Exhausted()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();

	if (Painter && Evaluator)
	{
		MeshingScratch* Scratch = Painter->Scratch;

		isosurface::mesh& Mesh = Scratch->OutputMesh;

		Painter->Positions.reserve(Mesh.vertices_.size());
		Painter->Normals.resize(Mesh.vertices_.size(), glm::vec4(0.0, 0.0, 0.0, 0.0));
		Painter->Indices.resize(Mesh.faces_.size() * 3, 0);

		for (const isosurface::point_t& ExtractedVertex : Mesh.vertices_)
		{
			glm::vec4 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z, 1.0);
			Painter->Positions.push_back(Vertex);
		}

		if (NextTask)
		{
			Scheduler::EnqueueParallel(NextTask);
			NextTask = nullptr;
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