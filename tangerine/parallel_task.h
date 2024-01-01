
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

#pragma once

#include "scheduler.h"
#include "sdf_model.h"
#include "profiling.h"
#include <fmt/format.h>


struct ParallelTaskChain : ParallelTask
{
	ParallelTask* NextTask = nullptr;

	virtual ~ParallelTaskChain()
	{
		if (NextTask)
		{
			delete NextTask;
		}
	}
};


template<typename ContainerT>
struct ParallelDomainTaskChain : ParallelTaskChain
{
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

	ParallelDomainTaskChain(const char* InTaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain)
		: PainterWeakRef(InPainter)
		, EvaluatorWeakRef(InEvaluator)
		, Domain(&InDomain)
		, TaskName(InTaskName)
	{
	}

	ParallelDomainTaskChain(const char* InTaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator)
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
struct ParallelLambdaDomainTaskChain : ParallelDomainTaskChain<ContainerT>
{
	using ElementT = typename ContainerT::value_type;

	using BootThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(DrawableShared&, SDFOctreeShared&, ElementT&, const int)>;
	using DoneThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	bool HasBootThunk;

	ParallelLambdaDomainTaskChain(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(false)
	{
	}

	ParallelLambdaDomainTaskChain(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, ContainerT& InDomain, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<ContainerT>(TaskName, InPainter, InEvaluator, InDomain)
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


struct ParallelLambdaOctreeTaskChain : ParallelDomainTaskChain<SDFOctree>
{
	using ElementT = typename SDFOctree::value_type;

	using BootThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;
	using LoopThunkT = std::function<void(DrawableShared&, SDFOctreeShared&, ElementT&)>;
	using DoneThunkT = std::function<void(DrawableShared&, SDFOctreeShared&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	ParallelLambdaOctreeTaskChain(const char* TaskName, DrawableShared& InPainter, SDFOctreeShared& InEvaluator, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<SDFOctree>(TaskName, InPainter, InEvaluator)
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
