
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
#include <atomic>
#include <mutex>
#include <set>


#define USE_GRADIENT_NORMALS 1


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
	// Ideally Drawable::~Drawable would just delete this directly, but
	// that is logistically difficult since the struct is only defined in this file.
	// So instead we have this hack for now.  Ideally MeshingScratch might eventually be
	// replaced with a better thought out scratch space for parallel tasks etc.
	delete Scratch;
}


struct MeshingJob : AsyncTask
{
	DrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;
	virtual void Run();
	virtual void Done();
	virtual void Abort();

	void DebugOctree(DrawableShared& Painter, SDFOctreeShared& Evaluator);
	void NaiveSurfaceNets(DrawableShared& Painter, SDFOctreeShared& Evaluator);
};


struct MeshingComplete : AsyncTask
{
	DrawableWeakRef PainterWeakRef;

	MeshingComplete(DrawableShared InPainter)
		: PainterWeakRef(InPainter)
	{
	}

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

	DrawableWeakRef PainterWeakRef;
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

	ParallelMeshingTask(const char* InTaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(&InDomain)
		, TaskName(InTaskName)
	{
	}

	ParallelMeshingTask(const char* InTaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(nullptr)
		, TaskName(InTaskName)
	{
	}

	virtual void Setup(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
	}

	virtual void Loop(DrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int ElementIndex)
	{
	}

	virtual void Done(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
	}

	template<typename ForContainerT>
	requires std::contiguous_iterator<IteratorT>
	void RunInner(DrawableShared& Painter, SDFOctreeShared& Evaluator)
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
	void RunInner(DrawableShared& Painter, SDFOctreeShared& Evaluator)
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
	void RunInner(DrawableShared& Painter, SDFOctreeShared& Evaluator)
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
		DrawableShared Painter = PainterWeakRef.lock();
		SDFOctreeShared Evaluator = EvaluatorWeakRef.lock();
		if (Painter && Evaluator)
		{
			RunInner<ContainerT>(Painter, Evaluator);
		}
	}

	virtual void Exhausted()
	{
		ProfileScope Fnord(fmt::format("{} (Exhausted)", TaskName));
		DrawableShared Painter = PainterWeakRef.lock();
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

	using BootThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(DrawableShared&, SDFOctreeShared&, ElementT&, const int)>;
	using DoneThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	bool HasBootThunk;

	MeshingLambdaContainerTask(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(false)
	{
	}

	MeshingLambdaContainerTask(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(true)
	{
	}

	virtual void Setup(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		if (HasBootThunk)
		{
			BootThunk(Painter, Evaluator);
		}
	}

	virtual void Loop(DrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int ElementIndex)
	{
		LoopThunk(Painter, Evaluator, Element, ElementIndex);
	}

	virtual void Done(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		DoneThunk(Painter, Evaluator);
	}
};


struct MeshingLambdaOctreeTask : ParallelMeshingTask<SDFOctree>
{
	using SharedT = std::shared_ptr<ParallelMeshingTask<SDFOctree>>;
	using ElementT = typename SDFOctree::value_type;

	using BootThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(DrawableShared&, SDFOctreeShared&, ElementT&)>;
	using DoneThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	MeshingLambdaOctreeTask(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelMeshingTask<SDFOctree>(TaskName, InPainter, InEvaluator)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
	{
	}

	virtual void Setup(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		BootThunk(Painter, Evaluator);
	}

	virtual void Loop(DrawableShared& Painter, SDFOctreeShared& Evaluator, ElementT& Element, const int Unused)
	{
		LoopThunk(Painter, Evaluator, Element);
	}

	virtual void Done(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		DoneThunk(Painter, Evaluator);
	}
};


void Sodapop::Populate(DrawableShared Painter, float MeshingDensityPush)
{
	ProfileScope Fnord("Sodapop::Populate");
	Painter->Scratch = new MeshingScratch();
	Painter->Scratch->MeshingDensity = DefaultMeshingDensity + MeshingDensityPush;

	MeshingJob* Task = new MeshingJob();
	Task->PainterWeakRef = Painter;
	Task->EvaluatorWeakRef = Painter->Evaluator;

	Scheduler::EnqueueInbox(Task);
}


void MeshingJob::Run()
{
	ProfileScope MeshingJobRun("MeshingJob::Run");
	DrawableShared Painter = PainterWeakRef.lock();
	SDFOctreeShared Evaluator = nullptr;
	{
		// Create an evaluator octree for meshing, but only populate it enough that the rest can be populated in parallel.
		if (Painter)
		{
			SDFNodeShared RootNode = EvaluatorWeakRef.lock();
			if (RootNode)
			{
				float Margin = 0.0;
				Painter->EvaluatorOctree = Evaluator = SDFOctree::Create(Painter->Evaluator, .25, false, 3, Margin);

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
	}

	if (Painter && Evaluator)
	{
		Assert(!Evaluator->Bounds.Degenerate());
		Assert(Evaluator->Bounds.Volume() > 0);
		Painter->MeshingStart = Clock::now();

		switch (Painter->MeshingAlgorithm)
		{
#if 1
		case MeshingAlgorithms::NaiveSurfaceNets:
			return NaiveSurfaceNets(Painter, Evaluator);
#endif
		default:
			return DebugOctree(Painter, Evaluator);
		}
	}
}


void MeshingComplete::Run()
{
}


void MeshingComplete::Done()
{
	DrawableShared Painter = PainterWeakRef.lock();
	if (Painter)
	{
		ProfileScope MeshingJobRun("MeshingComplete::Done");

		Painter->MeshingComplete = Clock::now();
		Painter->ReadyDelay = Painter->MeshingComplete - Painter->MeshingStart;

		MeshReady(Painter);
	}
}


void MeshingComplete::Abort()
{
}


namespace
{
	const std::vector<glm::vec3> CubeVertices = \
	{
		glm::vec3(-1.0, -1.0, -1.0), // 0 (---)
		glm::vec3(-1.0, -1.0,  1.0), // 1 (--+)
		glm::vec3(-1.0,  1.0, -1.0), // 2 (-+-)
		glm::vec3(-1.0,  1.0,  1.0), // 3 (-++)
		glm::vec3( 1.0, -1.0, -1.0), // 4 (+--)
		glm::vec3( 1.0, -1.0,  1.0), // 5 (+-+)
		glm::vec3( 1.0,  1.0, -1.0), // 6 (++-)
		glm::vec3( 1.0,  1.0,  1.0)  // 7 (+++)
	};

	const std::vector<int> CubeIndices = \
	{
		// -X
		0, 1, 2, 1, 3, 2,

		// +X
		6, 7, 4, 7, 5, 4,

		// -Y
		4, 5, 0, 5, 1, 0,

		// +Y
		2, 3, 6, 3, 7, 6,

		// -Z
		0, 2, 4, 2, 6, 4,

		// +Z
		5, 7, 1, 7, 3, 1
	};
}


void MeshingJob::DebugOctree(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	ParallelTaskChain* MeshingOctreeTask;
	{
		using TaskT = MeshingLambdaContainerTask<std::vector<SDFOctree*>>;
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, SDFOctree* Incomplete, const int Index)
		{
			Incomplete->Populate(false, 3, -1);
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
			BeginEvent("Evaluator::LinkLeaves");
			Evaluator->LinkLeaves();
			EndEvent();
			{
				const int IndexCount = Evaluator->OctreeLeafCount * CubeIndices.size();
				Painter->Indices.resize(IndexCount);
			}
			{
				const int VertexCount = Evaluator->OctreeLeafCount * CubeVertices.size();
				Painter->Positions.resize(VertexCount);
				Painter->Normals.resize(VertexCount);
				Painter->Colors.resize(VertexCount);
			}
		};

		MeshingOctreeTask = new TaskT("Populate Octree", Painter, Evaluator, Painter->Scratch->Incompletes, LoopThunk, DoneThunk);
	}

	ParallelTaskChain* OctreeMeshDataTask;
	{
		using TaskT = MeshingLambdaOctreeTask;

		TaskT::BootThunkT BootThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
		};

		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& RootNode, SDFOctree& LeafNode)
		{
			const int LeafIndex = LeafNode.DebugLeafIndex;
			const int IndexStart = LeafIndex * CubeIndices.size();
			const int VertexStart = LeafIndex * CubeVertices.size();

			for (int i = 0; i < CubeIndices.size(); ++i)
			{
				Painter->Indices[IndexStart + i] = VertexStart + CubeIndices[i];
			}

			const glm::vec3 Center = LeafNode.Bounds.Center();
			const glm::vec3 HalfExtent = LeafNode.Bounds.Extent() * glm::vec3(.5);

			for (int v = 0; v < CubeVertices.size(); ++v)
			{
				const glm::vec3 LocalOffset = CubeVertices[v] * HalfExtent;
				const glm::vec3 Position = Center + LocalOffset;
				const glm::vec3 Normal = LeafNode.Gradient(Position);

				Painter->Positions[VertexStart + v] = glm::vec4(Position, 1.0);
				Painter->Normals[VertexStart + v] = glm::vec4(Normal, 1.0);
			}
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
		};

		OctreeMeshDataTask = new TaskT("Populate Octree Mesh Data", Painter, Evaluator, BootThunk, LoopThunk, DoneThunk);
		MeshingOctreeTask->NextTask = OctreeMeshDataTask;
	}

	ParallelTaskChain* MaterialAssignmentTask;
	{
		using TaskT = MeshingLambdaContainerTask<std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, const glm::vec4& Position, const int Index)
		{
			glm::vec3 Sample = glm::vec3(0.0);
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

			Painter->Colors[Index] = glm::vec4(Sample, 1.0);
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
			Scheduler::EnqueueOutbox(new MeshingComplete(Painter));

			BeginEvent("Delete Scratch Data");
			delete Painter->Scratch;
			EndEvent();

			Painter->Scratch = nullptr;
		};

		MaterialAssignmentTask = new TaskT("Material Assignment", Painter, Evaluator, Painter->Positions, LoopThunk, DoneThunk);
		OctreeMeshDataTask->NextTask = MaterialAssignmentTask;
	}

	Scheduler::EnqueueParallel(MeshingOctreeTask);
}


void MeshingJob::NaiveSurfaceNets(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
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
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, SDFOctree* Incomplete, const int Index)
		{
			Incomplete->Populate(false, 3, -1);
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
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

		TaskT::BootThunkT BootThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
			MeshingScratch* Scratch = Painter->Scratch;

			isosurface::regular_grid_t& Grid = Scratch->Grid;
			size_t IndexRange = Grid.sx * Grid.sy * Grid.sz;
			const size_t BucketSize = glm::min(size_t(64), IndexRange);
			const size_t BucketCount = ((IndexRange + BucketSize - 1) / BucketSize);

			Scratch->PointCacheBucketSize = BucketSize;
			Scratch->PointCache.resize(BucketCount);
		};

		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& RootNode, SDFOctree& LeafNode)
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

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
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

		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, const PointCacheBucket& Bucket, const int Ignore)
		{
			MeshingScratch* Scratch = Painter->Scratch;
			for (size_t GridIndex : Bucket.Points)
			{
				Scratch->FirstLoopInnerThunk(*Scratch, IndexToGridPoint(Scratch->Grid, GridIndex));
			}
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
		};

