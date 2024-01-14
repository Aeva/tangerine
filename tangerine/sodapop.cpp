
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
#include "parallel_task.h"
#include "profiling.h"
#include "tangerine.h"
#include "sdf_model.h"
#include "material.h"
#include "mesh_generators.h"
#include <fmt/format.h>
#include <surface_nets.h>
#include <concepts>
#include <iterator>
#include <atomic>
#include <mutex>
#include <set>
#include <array>
#include <random>
#include <numbers>
#include <algorithm>
#include <tuple>


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


struct MeshingScratch
{
	DrawableShared Painter;
	SDFOctreeShared Evaluator;

	// Intermediary domain for MeshingOctreeTask
	std::vector<SDFOctree*> Incompletes;

	static std::unique_ptr<MeshingScratch> Create(DrawableShared& Painter, SDFOctreeShared& Evaluator)
	{
		std::unique_ptr<MeshingScratch> Intermediary(new MeshingScratch(Painter, Evaluator));
		return Intermediary;
	}

	virtual ~MeshingScratch()
	{
	};

protected:
	MeshingScratch(DrawableShared& InPainter, SDFOctreeShared& InEvaluator)
		: Painter(InPainter)
		, Evaluator(InEvaluator)
	{
		SDFOctree::CallbackType IncompleteSearch = [&](SDFOctree& Leaf)
		{
			if (Leaf.Incomplete)
			{
				Incompletes.push_back(&Leaf);
			}
		};

		BeginEvent("Evaluator::Walk IncompleteSearch");
		Evaluator->Walk(IncompleteSearch);
		EndEvent();
	}
};


struct NaiveSurfaceNetsScratch : MeshingScratch
{
	isosurface::AsyncParallelSurfaceNets Ext;

	glm::vec3 JitterSpan;

	// Intermediary domain for MeshingVertexLoopTask
	size_t PointCacheBucketSize = 0;
	std::vector<PointCacheBucket> PointCache;

	// Crit used by MeshingNormalLoopTask
	std::mutex NormalsCS;

	static std::unique_ptr<NaiveSurfaceNetsScratch> Create(DrawableShared& Painter, SDFOctreeShared& Evaluator, float MeshingDensity)
	{
		std::unique_ptr<NaiveSurfaceNetsScratch> Intermediary(new NaiveSurfaceNetsScratch(Painter, Evaluator, MeshingDensity));
		return Intermediary;
	}

	virtual ~NaiveSurfaceNetsScratch()
	{
	};

protected:
	NaiveSurfaceNetsScratch(DrawableShared& InPainter, SDFOctreeShared& InEvaluator, float MeshingDensity)
		: MeshingScratch(InPainter, InEvaluator)
	{
		isosurface::regular_grid_t& Grid = Ext.Grid;
		{
			const float Density = glm::floor(MeshingDensity);
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
		JitterSpan = glm::vec3(Grid.dx, Grid.dy, Grid.dz) * glm::vec3(0.5);
	}
};


struct MeshingJob : AsyncTask
{
	DrawableWeakRef PainterWeakRef;
	SDFNodeWeakRef EvaluatorWeakRef;
	float NaiveSurfaceNetsDensity;

	virtual void Run();
	virtual void Done();
	virtual void Abort();

