
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

#include "errors.h"
#include "scheduler.h"
#include "profiling.h"
#include "sdf_model.h"
#include "material.h"
#include <fmt/format.h>
#include <surface_nets.h>
#include <concepts>
#include <iterator>
#include <random>
#include <atomic>
#include <mutex>
#include <set>


#define USE_GRADIENT_NORMALS 1


static thread_local std::default_random_engine RNGesus;
static thread_local std::uniform_real_distribution<double> Roll(-1.0, std::nextafter(1.0, DBL_MAX));

static const float DefaultMeshingDensity = 20.0;


size_t GridPointToIndex(const isosurface::regular_grid_t& Grid, size_t GridX, size_t GridY, size_t GridZ)
{
	return GridX + (GridY * Grid.sx) + (GridZ * Grid.sx * Grid.sy);
}


size_t GridPointToIndex(const isosurface::regular_grid_t& Grid, isosurface::AsyncParallelSurfaceNets::GridPoint Point)
{
	return GridPointToIndex(Grid, Point.i, Point.j, Point.k);
}


isosurface::AsyncParallelSurfaceNets::GridPoint IndexToGridPoint(const isosurface::regular_grid_t& Grid, const size_t GridIndex)
{
	isosurface::AsyncParallelSurfaceNets::GridPoint Point;
	Point.i = GridIndex % Grid.sx;
	Point.j = (GridIndex / Grid.sx) % Grid.sy;
	Point.k = GridIndex / (Grid.sx * Grid.sy);
	return Point;
}


struct PointCacheBucket
{
	std::set<size_t> Points;
	std::mutex CS;

	void Insert(size_t Index)
	{
		CS.lock();
		Points.insert(Index);
		CS.unlock();
	}

	PointCacheBucket()
	{
	}

	PointCacheBucket(PointCacheBucket&& Other) noexcept
	{
		Points.swap(Other.Points);
	}
};


struct MeshingScratch : isosurface::AsyncParallelSurfaceNets
{
	int MeshingDensity;

	// Intermediary domain for MeshingOctreeTask
	std::vector<SDFOctree*> Incompletes;

	// Intermediary domain for MeshingVertexLoopTask
	size_t PointCacheBucketSize = 0;
	std::vector<PointCacheBucket> PointCache;

	// Crit used by MeshingNormalLoopTask
	std::mutex NormalsCS;
};


void Sodapop::DeleteMeshingScratch(MeshingScratch* Scratch)
{
	// Ideally SodapopDrawable::~SodapopDrawable would just delete this directly, but
	// that is logistically difficult since the struct is only defined in this file.
	// So instead we have this hack for now.  Ideally MeshingScratch might eventually be
	// replaced with a better thought out scratch space for parallel tasks etc.
	delete Scratch;
}


struct MeshingJob : AsyncTask
{
	SodapopDrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;
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
struct ParallelMeshingTask : ParallelTaskChain
{
	using SharedT = std::shared_ptr<ParallelMeshingTask<ContainerT>>;
	using ElementT = typename ContainerT::value_type;
	using IteratorT = typename ContainerT::iterator;

	SodapopDrawableWeakRef PainterWeakRef;
	SDFOctreeWeakRef EvaluatorWeakRef;

	ContainerT* Domain;
	std::string TaskName;
	bool SetupPending = true;
	int SetupCalled = 0;

	std::mutex IterationCS;
	std::atomic_int NextIndex = 0;
	IteratorT NextIter;
	IteratorT StopIter;
	SDFOctree* NextLeaf = nullptr;

	ParallelMeshingTask(const char* InTaskName, SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(&InDomain)
		, TaskName(InTaskName)
	{
	}

	ParallelMeshingTask(const char* InTaskName, SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(nullptr)
		, TaskName(InTaskName)
	{
	}

	virtual void Setup(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
	}

	virtual void Loop(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int ElementIndex)
	{
	}

	virtual void Done(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
	}

	template<typename ForContainerT>
	requires std::contiguous_iterator<IteratorT>
	void RunInner(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				Setup(Painter, Evaluator);
			}
			IterationCS.unlock();
		}
		while (true)
		{
			const int ClaimedIndex = NextIndex.fetch_add(1);
			const size_t Range = Domain->size();
			if (ClaimedIndex < Range)
			{
				ElementT& Element = (*Domain)[ClaimedIndex];
				Loop(Painter, Evaluator, Element, ClaimedIndex);
			}
			else
			{
				break;
			}
		}
	}