		MeshingVertexLoopTask = new TaskT("Vertex Loop", Painter, Evaluator, Painter->Scratch->PointCache, LoopThunk, DoneThunk);
		MeshingPointCacheTask->NextTask = MeshingVertexLoopTask;
	}

	ParallelTaskChain* MeshingFaceLoopTask;
	{
		using TaskT = MeshingLambdaContainerTask<std::unordered_map<std::size_t, std::uint64_t>>;

		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, const std::pair<std::size_t const, std::uint64_t>& Element, const int Ignore)
		{
			MeshingScratch* Scratch = Painter->Scratch;
			Scratch->SecondLoopThunk(*Scratch, Element);
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
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
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, const FaceT& Face, const int Index)
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

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
		};

		MeshingNormalLoopTask = new TaskT("Normal Loop", Painter, Evaluator, Painter->Scratch->OutputMesh.faces_, LoopThunk, DoneThunk);
		MeshingFaceLoopTask->NextTask = MeshingNormalLoopTask;
	}

	ParallelTaskChain* MeshingAverageNormalLoopTask;
	{
		using TaskT = MeshingLambdaContainerTask<std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, glm::vec4& Normal, const int Index)
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

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
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

		TaskT::LoopThunkT LoopThunk = [JitterSpan](DrawableShared& Painter, SDFOctreeShared& Evaluator, const glm::vec4& Position, const int Index)
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

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
			Scheduler::EnqueueOutbox(new MeshingComplete(Painter));

			BeginEvent("Delete Scratch Data");
			delete Painter->Scratch;
			EndEvent();

			Painter->Scratch = nullptr;
		};

		MeshingJitterLoopTask = new TaskT("Jitter Loop", Painter, Evaluator, Painter->Positions, LoopThunk, DoneThunk);
		MeshingAverageNormalLoopTask->NextTask = MeshingJitterLoopTask;
	}

	Scheduler::EnqueueParallel(MeshingOctreeTask);
}