	void DebugOctree(DrawableShared& Painter, SDFOctreeShared& Evaluator);
	void NaiveSurfaceNets(DrawableShared& Painter, SDFOctreeShared& Evaluator);
	void SphereLatticeSearch(DrawableShared& Painter, SDFOctreeShared& Evaluator);
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


void Sodapop::Populate(DrawableShared Painter, float MeshingDensityPush)
{
	ProfileScope Fnord("Sodapop::Populate");

	MeshingJob* Task = new MeshingJob();
	Task->PainterWeakRef = Painter;
	Task->EvaluatorWeakRef = Painter->Evaluator;
	Task->NaiveSurfaceNetsDensity = DefaultMeshingDensity + MeshingDensityPush;

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
			}
		}
	}

	if (Painter && Evaluator)
	{
		Assert(!Evaluator->Bounds.Degenerate());
		Assert(Evaluator->Bounds.Volume() > 0);
		Painter->MeshingFrameStart = GetFrameNumber();

#if 1
		switch (Painter->MeshingAlgorithm)
		{
#if 1
		case MeshingAlgorithms::NaiveSurfaceNets:
			return NaiveSurfaceNets(Painter, Evaluator);
#endif
		case MeshingAlgorithms::SphereLatticeSearch:
			return SphereLatticeSearch(Painter, Evaluator);
		default:
			return DebugOctree(Painter, Evaluator);
		}
#else
		return SphereLatticeSearch(Painter, Evaluator);
#endif
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

		Painter->MeshingFrameComplete = GetFrameNumber();
		Painter->MeshingFrameLatency = Painter->MeshingFrameComplete - Painter->MeshingFrameStart;

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


template<bool NormalsPopulated, bool ColorsPopluated>
void ApplyVertexSequence(DrawableShared& Painter)
{
	if (Painter->VertexOrderHint == VertexSequence::Shuffle)
	{
		std::vector<size_t> Sequence;

		size_t HalfPoint = Painter->Positions.size() / 2;
		size_t MirrorPoint;
		if ((Painter->Positions.size() % 2) == 0)
		{
			MirrorPoint = HalfPoint;
		}
		else
		{
			MirrorPoint = HalfPoint + 1;
		}

		Sequence.reserve(MirrorPoint);

		for (size_t SwapIndex = 0; SwapIndex < HalfPoint; ++SwapIndex)
		{
			Sequence.push_back(MirrorPoint + SwapIndex);
		}

		std::random_device RandomDevice;
		std::mt19937 RandomGenerator(RandomDevice());
		RandomGenerator.seed(0);
		std::shuffle(Sequence.begin(), Sequence.end(), RandomGenerator);

		if (MirrorPoint != HalfPoint)
		{
			Sequence.push_back(HalfPoint);
		}

		std::vector<size_t> Exchange;
		Exchange.resize(Painter->Positions.size(), -1);

		for (size_t TargetIndex = 0; TargetIndex < Sequence.size(); ++TargetIndex)
		{
			size_t SwapIndex = Sequence[TargetIndex];
			Assert(TargetIndex != SwapIndex || TargetIndex == HalfPoint);

			Exchange[TargetIndex] = SwapIndex;
			Exchange[SwapIndex] = TargetIndex;

			std::swap(Painter->Positions[TargetIndex], Painter->Positions[SwapIndex]);

			if (NormalsPopulated)
			{
				std::swap(Painter->Normals[TargetIndex], Painter->Normals[SwapIndex]);
			}

			if (ColorsPopluated)
			{
				std::swap(Painter->Colors[TargetIndex], Painter->Colors[SwapIndex]);
			}
		}

		for (size_t IndexIndex = 0; IndexIndex < Painter->Indices.size(); ++IndexIndex)
		{
			Painter->Indices[IndexIndex] = Exchange[Painter->Indices[IndexIndex]];
		}
	}
}


void MeshingJob::DebugOctree(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	ParallelTaskChain<MeshingScratch>* MeshingOctreeTask;
	{
		using DomainT = std::vector<SDFOctree*>;
		using TaskT = ParallelLambdaDomainTaskChain<MeshingScratch, DomainT>;
		TaskT::LoopThunkT LoopThunk = [](MeshingScratch& Intermediary, SDFOctree* Incomplete, const int Index)
		{
			Incomplete->Populate(false, 3, -1);
		};

		TaskT::DoneThunkT DoneThunk = [](MeshingScratch& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
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
			}
		};

		TaskT::AccessorT Accessor = [](MeshingScratch& Intermediary)
		{
			return &Intermediary.Incompletes;
		};

		{
			std::unique_ptr<MeshingScratch> InitialIntermediary = MeshingScratch::Create(Painter, Evaluator);
			MeshingOctreeTask = new TaskT("Populate Octree", InitialIntermediary, Accessor, LoopThunk, DoneThunk);
		}
	}

	ParallelTaskChain<MeshingScratch>* OctreeMeshDataTask;
	{
		using TaskT = ParallelLambdaOctreeTaskChain<MeshingScratch>;

		TaskT::BootThunkT BootThunk = [](MeshingScratch& Intermediary)
		{
		};

		TaskT::LoopThunkT LoopThunk = [](MeshingScratch& Intermediary, SDFOctree& LeafNode)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& RootNode = Intermediary.Evaluator;
			if (Painter && RootNode)
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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](MeshingScratch& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				ApplyVertexSequence<true, false>(Painter);
			}
		};

		TaskT::AccessorT Accessor = [](MeshingScratch& Intermediary)
		{
			return Intermediary.Evaluator.get();
		};

		OctreeMeshDataTask = new TaskT("Populate Octree Mesh Data", Accessor, BootThunk, LoopThunk, DoneThunk);
		MeshingOctreeTask->NextTask = OctreeMeshDataTask;
	}

	ParallelTaskChain<MeshingScratch>* MaterialAssignmentTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<MeshingScratch, std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](MeshingScratch& Intermediary, const glm::vec4& Position, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](MeshingScratch& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				Scheduler::EnqueueOutbox(new MeshingComplete(Painter));
			}
		};

		TaskT::AccessorT Accessor = [](MeshingScratch& Intermediary)
		{
			return &Intermediary.Painter->Positions;
		};

		MaterialAssignmentTask = new TaskT("Material Assignment", Accessor, LoopThunk, DoneThunk);
		OctreeMeshDataTask->NextTask = MaterialAssignmentTask;
	}

	Scheduler::EnqueueParallel(MeshingOctreeTask);
}