	template<typename ForContainerT>
	requires std::forward_iterator<IteratorT>
	void RunInner(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				NextIter = Domain->begin();
				StopIter = Domain->end();
				Setup(Painter, Evaluator);
			}
			IterationCS.unlock();
		}
		while (true)
		{
			bool ValidIteration = false;
			IteratorT Cursor;
			{
				IterationCS.lock();
				if (NextIter != StopIter)
				{
					ValidIteration = true;
					Cursor = NextIter;
					++NextIter;
				}
				IterationCS.unlock();
			}
			if (ValidIteration)
			{
				Loop(Painter, Evaluator, *Cursor, -1);
			}
			else
			{
				break;
			}
		}
	}

	template<typename ForContainerT>
	requires std::same_as<SDFOctree, ForContainerT>
	void RunInner(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				NextLeaf = Evaluator->Next;
				Setup(Painter, Evaluator);
			}
			IterationCS.unlock();
		}
		while (true)
		{
			SDFOctree* Leaf = nullptr;
			{
				IterationCS.lock();
				Leaf = NextLeaf;
				NextLeaf = Leaf ? Leaf->Next : nullptr;
				IterationCS.unlock();
			}
			if (Leaf)
			{
				Loop(Painter, Evaluator, *Leaf, -1);
			}
			else
			{
				break;
			}
		}
	}

	virtual void Run()
	{
		ProfileScope Fnord(fmt::format("{} (Run)", TaskName));
		SodapopDrawableShared Painter = PainterWeakRef.lock();
		SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();
		if (Painter && Evaluator)
		{
			RunInner<ContainerT>(Painter, Evaluator);
		}
	}

	virtual void Exhausted()
	{
		ProfileScope Fnord(fmt::format("{} (Exhausted)", TaskName));
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


template<typename ContainerT>
struct MeshingLambdaContainerTask : ParallelMeshingTask<ContainerT>
{
	using SharedT = std::shared_ptr<ParallelMeshingTask<ContainerT>>;
	using ElementT = typename ContainerT::value_type;

	using BootThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&, ElementT&, const int)>;
	using DoneThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	bool HasBootThunk;

	MeshingLambdaContainerTask(const char* TaskName, SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(false)
	{
	}

	MeshingLambdaContainerTask(const char* TaskName, SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(true)
	{
	}

	virtual void Setup(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		if (HasBootThunk)
		{
			BootThunk(Painter, Evaluator);
		}
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


struct MeshingLambdaOctreeTask : ParallelMeshingTask<SDFOctree>
{
	using SharedT = std::shared_ptr<ParallelMeshingTask<SDFOctree>>;
	using ElementT = typename SDFOctree::value_type;

	using BootThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&, ElementT&)>;
	using DoneThunkT = std::function<void(SodapopDrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	MeshingLambdaOctreeTask(const char* TaskName, SodapopDrawableShared& InPainter, SDFOctreeShared& InEvaluator, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<SDFOctree>(TaskName, InPainter, InEvaluator)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
	{
	}

	virtual void Setup(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		BootThunk(Painter, Evaluator);
	}

	virtual void Loop(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int Unused)
	{
		LoopThunk(Painter, Evaluator, Element);
	}

	virtual void Done(SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		DoneThunk(Painter, Evaluator);
	}
};


void Sodapop::Populate(SodapopDrawableShared Painter, float MeshingDensityPush)
{
	ProfileScope Fnord("Sodapop::Populate");
	Painter->Scratch = new MeshingScratch();
	Painter->Scratch->MeshingDensity = DefaultMeshingDensity + MeshingDensityPush;

	MeshingJob* Task = new MeshingJob();
	Task->PainterWeakRef = Painter;
	Task->EvaluatorWeakRef = Painter->Evaluator;

	Scheduler::Enqueue(Task);
}


void MeshingJob::Run()
{
	ProfileScope MeshingJobRun("MeshingJob::Run");
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = nullptr;
	if (Painter)
	{
		SDFNodeShared RootNode = EvaluatorWeakRef.lock();
		if (RootNode)
		{
			Painter->EvaluatorOctree = Evaluator = SDFOctree::Create(Painter->Evaluator, .25, false, 3);

			if (Evaluator == nullptr)
			{
				return;
			}

			SDFOctree::CallbackType IncompleteSearch = [&](SDFOctree& Leaf)
			{
				if (Leaf.Incomplete)
				{
					Painter->Scratch->Incompletes.push_back(&Leaf);
				}
			};

			BeginEvent("Evaluator::Walk IncompleteSearch");
			Evaluator->Walk(IncompleteSearch);
			EndEvent();
		}
	}

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
		{
			const float Density = Painter->Scratch->MeshingDensity;
			const glm::vec3 SamplesPerUnit = glm::max(Evaluator->Bounds.Extent() * glm::vec3(Density), glm::vec3(8.0));

			Grid.x = Evaluator->Bounds.Min.x;
			Grid.y = Evaluator->Bounds.Min.y;
			Grid.z = Evaluator->Bounds.Min.z;
			Grid.sx = size_t(glm::ceil(SamplesPerUnit.x));
			Grid.sy = size_t(glm::ceil(SamplesPerUnit.y));
			Grid.sz = size_t(glm::ceil(SamplesPerUnit.z));
			Grid.dx = Evaluator->Bounds.Extent().x / static_cast<float>(Grid.sx);
			Grid.dy = Evaluator->Bounds.Extent().y / static_cast<float>(Grid.sy);
			Grid.dz = Evaluator->Bounds.Extent().z / static_cast<float>(Grid.sz);

			Grid.x -= Grid.dx * 2;
			Grid.y -= Grid.dy * 2;
			Grid.z -= Grid.dz * 2;
			Grid.sx += 3;
			Grid.sy += 3;
			Grid.sz += 3;
		}
		Painter->Scratch->Grid = Grid;

		ParallelTaskChain* MeshingOctreeTask;
		{
			using TaskT = MeshingLambdaContainerTask<std::vector<SDFOctree*>>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, SDFOctree* Incomplete, const int Index)
			{
				Incomplete->Populate(false, 3, -1);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				BeginEvent("Evaluator::LinkLeaves");
				Evaluator->LinkLeaves();
				EndEvent();

				{
					SDFOctree* EvaluatorRaw = Evaluator.get();
					Painter->Scratch->ImplicitFunction = [EvaluatorRaw](float X, float Y, float Z) -> float
					{
						// Clamp to prevent INFs from turning into NaNs elsewhere.
						return glm::clamp(EvaluatorRaw->Eval(glm::vec3(X, Y, Z), false), -100.0f, 100.0f);
					};
				}
				Painter->Scratch->Setup();

				if (Painter->Scratch->FirstLoopDomain.size() == 0)
				{
					return;
				}
			};

			MeshingOctreeTask = new TaskT("Populate Octree", Painter, Evaluator, Painter->Scratch->Incompletes, LoopThunk, DoneThunk);
		}

		ParallelTaskChain* MeshingPointCacheTask;
		{
			using TaskT = MeshingLambdaOctreeTask;

			TaskT::BootThunkT BootThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				MeshingScratch* Scratch = Painter->Scratch;

				isosurface::regular_grid_t& Grid = Scratch->Grid;
				size_t IndexRange = Grid.sx * Grid.sy * Grid.sz;
				const size_t BucketSize = glm::min(size_t(64), IndexRange);
				const size_t BucketCount = ((IndexRange + BucketSize - 1) / BucketSize);

				Scratch->PointCacheBucketSize = BucketSize;
				Scratch->PointCache.resize(BucketCount);
			};

			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& RootNode, SDFOctree& LeafNode)
			{
				MeshingScratch* Scratch = Painter->Scratch;
				isosurface::regular_grid_t& Grid = Scratch->Grid;
				const size_t Range = Scratch->PointCacheBucketSize;
				std::vector<PointCacheBucket>& PointCache = Scratch->PointCache;

				glm::vec3 Origin(Grid.x, Grid.y, Grid.z);
				glm::vec3 Step(Grid.dx, Grid.dy, Grid.dz);
				glm::vec3 Count(Grid.sx, Grid.sy, Grid.sz);

				glm::vec3 AlignedMin = glm::floor(glm::max(glm::vec3(0.0), LeafNode.Bounds.Min - Origin) / Step);
				glm::vec3 AlignedMax = glm::ceil((LeafNode.Bounds.Max - Origin) / Step);

				for (float z = AlignedMin.z; z <= AlignedMax.z; ++z)
				{
					for (float y = AlignedMin.y; y <= AlignedMax.y; ++y)
					{
						for (float x = AlignedMin.x; x <= AlignedMax.x; ++x)
						{
							const size_t Index = GridPointToIndex(Grid, size_t(x), size_t(y), size_t(z));
							const size_t Bin = Index / Range;
							if (Bin < PointCache.size())
							{
								PointCache[Bin].Insert(Index);
							}
						}
					}
				}
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				BeginEvent("Pruning");
				std::vector<PointCacheBucket> Pruned;
				Pruned.reserve(Painter->Scratch->PointCache.size());

				for (PointCacheBucket& Bucket : Painter->Scratch->PointCache)
				{
					if (Bucket.Points.size() > 0)
					{
						Pruned.emplace_back(std::move(Bucket));
					}
				}

				Painter->Scratch->PointCache.swap(Pruned);
				EndEvent();
			};

			MeshingPointCacheTask = new TaskT("Populate Point Cache", Painter, Evaluator, BootThunk, LoopThunk, DoneThunk);
			MeshingOctreeTask->NextTask = MeshingPointCacheTask;
		}

		ParallelTaskChain* MeshingVertexLoopTask;
		{
			using TaskT = MeshingLambdaContainerTask<std::vector<PointCacheBucket>>;

			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const PointCacheBucket& Bucket, const int Ignore)
			{
				MeshingScratch* Scratch = Painter->Scratch;
				for (size_t GridIndex : Bucket.Points)
				{
					Scratch->FirstLoopInnerThunk(*Scratch, IndexToGridPoint(Scratch->Grid, GridIndex));
				}
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
			};

			MeshingVertexLoopTask = new TaskT("Vertex Loop", Painter, Evaluator, Painter->Scratch->PointCache, LoopThunk, DoneThunk);
			MeshingPointCacheTask->NextTask = MeshingVertexLoopTask;
		}

		ParallelTaskChain* MeshingFaceLoopTask;
		{
			using TaskT = MeshingLambdaContainerTask<std::unordered_map<std::size_t, std::uint64_t>>;

			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const std::pair<std::size_t const, std::uint64_t>& Element, const int Ignore)
			{
				MeshingScratch* Scratch = Painter->Scratch;
				Scratch->SecondLoopThunk(*Scratch, Element);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
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
			};

			MeshingFaceLoopTask = new TaskT("Face Loop", Painter, Evaluator, Painter->Scratch->SecondLoopDomain, LoopThunk, DoneThunk);
			MeshingVertexLoopTask->NextTask = MeshingFaceLoopTask;
		}

		ParallelTaskChain* MeshingNormalLoopTask;
		{
			using FaceT = isosurface::mesh::triangle_t;
			using TaskT = MeshingLambdaContainerTask<isosurface::mesh::faces_container_type>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const FaceT& Face, const int Index)
			{
				isosurface::mesh& Mesh = Painter->Scratch->OutputMesh;

				Painter->Indices[Index * 3 + 0] = uint32_t(Face.v0);
				Painter->Indices[Index * 3 + 1] = uint32_t(Face.v1);
				Painter->Indices[Index * 3 + 2] = uint32_t(Face.v2);

#if !USE_GRADIENT_NORMALS
				glm::vec3 A = Painter->Positions[Face.v0].xyz();
				glm::vec3 B = Painter->Positions[Face.v1].xyz();
				glm::vec3 C = Painter->Positions[Face.v2].xyz();
				glm::vec3 AB = glm::normalize(A - B);
				glm::vec3 AC = glm::normalize(A - C);
				glm::vec4 N = glm::vec4(glm::normalize(glm::cross(AB, AC)), 1);

				if (!glm::any(glm::isnan(N)))
				{
					Painter->Scratch->NormalsCS.lock();
					Painter->Normals[Face.v0] += N;
					Painter->Normals[Face.v1] += N;
					Painter->Normals[Face.v2] += N;
					Painter->Scratch->NormalsCS.unlock();
				}
#endif
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
			};

			MeshingNormalLoopTask = new TaskT("Normal Loop", Painter, Evaluator, Painter->Scratch->OutputMesh.faces_, LoopThunk, DoneThunk);
			MeshingFaceLoopTask->NextTask = MeshingNormalLoopTask;
		}

		ParallelTaskChain* MeshingAverageNormalLoopTask;
		{
			using TaskT = MeshingLambdaContainerTask<std::vector<glm::vec4>>;
			TaskT::LoopThunkT LoopThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, glm::vec4& Normal, const int Index)
			{
#if !USE_GRADIENT_NORMALS
				if (Normal.w > 0.0)
				{
					Normal = glm::vec4(glm::normalize(Normal.xyz() / Normal.w), 1.0);
				}
				else
#endif
				{
					Normal = glm::vec4(Evaluator->Gradient(Painter->Positions[Index].xyz()), 1.0);
				}
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				Painter->Colors.resize(Painter->Positions.size(), glm::vec4(0.0, 0.0, 0.0, 1.0));
			};

			MeshingAverageNormalLoopTask = new TaskT("Average Normals", Painter, Evaluator, Painter->Normals, LoopThunk, DoneThunk);
			MeshingNormalLoopTask->NextTask = MeshingAverageNormalLoopTask;
		}

		ParallelTaskChain* MeshingJitterLoopTask;
		{
			using TaskT = MeshingLambdaContainerTask<std::vector<glm::vec4>>;

			glm::vec3 JitterSpan = glm::vec3(Grid.dx, Grid.dy, Grid.dz) * glm::vec3(0.5);

			TaskT::LoopThunkT LoopThunk = [JitterSpan](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator, const glm::vec4& Position, const int Index)
			{
				glm::vec3 Sample = glm::vec3(0.0);
				{
					MaterialShared Material = Painter->EvaluatorOctree->GetMaterial(Position.xyz());
					if (Material)
					{
						auto FoundSlot = Painter->SlotLookup.find(Material);
						if (FoundSlot != Painter->SlotLookup.end())
						{
							size_t SlotIndex = FoundSlot->second;
							MaterialVertexGroup& Slot = Painter->MaterialSlots[SlotIndex];

							Painter->MaterialSlotsCS.lock();
							Slot.Vertices.push_back(Index);
							Painter->MaterialSlotsCS.unlock();
						}

						ChthonicMaterialInterface* ChthonicMaterial = dynamic_cast<ChthonicMaterialInterface*>(Material.get());
						if (ChthonicMaterial != nullptr)
						{
							glm::vec3 Normal = Painter->Normals[Index].xyz();
							Sample = ChthonicMaterial->Eval(Position.xyz(), Normal, Normal).xyz();
						}
					}
				}

				Painter->Colors[Index] = glm::vec4(Sample, 1.0);
			};

			TaskT::DoneThunkT DoneThunk = [](SodapopDrawableShared& Painter, SDFOctreeShared& Evaluator)
			{
				BeginEvent("Delete Scratch Data");
				delete Painter->Scratch;
				EndEvent();

				Painter->Scratch = nullptr;

				Painter->MeshingComplete = Clock::now();
				Painter->ReadyDelay = Painter->MeshingComplete - Painter->MeshingStart;

				Painter->MeshReady.store(true);

				SodapopDrawableWeakRef PainterWeakRef = Painter;

				Scheduler::EnqueueDelete([PainterWeakRef]()
				{
					// TODO: rethink how the outbox queue works to avoid hacks like this.

					SodapopDrawableShared Painter = PainterWeakRef.lock();
					if (Painter)
					{
						fmt::print("Meshing complete: {}\n", Painter->Name);
					}
				});
			};

			MeshingJitterLoopTask = new TaskT("Jitter Loop", Painter, Evaluator, Painter->Positions, LoopThunk, DoneThunk);
			MeshingAverageNormalLoopTask->NextTask = MeshingJitterLoopTask;
		}

		Scheduler::EnqueueParallel(MeshingOctreeTask);
	}
}