void MeshingJob::Done()
{
	DrawableShared Painter = PainterWeakRef.lock();
	SDFNodeShared Evaluator = EvaluatorWeakRef.lock();
	if (Painter && Evaluator)
	{
		void* JobPtr = (void*)this;
	}
	else
	{
		Abort();
	}
}


void MeshingJob::Abort()
{
}


struct ShaderTask : public ContinuousTask
{
	SDFModelWeakRef ModelWeakRef;
	DrawableWeakRef PainterWeakRef;

	// These pointers are only safe to access while the model and painter are both locked.
	InstanceColoringGroup* ColoringGroup = nullptr;
	MaterialInterface* Material = nullptr;
	ChthonicMaterialInterface* ChthonicMaterial = nullptr;
	PhotonicMaterialInterface* PhotonicMaterial = nullptr;

	ShaderTask(SDFModelShared& Instance, DrawableShared& Painter, InstanceColoringGroup* InColoringGroup)
		: ModelWeakRef(Instance)
		, PainterWeakRef(Painter)
		, ColoringGroup(InColoringGroup)
	{
		Material = ColoringGroup->VertexGroup->Material.get();
		ChthonicMaterial = dynamic_cast<ChthonicMaterialInterface*>(Material);
		PhotonicMaterial = dynamic_cast<PhotonicMaterialInterface*>(Material);
	}