void MeshingJob::NaiveSurfaceNets(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingOctreeTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, std::vector<SDFOctree*>>;
		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, SDFOctree* Incomplete, const int Index)
		{
			Incomplete->Populate(false, 3, -1);
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Evaluator)
			{
				BeginEvent("Evaluator::LinkLeaves");
				Evaluator->LinkLeaves();
				EndEvent();

				{
					SDFOctree* EvaluatorRaw = Evaluator.get();
					Intermediary.Ext.ImplicitFunction = [EvaluatorRaw](float X, float Y, float Z) -> float
					{
						// Clamp to prevent INFs from turning into NaNs elsewhere.
						return glm::clamp(EvaluatorRaw->Eval(glm::vec3(X, Y, Z), false), -100.0f, 100.0f);
					};
				}
				Intermediary.Ext.Setup();

				if (Intermediary.Ext.FirstLoopDomain.size() == 0)
				{
					return;
				}
			}
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.Incompletes;
		};

		{
			std::unique_ptr<NaiveSurfaceNetsScratch> InitialIntermediary = NaiveSurfaceNetsScratch::Create(Painter, Evaluator, NaiveSurfaceNetsDensity);
			MeshingOctreeTask = new TaskT("Populate Octree", InitialIntermediary, Accessor, LoopThunk, DoneThunk);
		}
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingPointCacheTask;
	{
		using TaskT = ParallelLambdaOctreeTaskChain<NaiveSurfaceNetsScratch>;

		TaskT::BootThunkT BootThunk = [](auto& Intermediary)
		{
			isosurface::regular_grid_t& Grid = Intermediary.Ext.Grid;
			size_t IndexRange = Grid.sx * Grid.sy * Grid.sz;
			const size_t BucketSize = glm::min(size_t(64), IndexRange);
			const size_t BucketCount = ((IndexRange + BucketSize - 1) / BucketSize);

			Intermediary.PointCacheBucketSize = BucketSize;
			Intermediary.PointCache.resize(BucketCount);
		};

		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, SDFOctree& LeafNode)
		{
			isosurface::regular_grid_t& Grid = Intermediary.Ext.Grid;
			const size_t Range = Intermediary.PointCacheBucketSize;
			std::vector<PointCacheBucket>& PointCache = Intermediary.PointCache;

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

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			BeginEvent("Pruning");
			std::vector<PointCacheBucket> Pruned;
			Pruned.reserve(Intermediary.PointCache.size());

			for (PointCacheBucket& Bucket : Intermediary.PointCache)
			{
				if (Bucket.Points.size() > 0)
				{
					Pruned.emplace_back(std::move(Bucket));
				}
			}

			Intermediary.PointCache.swap(Pruned);
			EndEvent();
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return Intermediary.Evaluator.get();
		};

		MeshingPointCacheTask = new TaskT("Populate Point Cache", Accessor, BootThunk, LoopThunk, DoneThunk);
		MeshingOctreeTask->NextTask = MeshingPointCacheTask;
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingVertexLoopTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, std::vector<PointCacheBucket>>;

		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, const PointCacheBucket& Bucket, const int Ignore)
		{
			for (size_t GridIndex : Bucket.Points)
			{
				Intermediary.Ext.FirstLoopInnerThunk(Intermediary.Ext, IndexToGridPoint(Intermediary.Ext.Grid, GridIndex));
			}
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.PointCache;
		};

		MeshingVertexLoopTask = new TaskT("Vertex Loop", Accessor, LoopThunk, DoneThunk);
		MeshingPointCacheTask->NextTask = MeshingVertexLoopTask;
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingFaceLoopTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, std::unordered_map<std::size_t, std::uint64_t>>;

		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, const std::pair<std::size_t const, std::uint64_t>& Element, const int Ignore)
		{
			Intermediary.Ext.SecondLoopThunk(Intermediary.Ext, Element);
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				isosurface::mesh& Mesh = Intermediary.Ext.OutputMesh;

				Painter->Positions.reserve(Mesh.vertices_.size());
				Painter->Normals.resize(Mesh.vertices_.size(), glm::vec4(0.0, 0.0, 0.0, 0.0));
				Painter->Indices.resize(Mesh.faces_.size() * 3, 0);

				for (const isosurface::point_t& ExtractedVertex : Mesh.vertices_)
				{
					glm::vec4 Vertex(ExtractedVertex.x, ExtractedVertex.y, ExtractedVertex.z, 1.0);
					Painter->Positions.push_back(Vertex);
				}
			}
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.Ext.SecondLoopDomain;
		};

		MeshingFaceLoopTask = new TaskT("Face Loop", Accessor, LoopThunk, DoneThunk);
		MeshingVertexLoopTask->NextTask = MeshingFaceLoopTask;
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingNormalLoopTask;
	{
		using FaceT = isosurface::mesh::triangle_t;
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, isosurface::mesh::faces_container_type>;
		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, const FaceT& Face, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				isosurface::mesh& Mesh = Intermediary.Ext.OutputMesh;

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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
#if USE_GRADIENT_NORMALS
				ApplyVertexSequence<true, false>(Painter);
#else
				ApplyVertexSequence<false, false>(Painter);
#endif
			}
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.Ext.OutputMesh.faces_;
		};

		MeshingNormalLoopTask = new TaskT("Normal Loop", Accessor, LoopThunk, DoneThunk);
		MeshingFaceLoopTask->NextTask = MeshingNormalLoopTask;
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingAverageNormalLoopTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, glm::vec4& Normal, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				Painter->Colors.resize(Painter->Positions.size(), glm::vec4(0.0, 0.0, 0.0, 1.0));
			}
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.Painter->Normals;
		};

		MeshingAverageNormalLoopTask = new TaskT("Average Normals", Accessor, LoopThunk, DoneThunk);
		MeshingNormalLoopTask->NextTask = MeshingAverageNormalLoopTask;
	}

	ParallelTaskChain<NaiveSurfaceNetsScratch>* MeshingJitterLoopTask;
	{
		using TaskT = ParallelLambdaDomainTaskChain<NaiveSurfaceNetsScratch, std::vector<glm::vec4>>;

		TaskT::LoopThunkT LoopThunk = [](auto& Intermediary, const glm::vec4& Position, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](auto& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				Scheduler::EnqueueOutbox(new MeshingComplete(Painter));
			}
		};

		TaskT::AccessorT Accessor = [](auto& Intermediary)
		{
			return &Intermediary.Painter->Positions;
		};

		MeshingJitterLoopTask = new TaskT("Jitter Loop", Accessor, LoopThunk, DoneThunk);
		MeshingAverageNormalLoopTask->NextTask = MeshingJitterLoopTask;
	}

	Scheduler::EnqueueParallel(MeshingOctreeTask);
}


