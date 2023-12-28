
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
		Painter->MeshingFrameStart = GetFrameNumber();

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
			ApplyVertexSequence<true, false>(Painter);
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
#if USE_GRADIENT_NORMALS
			ApplyVertexSequence<true, false>(Painter);
#else
			ApplyVertexSequence<false, false>(Painter);
#endif
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


enum class LatticeSymbol
{
	B, // Unambigously empty space
	A, // Ambigously empty space
	X, // Interior
	I, // Invalid
};


// The FCC lattice coordinate offset between layers when the diameter is 1.
static const glm::vec3 UnitLatticeOffset(1.f, 1.f, glm::sqrt(2));


// The relative offset between a FCC lattice sphere and it's neighbors when the diameter is 1.
static const std::array<glm::vec3, 12> UnitLatticeNeighbors = \
{
	// the layer below
	UnitLatticeOffset * glm::vec3(-1.f, -1.f, -1.f),
	UnitLatticeOffset * glm::vec3( 1.f, -1.f, -1.f),
	UnitLatticeOffset * glm::vec3(-1.f,  1.f, -1.f),
	UnitLatticeOffset * glm::vec3( 1.f,  1.f, -1.f),

	// same layer
	glm::vec3( 0, -1, 0),
	glm::vec3(-1,  0, 0),
	glm::vec3( 1,  0, 0),
	glm::vec3( 0,  1, 0),

	// the layer above
	UnitLatticeOffset * glm::vec3(-1.f, -1.f, 1.f),
	UnitLatticeOffset * glm::vec3( 1.f, -1.f, 1.f),
	UnitLatticeOffset * glm::vec3(-1.f,  1.f, 1.f),
	UnitLatticeOffset * glm::vec3( 1.f,  1.f, 1.f)
};


struct LatticeParameters
{
	// The XY grid size for a lattice defined as a tightly packed set of spheres.
	const float Diameter;
	const float Radius;

	// A virtual extended sphere is used to prevent coverage gaps when querying the distance field.
	const float ExtendedDiameter;
	const float ExtendedRadius;

	// The coordinate offset between layers.
	const glm::vec3 LayerOffset;

	// The relative offset between a sphere and it's connected neighbors.
	const std::array<glm::vec3, 12> Neighbors;

	static std::array<glm::vec3, 12> PopulateNeighbors(const float Diameter)
	{
		std::array<glm::vec3, 12> ScaledLatticeNeighbors;
		for (size_t Index = 0; Index < 12; ++Index)
		{
			ScaledLatticeNeighbors[Index] = UnitLatticeNeighbors[Index] * Diameter;
		}
		return ScaledLatticeNeighbors;
	}

	LatticeParameters(const float Density)
		: Diameter(1.f / Density)
		, Radius(Diameter * .5f)
		, ExtendedDiameter(Diameter * glm::sqrt(2.f))
		, ExtendedRadius(Radius * glm::sqrt(2.f))
		, LayerOffset(UnitLatticeOffset * Radius)
		, Neighbors(PopulateNeighbors(Diameter))
	{
	}
};


struct LatticeSample
{
	const glm::vec3 Center;
	float Dist = 0.f;
	LatticeSymbol Symbol = LatticeSymbol::I;

	LatticeSample(const glm::vec3 Point, const LatticeParameters& Lattice, SDFOctreeShared& Evaluator)
		: Center(Point)
	{
		const float Epsilon = 0.001f;

		Dist = Evaluator->Eval(Point, false);
		if (glm::isinf(Dist) || glm::isnan(Dist))
		{
			Symbol = LatticeSymbol::I;
		}
		else if (Dist <= Epsilon)
		{
			Symbol = LatticeSymbol::X;
		}
		else if (Dist > Lattice.ExtendedRadius)
		{
			Symbol = LatticeSymbol::B;
		}
		else
		{
			Symbol = LatticeSymbol::A;
		}
	}
};