	ContinuousTask::Status Run() override;
};


void Sodapop::Attach(SDFModelShared& Instance)
{
	Assert(Instance->Painter != nullptr);

	FlagSceneRepaint();

	for (InstanceColoringGroupUnique& ColoringGroup : Instance->ColoringGroups)
	{
		ShaderTask* Task = new ShaderTask(Instance, Instance->Painter, ColoringGroup.get());
		Scheduler::EnqueueContinuous(Task);
	}
}


static MaterialOverride MaterialOverrideMode = MaterialOverride::Off;


void Sodapop::SetMaterialOverrideMode(MaterialOverride Mode)
{
	if (MaterialOverrideMode != Mode)
	{
		MaterialOverrideMode = Mode;
		FlagSceneRepaint();
	}
}


ContinuousTask::Status ShaderTask::Run()
{
	SDFModelShared Instance = ModelWeakRef.lock();
	DrawableShared Painter = PainterWeakRef.lock();

	if (Instance && Painter)
	{
		if (Instance->Visibility != VisibilityStates::Invisible)
		{
			if (ColoringGroup->StartRepaint())
			{
				return ContinuousTask::Status::Converged;
			}

			std::vector<glm::vec4> Colors;
			Colors.reserve(ColoringGroup->IndexRange);

			glm::mat4 WorldToLocalMatrix = Instance->AtomicWorldToLocal.load();
			glm::vec4 LocalEye = WorldToLocalMatrix * glm::vec4(Instance->AtomicCameraOrigin.load(), 1.0);
			LocalEye /= LocalEye.w;

			std::vector<size_t>& Vertices = ColoringGroup->VertexGroup->Vertices;
			for (size_t RelativeIndex = 0; RelativeIndex < ColoringGroup->IndexRange; ++RelativeIndex)
			{
				const size_t VertexIndex = Vertices[ColoringGroup->IndexStart + RelativeIndex];

				if (MaterialOverrideMode == MaterialOverride::Normals)
				{
					glm::vec3 Normal = Painter->Normals[VertexIndex].xyz();
					glm::vec4 NewColor = MaterialDebugNormals::StaticEval(Normal);
					Colors.push_back(NewColor);
				}
				else if (PhotonicMaterial != nullptr)
				{
					glm::vec3 Point = Painter->Positions[VertexIndex].xyz();
					glm::vec3 Normal = Painter->Normals[VertexIndex].xyz();
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

					Colors.push_back(NewColor);
				}
				else if (ChthonicMaterial != nullptr)
				{
					glm::vec3 Point = Painter->Positions[VertexIndex].xyz();
					glm::vec3 Normal = Painter->Normals[VertexIndex].xyz();
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

					Colors.push_back(NewColor);
				}
			}

			{
				ColoringGroup->ColorCS.lock();
				std::swap(ColoringGroup->Colors, Colors);
				ColoringGroup->ColorCS.unlock();
			}

			Scheduler::RequestAsyncRedraw();
		
			return ContinuousTask::Status::Repainted;
		}
		else
		{
			return ContinuousTask::Status::Skipped;
		}
	}
	else
	{
		// One or both of the model instance and painter are invalid now, so kill the task.
		return ContinuousTask::Status::Remove;
	}
}