// This is used to translate lattice addresses into spatial coordinates.
static const glm::vec3 UnitLatticeOffset(1.f, 1.f, glm::sqrt(2));


// Like %, but behaves as if the values are on a continuous positive number line.
template<typename T>
static T Sequence(T Number, int Period)
{
	return (Number % Period + Period) % Period;
}


static glm::ivec3 Sequence(glm::ivec3 Number, int Period)
{
	return glm::ivec3(
		Sequence(Number.x, Period),
		Sequence(Number.y, Period),
		Sequence(Number.z, Period));
}


enum class LatticeAddressType
{
	Invalid = 0,
	Padding,
	Cell,
	Edge,
	Vertex,
};


struct LatticeGrid
{
	// For converting between lattice indices and coordinates.
	glm::vec3 Translation;
	glm::ivec3 Min;
	glm::ivec3 Max;
	glm::ivec3 Range;
	size_t AddressRange;

	// Returns true if the provided lattice address is in range.
	bool IsValidAddress(size_t Address) const
	{
		return Address < AddressRange;
	}

	// Returns true if the provided lattice index tripple is in range.
	bool IsValidIndex(glm::ivec3 Indices) const
	{
		return glm::all(glm::lessThanEqual(Min, Indices)) && glm::all(glm::lessThanEqual(Indices, Max));
	}

	// Convert a lattice index tripple to a coordinate.
	glm::vec3 GetCoord(glm::ivec3 Indices) const
	{
		return glm::vec3(Indices) * Translation;
	}

	// Convert a flat index into a lattice index tripple.
	glm::ivec3 UnpackAddress(size_t Address) const
	{
		const int SliceStride = Range.x * Range.y;
		glm::ivec3 Indices = Min;
		Indices.x += (Address % SliceStride) % Range.x;
		Indices.y += (Address % SliceStride) / Range.x;
		Indices.z += Address / SliceStride;
		return Indices;
	}

	// Convert a lattice index tripple into a flat index.
	size_t PackAddress(glm::ivec3 Indices) const
	{
		Indices = glm::clamp(Indices, Min, Max) - Min;
		size_t Address = size_t(Indices.x);
		Address += size_t(Indices.y) * size_t(Range.x);
		Address += size_t(Indices.z) * size_t(Range.x) * size_t(Range.y);
		return Address;
	}

	// Convert a flat index to a coordinate.
	glm::vec3 GetCoord(size_t Address) const
	{
		return GetCoord(UnpackAddress(Address));
	}