struct Plane
{
	glm::vec3 Pivot;
	glm::vec3 Normal;
};


struct LatticeCell : public LatticeSample
{
	const LatticeParameters Lattice;
	std::vector<LatticeSample> Neighbors;
	std::vector<Plane> SurfacePlanes;
	std::vector<size_t> SurfaceNeighbors;

	LatticeCell(const glm::vec3 Point, const LatticeParameters InLattice, SDFOctreeShared& Evaluator)
		: LatticeSample(Point, InLattice, Evaluator)
		, Lattice(InLattice)
	{
		if (IsAmbiguous())
		{
			Neighbors.reserve(Lattice.Neighbors.size());
			for (const glm::vec3 Offset : Lattice.Neighbors)
			{
				glm::vec3 Other = Point + Offset;
				Neighbors.emplace_back(Other, Lattice, Evaluator);
			}
			SurfacePlanes.reserve(Neighbors.size());
			SurfaceNeighbors.reserve(Neighbors.size());

			for (size_t NeighborIndex = 0; NeighborIndex < Neighbors.size(); ++NeighborIndex)
			{
				const LatticeSample& Other = Neighbors[NeighborIndex];

				if (Symbol == LatticeSymbol::X)
				{
					if (Other.Symbol == LatticeSymbol::A || Other.Symbol == LatticeSymbol::B)
					{
						const glm::vec3 RayStart = Other.Center;
						const glm::vec3 RayDir = glm::normalize(Center - Other.Center);

						const float MaxTravel = glm::distance(Center, Other.Center);
						const float MinTravel = MaxTravel - Lattice.ExtendedRadius;

						RayHit Hit = Evaluator->Evaluator->RayMarch(RayStart, RayDir);
						if (Hit.Hit && Hit.Travel >= MinTravel && Hit.Travel <= MaxTravel)
						{
							Plane& Surfel = SurfacePlanes.emplace_back();
							Surfel.Pivot = Hit.Position;
							Surfel.Normal = Evaluator->Gradient(Hit.Position);
							SurfaceNeighbors.push_back(NeighborIndex);
						}
					}
				}
				else if (Symbol == LatticeSymbol::A)
				{
					if (Other.Symbol == LatticeSymbol::X)
					{
						const glm::vec3 RayStart = Center;
						const glm::vec3 RayDir = glm::normalize(Other.Center - Center);

						const float MaxTravel = Lattice.ExtendedRadius;
						const float MinTravel = Dist;

						RayHit Hit = Evaluator->Evaluator->RayMarch(RayStart, RayDir);
						if (Hit.Hit && Hit.Travel >= MinTravel && Hit.Travel <= MaxTravel)
						{
							Plane& Surfel = SurfacePlanes.emplace_back();
							Surfel.Pivot = Hit.Position;
							Surfel.Normal = Evaluator->Gradient(Hit.Position);
							SurfaceNeighbors.push_back(NeighborIndex);
						}
					}
				}
			}
		
			if (SurfacePlanes.size() > 0)
			{
				Assert(SurfacePlanes.size() == SurfaceNeighbors.size());
			}
		}
	}

	bool IsValid() const
	{
		return Symbol != LatticeSymbol::I && Neighbors.size() > 0;
	}

	bool IsAmbiguous() const
	{
		return Symbol == LatticeSymbol::A || Symbol == LatticeSymbol::X;
	}
};


struct LessVec3
{
	bool operator()(const glm::vec3& L, const glm::vec3& R) const
	{
		if (L.z < R.z)
		{
			return true;
		}
		else if (L.z == R.z)
		{
			if (L.y < R.y)
			{
				return true;
			}
			else if (L.y == R.y)
			{
				return L.x < R.x;
			}
		}

		return false;
	}
};


struct MeshBuilder
{
	std::vector<glm::vec4> Vertices;
	std::vector<uint32_t> Indices;
	std::map<glm::vec3, uint32_t, LessVec3> Memo;