void MeshingJob::Done()
{
	SodapopDrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();
	if (Painter && Evaluator)
	{
		void* JobPtr = (void*)this;
		fmt::print("[{}] Parallel tasks started.\n", JobPtr);
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


struct ShaderTask : public ContinuousTask
{
	SDFModelWeakRef ModelWeakRef;
	SodapopDrawableWeakRef PainterWeakRef;

	// These pointers are only safe to access while the model and painter are both locked.
	MaterialVertexGroup* VertexGroup = nullptr;
	MaterialInterface* Material = nullptr;
	ChthonicMaterialInterface* ChthonicMaterial = nullptr;
	PhotonicMaterialInterface* PhotonicMaterial = nullptr;

	ShaderTask(SDFModelShared& Instance, SodapopDrawableShared& Painter, size_t MaterialIndex)
		: ModelWeakRef(Instance)
		, PainterWeakRef(Painter)
	{
		VertexGroup = &(Painter->MaterialSlots[MaterialIndex]);
		Material = VertexGroup->Material.get();
		ChthonicMaterial = dynamic_cast<ChthonicMaterialInterface*>(Material);
		PhotonicMaterial = dynamic_cast<PhotonicMaterialInterface*>(Material);
	}

	int NextUpdate = 0;

	bool Run();
};


void Sodapop::Attach(SDFModelShared& Instance)
{
	SodapopDrawableShared SodapopPainter = std::dynamic_pointer_cast<SodapopDrawable>(Instance->Painter);

	if (SodapopPainter)
	{
		for (size_t MaterialIndex = 0; MaterialIndex < SodapopPainter->MaterialSlots.size(); ++MaterialIndex)
		{
			ShaderTask* Task = new ShaderTask(Instance, SodapopPainter, MaterialIndex);
			Scheduler::Enqueue(Task);
		}
	}
}


MaterialOverride MaterialOverrideMode = MaterialOverride::Off;


void Sodapop::SetMaterialOverrideMode(MaterialOverride Mode)
{
	MaterialOverrideMode = Mode;
}


bool ShaderTask::Run()
{
	SDFModelShared Instance = ModelWeakRef.lock();
	SodapopDrawableShared Painter = PainterWeakRef.lock();

	if (Instance && Painter)
	{
		if (Instance->Dirty.load() && Painter->MeshReady.load() && Instance->Visibility != VisibilityStates::Invisible)
		{
			// The model instance requested a repaint and the painter is ready for drawing.

			Instance->SodapopCS.lock();
			if (Instance->Colors.size() == 0)
			{
				Instance->Colors.resize(Painter->Colors.size());
			}
			Instance->SodapopCS.unlock();

			glm::vec4 LocalEye = Instance->ThreadSafeTransform.load() * glm::vec4(Instance->CameraOrigin, 1.0);

			bool NeedsRepaint = false;

			if (MaterialOverrideMode == MaterialOverride::Normals)
			{
				for (int n = 0; n < VertexGroup->Vertices.size(); ++n)
				{
					if (Instance->Drawing.load())
					{
						break;
					}
					const int UpdateIndex = NextUpdate % VertexGroup->Vertices.size();
					NextUpdate = UpdateIndex + 1;

					const int Vertex = VertexGroup->Vertices[UpdateIndex];

					if (Vertex < 0 || Vertex >= Instance->Colors.size())
					{
						// Mitigation attempt for a tricky null access crash, which can be triggered by
						// writing or comparing the color value for the current vertex.  In this situation
						// the instance is not yet visible, the painter's Color array appears to be initialized,
						// but the instance's Color array has a size of zero.
						break;
					}

					glm::vec3 Normal = Painter->Normals[Vertex].xyz();
					glm::vec4 NewColor = MaterialDebugNormals::StaticEval(Normal);

					NeedsRepaint = NeedsRepaint || !glm::all(glm::equal(Instance->Colors[Vertex], NewColor));
					Instance->Colors[Vertex] = NewColor;
				}
			}
			else if (PhotonicMaterial != nullptr)
			{
				for (int n = 0; n < VertexGroup->Vertices.size(); ++n)
				{
					if (Instance->Drawing.load())
					{
						break;
					}
					const int UpdateIndex = NextUpdate % VertexGroup->Vertices.size();
					NextUpdate = UpdateIndex + 1;

					const int Vertex = VertexGroup->Vertices[UpdateIndex];

					if (Vertex < 0 || Vertex >= Instance->Colors.size())
					{
						// Mitigation attempt for a tricky null access crash, which can be triggered by
						// writing or comparing the color value for the current vertex.  In this situation
						// the instance is not yet visible, the painter's Color array appears to be initialized,
						// but the instance's Color array has a size of zero.
						break;
					}

					glm::vec3 Point = Painter->Positions[Vertex].xyz();
					glm::vec3 Normal = Painter->Normals[Vertex].xyz();
					glm::vec3 View;
					glm::vec4 NewColor;

					if (MaterialOverrideMode == MaterialOverride::Invariant)
					{
						View = Normal;
					}
					else
					{
						View = glm::normalize(LocalEye.xyz() - Point);
					}

					{
						// TODO accumulate light
						glm::vec3 Light = glm::vec3(0.0, 0.0, -1.0);
						//glm::vec3 Light = glm::normalize(Point - glm::vec3(0.0, 0.0, 5.0));
						NewColor = PhotonicMaterial->Eval(Point, Normal, View, Light);
					}

					NeedsRepaint = NeedsRepaint || !glm::all(glm::equal(Instance->Colors[Vertex], NewColor));
					Instance->Colors[Vertex] = NewColor;
				}
			}
			else if (ChthonicMaterial != nullptr)
			{
				for (int n = 0; n < VertexGroup->Vertices.size(); ++n)
				{
					if (Instance->Drawing.load())
					{
						break;
					}
					const int UpdateIndex = NextUpdate % VertexGroup->Vertices.size();
					NextUpdate = UpdateIndex + 1;

					const int Vertex = VertexGroup->Vertices[UpdateIndex];

					if (Vertex < 0 || Vertex >= Instance->Colors.size())
					{
						// Mitigation attempt for a tricky null access crash, which can be triggered by
						// writing or comparing the color value for the current vertex.  In this situation
						// the instance is not yet visible, the painter's Color array appears to be initialized,
						// but the instance's Color array has a size of zero.
						break;
					}

					glm::vec3 Point = Painter->Positions[Vertex].xyz();
					glm::vec3 Normal = Painter->Normals[Vertex].xyz();
					glm::vec3 View;
					glm::vec4 NewColor;

					if (MaterialOverrideMode == MaterialOverride::Invariant)
					{
						View = Normal;
					}
					else
					{
						View = glm::normalize(LocalEye.xyz() - Point);
					}

					NewColor = ChthonicMaterial->Eval(Point, Normal, View);

					NeedsRepaint = NeedsRepaint || !glm::all(glm::equal(Instance->Colors[Vertex], NewColor));
					Instance->Colors[Vertex] = NewColor;
				}
			}

			if (NeedsRepaint)
			{
				Scheduler::RequestAsyncRedraw();
			}
			else
			{
				// TODO: model instance doesn't get marked as dirty again.  Also NeedsRepaint isn't a good metric for convergence
				// if this process is ever split up to allow multiple threads working on the same instance, which is planned.
				//Instance->Dirty.store(false);
			}
		}
		return true;
	}
	else
	{
		// One or both of the model instance and painter are invalid now, so kill the task.
		return false;
	}
}