	LatticeAddressType GetAddressType(glm::ivec3 Indices) const
	{
		const glm::ivec3 Mod2 = Sequence(Indices, 2);
		const glm::ivec3 Mod4 = Sequence(Indices, 4);
		const glm::ivec3 CellPhase(Mod4);
		const glm::ivec3 EdgePhase(Mod4.xy, Mod2.z);
		const glm::ivec3 VertPhase(Mod4);

		const auto Match = [](const glm::ivec3& Phase, const glm::ivec3 Match) -> bool
		{
			return glm::all(glm::equal(Phase, Match));
		};

		const bool IsCell = \
			Match(CellPhase, glm::ivec3(2, 2, 0)) ||
			Match(CellPhase, glm::ivec3(0, 0, 2));

		const bool IsEdge = \
			Match(EdgePhase, glm::ivec3(2, 0, 0)) ||
			Match(EdgePhase, glm::ivec3(0, 2, 0)) ||
			Match(EdgePhase, glm::ivec3(1, 1, 1)) ||
			Match(EdgePhase, glm::ivec3(3, 1, 1)) ||
			Match(EdgePhase, glm::ivec3(1, 3, 1)) ||
			Match(EdgePhase, glm::ivec3(3, 3, 1));

		const bool IsVert = \
			Match(VertPhase, glm::ivec3(0, 0, 0)) ||
			Match(VertPhase, glm::ivec3(2, 0, 1)) ||
			Match(VertPhase, glm::ivec3(0, 2, 1)) ||
			Match(VertPhase, glm::ivec3(2, 2, 2)) ||
			Match(VertPhase, glm::ivec3(2, 0, 3)) ||
			Match(VertPhase, glm::ivec3(0, 2, 3));

		if (IsCell)
		{
			Assert(!IsEdge);
			Assert(!IsVert);
			return LatticeAddressType::Cell;
		}
		else if (IsEdge)
		{
			Assert(!IsCell);
			Assert(!IsVert);
			return LatticeAddressType::Edge;
		}
		else if (IsVert)
		{
			Assert(!IsCell);
			Assert(!IsEdge);
			return LatticeAddressType::Vertex;
		}
		else
		{
			return LatticeAddressType::Padding;
		}
	}

	LatticeAddressType GetAddressType(size_t Address) const
	{
		if (IsValidAddress(Address))
		{
			return GetAddressType(UnpackAddress(Address));
		}
		else
		{
			return LatticeAddressType::Invalid;
		}
	}

	using WalkNeighborsThunkT = std::function<void(int Side, size_t EdgeAddress, size_t NeighborAddress)>;

	void WalkNeighbors(glm::ivec3 CellIndex, WalkNeighborsThunkT& Thunk) const
	{
		Assert(GetAddressType(CellIndex) == LatticeAddressType::Cell);

		const glm::ivec3 EdgeOffsets[12] =
		{
			{-1, -1, -1},
			{ 1, -1, -1},
			{-1,  1, -1},
			{ 1,  1, -1},

			{ 0, -2,  0},
			{-2,  0,  0},
			{ 2,  0,  0},
			{ 0,  2,  0},

			{-1, -1,  1},
			{ 1, -1,  1},
			{-1,  1,  1},
			{ 1,  1,  1}
		};

		for (int Side = 0; Side < 12; ++Side)
		{
			glm::ivec3 Edge = EdgeOffsets[Side] + CellIndex;
			glm::ivec3 Neighbor = EdgeOffsets[Side] * 2 + CellIndex;
			if (IsValidIndex(Neighbor))
			{
				Assert(IsValidIndex(Edge));
				Assert(GetAddressType(Edge) == LatticeAddressType::Edge);
				Assert(GetAddressType(Neighbor) == LatticeAddressType::Cell);
				Thunk(Side, PackAddress(Edge), PackAddress(Neighbor));
			}
		}
	}

	using FaceVerticesThunkT = std::function<void(size_t Cellddress, glm::ivec3 A, glm::ivec3 B, glm::ivec3 C, glm::ivec3 D)>;

	void GetVerticesForEdge(glm::ivec3 CellIndex, int Face, FaceVerticesThunkT& Thunk) const
	{
		const int A = 2;
		const int B = 1;
		const int C = 2;
		const int Z = 0;

		size_t CellAddress = PackAddress(CellIndex);
		Assert(GetAddressType(CellAddress) == LatticeAddressType::Cell);

		switch(Face)
		{
		case 0:
			// -X -Y -Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, -C),
				CellIndex + glm::ivec3(-A, -A, -Z),
				CellIndex + glm::ivec3( Z, -A, -B),
				CellIndex + glm::ivec3(-A,  Z, -B));
			break;

		case 1:
			// +X -Y -Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, -C),
				CellIndex + glm::ivec3(+A, -A, -Z),
				CellIndex + glm::ivec3(+A,  Z, -B),
				CellIndex + glm::ivec3( Z, -A, -B));
			break;