	void Accumulate(glm::vec3 Vertex)
	{
		auto [Index, Outcome] = Memo.insert({ Vertex, uint32_t(Vertices.size()) });
		if (Outcome)
		{
			Vertices.push_back(glm::vec4(Vertex, 1.0));
		}
		Indices.push_back(Index->second);
	}
};


struct UnitRhombicDodecahedronBuilder : public MeshBuilder
{
	UnitRhombicDodecahedronBuilder()
	{
		// These are relative to a *radius* of 1
		const float A = 1.f;
		const float B = glm::sqrt(2.f) / 2.f;
		const float C = glm::sqrt(2.f);
		const float Z = 0.f;

		// -Y
		Rhombus(
			glm::vec3(-A, -A,  Z),
			glm::vec3(+A, -A,  Z),
			glm::vec3(+Z, -A, -B),
			glm::vec3(+Z, -A, +B));

		// +X
		Rhombus(
			glm::vec3(+A, -A,  Z),
			glm::vec3(+A, +A,  Z),
			glm::vec3(+A, +Z, -B),
			glm::vec3(+A, +Z, +B));

		// +Y
		Rhombus(
			glm::vec3(+A, +A,  Z),
			glm::vec3(-A, +A,  Z),
			glm::vec3( Z, +A, -B),
			glm::vec3( Z, +A, +B));

		// -X
		Rhombus(
			glm::vec3(-A, +A,  Z),
			glm::vec3(-A, -A,  Z),
			glm::vec3(-A,  Z, -B),
			glm::vec3(-A,  Z, +B));

		// -X -Y +Z
		Rhombus(
			glm::vec3( Z,  Z, +C),
			glm::vec3(-A, -A,  Z),
			glm::vec3(-A,  Z, +B),
			glm::vec3( Z, -A, +B));

		// +X -Y +Z
		Rhombus(
			glm::vec3( Z,  Z, +C),
			glm::vec3(+A, -A,  Z),
			glm::vec3( Z, -A, +B),
			glm::vec3(+A,  Z, +B));

		// +X +Y +Z
		Rhombus(
			glm::vec3( Z,  Z, +C),
			glm::vec3(+A, +A,  Z),
			glm::vec3(+A,  Z, +B),
			glm::vec3( Z, +A, +B));

		// -X +Y +Z
		Rhombus(
			glm::vec3( Z,  Z, +C),
			glm::vec3(-A, +A,  Z),
			glm::vec3( Z, +A, +B),
			glm::vec3(-A,  Z, +B));

		// -X -Y -Z
		Rhombus(
			glm::vec3( Z,  Z, -C),
			glm::vec3(-A, -A, -Z),
			glm::vec3( Z, -A, -B),
			glm::vec3(-A,  Z, -B));

		// +X -Y -Z
		Rhombus(
			glm::vec3( Z,  Z, -C),
			glm::vec3(+A, -A, -Z),
			glm::vec3(+A,  Z, -B),
			glm::vec3( Z, -A, -B));

		// +X +Y -Z
		Rhombus(
			glm::vec3( Z,  Z, -C),
			glm::vec3(+A, +A, -Z),
			glm::vec3( Z, +A, -B),
			glm::vec3(+A,  Z, -B));

		// -X +Y -Z
		Rhombus(
			glm::vec3( Z,  Z, -C),
			glm::vec3(-A, +A,  Z),
			glm::vec3(-A,  Z, -B),
			glm::vec3( Z, +A, -B));
	}


	void Rhombus(glm::vec3 AcuteLeft, glm::vec3 AcuteRight, glm::vec3 ObtuseBottom, glm::vec3 ObtuseTop)
	{
		Accumulate(AcuteLeft);
		Accumulate(ObtuseBottom);
		Accumulate(ObtuseTop);
		Accumulate(ObtuseTop);
		Accumulate(ObtuseBottom);
		Accumulate(AcuteRight);
	}
};