		case 2:
			// -X +Y -Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, -C),
				CellIndex + glm::ivec3(-A, +A,  Z),
				CellIndex + glm::ivec3(-A,  Z, -B),
				CellIndex + glm::ivec3( Z, +A, -B));
			break;

		case 3:
			// +X +Y -Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, -C),
				CellIndex + glm::ivec3(+A, +A, -Z),
				CellIndex + glm::ivec3( Z, +A, -B),
				CellIndex + glm::ivec3(+A,  Z, -B));
			break;

		case 4:
			// -Y
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3(-A, -A,  Z),
				CellIndex + glm::ivec3(+A, -A,  Z),
				CellIndex + glm::ivec3(+Z, -A, -B),
				CellIndex + glm::ivec3(+Z, -A, +B));
			break;

		case 5:
			// -X
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3(-A, +A,  Z),
				CellIndex + glm::ivec3(-A, -A,  Z),
				CellIndex + glm::ivec3(-A,  Z, -B),
				CellIndex + glm::ivec3(-A,  Z, +B));
			break;

		case 6:
			// +X
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3(+A, -A,  Z),
				CellIndex + glm::ivec3(+A, +A,  Z),
				CellIndex + glm::ivec3(+A, +Z, -B),
				CellIndex + glm::ivec3(+A, +Z, +B));
			break;

		case 7:
			// +Y
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3(+A, +A,  Z),
				CellIndex + glm::ivec3(-A, +A,  Z),
				CellIndex + glm::ivec3( Z, +A, -B),
				CellIndex + glm::ivec3( Z, +A, +B));
			break;

		case 8:
			// -X -Y +Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, +C),
				CellIndex + glm::ivec3(-A, -A,  Z),
				CellIndex + glm::ivec3(-A,  Z, +B),
				CellIndex + glm::ivec3( Z, -A, +B));
			break;

		case 9:
			// +X -Y +Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, +C),
				CellIndex + glm::ivec3(+A, -A,  Z),
				CellIndex + glm::ivec3( Z, -A, +B),
				CellIndex + glm::ivec3(+A,  Z, +B));
			break;

		case 10:
			// -X +Y +Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, +C),
				CellIndex + glm::ivec3(-A, +A,  Z),
				CellIndex + glm::ivec3( Z, +A, +B),
				CellIndex + glm::ivec3(-A,  Z, +B));
			break;

		case 11:
			// +X +Y +Z
			Thunk(
				CellAddress,
				CellIndex + glm::ivec3( Z,  Z, +C),
				CellIndex + glm::ivec3(+A, +A,  Z),
				CellIndex + glm::ivec3(+A,  Z, +B),
				CellIndex + glm::ivec3( Z, +A, +B));
			break;

		default:
			Assert(Face > 0 && Face < 12);
			break;
		}
	}

	LatticeGrid(float Radius, AABB EvalBounds)
	{
		Translation = UnitLatticeOffset * (Radius * .5f);
		AABB MeshingBounds = EvalBounds + Translation * 3.f;
		Min = glm::ivec3(glm::ceil(glm::abs(MeshingBounds.Min) / Translation) * glm::sign(MeshingBounds.Min));
		Max = glm::ivec3(glm::ceil(glm::abs(MeshingBounds.Max) / Translation) * glm::sign(MeshingBounds.Max));
		Range = Max - Min + 1;
		AddressRange = size_t(Range.x) * size_t(Range.y) * size_t(Range.z);

		// These assertions confirm how the cells are aligned within the grid.
		// Cell centers only occur on even numbered layers, and the XY indices of each cell are 4 apart.
		Assert(GetAddressType(glm::ivec3(2, 2, 0)) == LatticeAddressType::Cell);
		Assert(GetAddressType(glm::ivec3(4, 2, 0)) == LatticeAddressType::Edge);
		Assert(GetAddressType(glm::ivec3(6, 2, 0)) == LatticeAddressType::Cell);
		Assert(GetAddressType(glm::ivec3(0, 0, 2)) == LatticeAddressType::Cell);
		Assert(GetAddressType(glm::ivec3(2, 0, 2)) == LatticeAddressType::Edge);
		Assert(GetAddressType(glm::ivec3(4, 0, 2)) == LatticeAddressType::Cell);
	}
};


struct LatticeScratch : MeshingScratch
{
	// The XY grid size for a lattice defined as a tightly packed set of spheres.
	const float Diameter;
	const float Radius;

	// A virtual extended sphere is used to prevent coverage gaps when querying the distance field.
	const float ExtendedDiameter;
	const float ExtendedRadius;

	const LatticeGrid Grid;

	SequenceGenerator Sequence;

	struct CellInfo
	{
		size_t Address;
		uint32_t ActiveFaces;
	};

	ParallelAccumulator<CellInfo> CellAccumulator;

	struct Rhombus
	{
		size_t EdgeAddress;
		glm::vec3 A;
		glm::vec3 B;
		glm::vec3 C;
		glm::vec3 D;
	};
	std::vector<Rhombus> ActiveFaces;
	ParallelAccumulator<Rhombus> FaceAccumulator;

	MeshGenerator MeshInProgress;

	static std::unique_ptr<LatticeScratch> Create(DrawableShared& Painter, SDFOctreeShared& Evaluator, float Density = 16.0f)
	{
		std::unique_ptr<LatticeScratch> Intermediary(new LatticeScratch(Painter, Evaluator, Density));
		return Intermediary;
	}

	virtual ~LatticeScratch()
	{
	}

protected:
	LatticeScratch(DrawableShared& InPainter, SDFOctreeShared& InEvaluator, float Density)
		: MeshingScratch(InPainter, InEvaluator)
		, Diameter(1.f / Density)
		, Radius(Diameter * .5f)
		, ExtendedDiameter(Diameter * glm::sqrt(2.f))
		, ExtendedRadius(Radius * glm::sqrt(2.f))
		, Grid(Radius, Evaluator->Bounds)
	{
		Sequence.Reset(Grid.AddressRange);
		CellAccumulator.Reset();
		FaceAccumulator.Reset();
	}
};


void MeshingJob::SphereLatticeSearch(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	ParallelTaskBuilder<LatticeScratch> Chain;

	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, std::vector<SDFOctree*>>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, SDFOctree* Incomplete, const int Index)
		{
			Incomplete->Populate(false, 3, -1);
		};

		TaskT::DoneThunkT DoneThunk = [](LatticeScratch& Intermediary)
		{
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Evaluator)
			{
				BeginEvent("Evaluator::LinkLeaves");
				Evaluator->LinkLeaves();
				EndEvent();
			}
		};

		TaskT::AccessorT Accessor = [](LatticeScratch& Intermediary)
		{
			return &Intermediary.Incompletes;
		};

		{
			std::unique_ptr<LatticeScratch> InitialIntermediary = LatticeScratch::Create(Painter, Evaluator);
			Chain.Link(new TaskT("Populate Octree", InitialIntermediary, Accessor, LoopThunk, DoneThunk));
		}
	}

	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, SequenceGenerator>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, const size_t Address, const int Ignore)
		{
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			const LatticeGrid& Grid = Intermediary.Grid;

			// TODO: the vast majority of `Address` values here do not correspond to cells.
			if (Evaluator && Grid.GetAddressType(Address) == LatticeAddressType::Cell)
			{
				glm::vec3 Point = Grid.GetCoord(Address);
				SDFInterpreter* CellInterpreter = Evaluator->SelectInterpreter(Point, Intermediary.ExtendedRadius, false);
				if (CellInterpreter)
				{
					const float CellDist = CellInterpreter->Eval(Point);
					bool MatchedRequiredNeighbor = false;
					uint32_t ActiveFaces = 0;

					const float SplitDist = 0.001f;

					if (CellDist < SplitDist)
					{
						LatticeGrid::WalkNeighborsThunkT WalkThunk = [&](int Face, size_t EdgeAddress, size_t NeighborAddress) -> void
						{
							float AdjacentDist = std::numeric_limits<float>::infinity();
							glm::vec3 AdjacentPoint = Grid.GetCoord(Address);
							SDFInterpreter* AdjacentInterpreter = Evaluator->SelectInterpreter(AdjacentPoint, Intermediary.ExtendedRadius, false);
							if (AdjacentInterpreter)
							{
								AdjacentDist = Evaluator->Eval(Grid.UnpackAddress(NeighborAddress), Intermediary.ExtendedRadius);
							}
							if (AdjacentDist >= SplitDist)
							{
								MatchedRequiredNeighbor = true;
								ActiveFaces |= (1 << Face);
							}
						};
						Grid.WalkNeighbors(Grid.UnpackAddress(Address), WalkThunk);
					}

					if (MatchedRequiredNeighbor && ActiveFaces != 0)
					{
						Intermediary.CellAccumulator.Push({ Address, ActiveFaces });
					}
				}
			}
		};

		TaskT::DoneThunkT DoneThunk = [](LatticeScratch& Intermediary)
		{
		};

		TaskT::AccessorT Accessor = [](LatticeScratch& Intermediary)
		{
			return &Intermediary.Sequence;
		};

		Chain.Link(new TaskT("Find Active Cells", Accessor, LoopThunk, DoneThunk));
	}

	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, ParallelAccumulator<LatticeScratch::CellInfo>>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, const LatticeScratch::CellInfo& Cell, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			const LatticeGrid& Grid = Intermediary.Grid;
			if (Painter && Evaluator)
			{
				const glm::ivec3 CellIndex = Grid.UnpackAddress(Cell.Address);

				LatticeGrid::FaceVerticesThunkT VertexThunk = [&](size_t EdgeAddress, glm::ivec3 A, glm::ivec3 B, glm::ivec3 C, glm::ivec3 D) -> void
				{
					LatticeAddressType FoundA = Grid.GetAddressType(A);
					LatticeAddressType FoundB = Grid.GetAddressType(B);
					LatticeAddressType FoundC = Grid.GetAddressType(C);
					LatticeAddressType FoundD = Grid.GetAddressType(D);
					Assert(FoundA == LatticeAddressType::Vertex);
					Assert(FoundB == LatticeAddressType::Vertex);
					Assert(FoundC == LatticeAddressType::Vertex);
					Assert(FoundD == LatticeAddressType::Vertex);

					LatticeScratch::Rhombus Face = { EdgeAddress, Grid.GetCoord(A), Grid.GetCoord(B), Grid.GetCoord(C), Grid.GetCoord(D) };
					Intermediary.FaceAccumulator.Push(Face);
				};

				LatticeGrid::WalkNeighborsThunkT WalkThunk = [&](int Face, size_t EdgeAddress, size_t NeighborAddress) -> void
				{
					int32_t Mask = 1 << Face;
					if (Mask & Cell.ActiveFaces)
					{
						Grid.GetVerticesForEdge(Grid.UnpackAddress(Cell.Address), Face, VertexThunk);
					}
				};
				Grid.WalkNeighbors(CellIndex, WalkThunk);
			}
		};

		TaskT::DoneThunkT DoneThunk = [](LatticeScratch& Intermediary)
		{
			Intermediary.FaceAccumulator.Join(Intermediary.ActiveFaces);
			Intermediary.FaceAccumulator.Reset();

			for (const LatticeScratch::Rhombus& Rhombus : Intermediary.ActiveFaces)
			{
				const glm::vec3& AcuteLeft = Rhombus.A;
				const glm::vec3& AcuteRight = Rhombus.B;
				const glm::vec3& ObtuseBottom = Rhombus.C;
				const glm::vec3& ObtuseTop = Rhombus.D;
				Intermediary.MeshInProgress.Accumulate(AcuteLeft);
				Intermediary.MeshInProgress.Accumulate(ObtuseBottom);
				Intermediary.MeshInProgress.Accumulate(ObtuseTop);
				Intermediary.MeshInProgress.Accumulate(ObtuseTop);
				Intermediary.MeshInProgress.Accumulate(ObtuseBottom);
				Intermediary.MeshInProgress.Accumulate(AcuteRight);
			}

			Assert(Intermediary.Painter != nullptr);
			std::swap(Intermediary.Painter->Positions, Intermediary.MeshInProgress.Vertices);
			std::swap(Intermediary.Painter->Indices, Intermediary.MeshInProgress.Indices);
			Intermediary.Painter->Normals.resize(Intermediary.Painter->Positions.size());
			Intermediary.Painter->Colors.resize(Intermediary.Painter->Positions.size());
		};

		TaskT::AccessorT Accessor = [](LatticeScratch& Intermediary)
		{
			return &Intermediary.CellAccumulator;
		};

		Chain.Link(new TaskT("Find Active Lattice Edges", Accessor, LoopThunk, DoneThunk));
	}

	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, glm::vec4& Normal, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
			{
				Normal = glm::vec4(Evaluator->Gradient(Painter->Positions[Index].xyz()), 1.0);
			}
		};

		TaskT::DoneThunkT DoneThunk = [](LatticeScratch& Intermediary)
		{
		};

		TaskT::AccessorT Accessor = [](LatticeScratch& Intermediary)
		{
			return &Intermediary.Painter->Normals;
		};

		Chain.Link(new TaskT("Populate Normals", Accessor, LoopThunk, DoneThunk));
	}