static void SphereLatticeSearchInner(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	const float Density = 16.f;
	const LatticeParameters Lattice(Density);

	// Unscientific search bounds.
	const AABB Bounds = Evaluator->Bounds + Lattice.ExtendedRadius;

	// This records cells that may generate mesh data.
	std::vector<LatticeCell> CellsOfInterest;

	const glm::vec3 Origin = Bounds.Min;
	glm::vec3 Cursor = Origin;
	bool Even = true;
	for (Cursor.z = Origin.z; Cursor.z <= Bounds.Max.z; Cursor.z += Lattice.LayerOffset.z)
	{
		Even = !Even;
		const glm::vec2 LayerJitter = Even ? Lattice.LayerOffset.xy() : glm::vec2(0.f);
		const glm::vec3 LayerOrigin = glm::vec3(Origin.xy(), Cursor.z) + glm::vec3(LayerJitter, 0.f);

		for (Cursor.y = LayerOrigin.y; Cursor.y <= Bounds.Max.y; Cursor.y += Lattice.Diameter)
		{
			for (Cursor.x = LayerOrigin.x; Cursor.x <= Bounds.Max.x; Cursor.x += Lattice.Diameter)
			{
				const LatticeCell& Cell = CellsOfInterest.emplace_back(Cursor, Lattice, Evaluator);
				if (!Cell.IsValid() || !Cell.IsAmbiguous())
				{
					// TODO : parallelize this
					CellsOfInterest.pop_back();
				}
			}
		}
	}

	if (CellsOfInterest.size() > 0)
	{
		static UnitRhombicDodecahedronBuilder UnitHull;

		MeshBuilder Model;

		for (const LatticeCell& Cell : CellsOfInterest)
		{
			for (size_t LocalIndex : UnitHull.Indices)
			{
				const glm::vec4 LocalVertex = UnitHull.Vertices[LocalIndex];
				Model.Accumulate(LocalVertex.xyz() * Cell.Lattice.Radius + Cell.Center);
			}
		}

		if (Model.Indices.size() > 0)
		{
			Painter->Positions = std::move(Model.Vertices);
			Painter->Indices = std::move(Model.Indices);

			for (const glm::vec4& Position : Painter->Positions)
			{
				Painter->Normals.emplace_back(Evaluator->Gradient(Position.xyz()), 1.0f);
			}
		}
	}
}


void MeshingJob::SphereLatticeSearch(DrawableShared& Painter, SDFOctreeShared& Evaluator)
{
	ParallelTaskChain* PopulateLatticeTask;
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

			SphereLatticeSearchInner(Painter, Evaluator);

			Painter->Normals.resize(Painter->Positions.size());
		};

		PopulateLatticeTask = new TaskT("Populate Octree", Painter, Evaluator, Painter->Scratch->Incompletes, LoopThunk, DoneThunk);
	}

	ParallelTaskChain* PopulateNormalsTask;
	{
		using TaskT = MeshingLambdaContainerTask<std::vector<glm::vec4>>;
		TaskT::LoopThunkT LoopThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator, glm::vec4& Normal, const int Index)
		{
			Normal = glm::vec4(Evaluator->Gradient(Painter->Positions[Index].xyz()), 1.0);
		};

		TaskT::DoneThunkT DoneThunk = [](DrawableShared& Painter, SDFOctreeShared& Evaluator)
		{
			Painter->Colors.resize(Painter->Positions.size(), glm::vec4(0.0, 0.0, 0.0, 1.0));
		};

		PopulateNormalsTask = new TaskT("Populate Normals", Painter, Evaluator, Painter->Normals, LoopThunk, DoneThunk);
		PopulateLatticeTask->NextTask = PopulateNormalsTask;
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
		PopulateNormalsTask->NextTask = MaterialAssignmentTask;
	}

	Scheduler::EnqueueParallel(PopulateLatticeTask);
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