#if 0
	// This produces nicer contours, but a worse vertex distribution.
	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, glm::vec4& Position, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
			{
				if (Evaluator->Eval(Position.xyz()) >= 0.f)
				{
					glm::vec3 Normal = Intermediary.Painter->Normals[Index].xyz();
					RayHit Hit = Evaluator->Evaluator->RayMarch(Position.xyz(), -Normal);
					if (Hit.Hit && Hit.Travel < Intermediary.ExtendedRadius)
					{
						Position = glm::vec4(Hit.Position, 1.f);
					}
				}
			}
		};

		TaskT::DoneThunkT DoneThunk = [](LatticeScratch& Intermediary)
		{
		};

		TaskT::AccessorT Accessor = [](LatticeScratch& Intermediary)
		{
			return &Intermediary.Painter->Positions;
		};

		Chain.Link(new TaskT("Shrink Wrap Mesh", Accessor, LoopThunk, DoneThunk));
	}
#endif

	{
		using TaskT = ParallelLambdaDomainTaskChain<LatticeScratch, std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](LatticeScratch& Intermediary, const glm::vec4& Position, const int Index)
		{
			DrawableShared& Painter = Intermediary.Painter;
			SDFOctreeShared& Evaluator = Intermediary.Evaluator;
			if (Painter && Evaluator)
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
			}
		};

		TaskT::DoneThunkT DoneThunk = [](MeshingScratch& Intermediary)
		{
			DrawableShared& Painter = Intermediary.Painter;
			if (Painter)
			{
				Scheduler::EnqueueOutbox(new MeshingComplete(Painter));
			}
		};

		TaskT::AccessorT Accessor = [](MeshingScratch& Intermediary)
		{
			return &Intermediary.Painter->Positions;
		};

		Chain.Link(new TaskT("Material Assignment", Accessor, LoopThunk, DoneThunk));
	}

	Chain.Run();
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
