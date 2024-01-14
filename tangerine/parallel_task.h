
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
#include <vector>


template<typename ValueT>
class ParallelAccumulator
{
	using ContainerT = std::vector<ValueT>;
	std::vector<ContainerT> Lanes;

public:
	ParallelAccumulator()
	{
		Reset();
	}
	void Reset()
	{
		Lanes.clear();
		Lanes.resize(Scheduler::GetThreadPoolSize() + 1); // Main thread is 0
	}
	void Push(ValueT Value)
	{
		const size_t ThreadIndex = Scheduler::GetThreadIndex();
		Assert(ThreadIndex < Lanes.size());
		Lanes[ThreadIndex].push_back(Value);
	}
	void Join(ContainerT& Merged)
	{
		size_t TotalSize = 0;
		for (const ContainerT& Lane : Lanes)
		{
			TotalSize += Lane.size();
		}
		Merged.reserve(TotalSize);
		for (const ContainerT& Lane : Lanes)
		{
			for (const ValueT& Value : Lane)
			{
				Merged.emplace_back(Value);
			}
		}
	}
};


class SequenceGenerator
{
	size_t Count;
	std::atomic_size_t Progress;

public:
	using value_type = size_t;
	using iterator = void*;

	SequenceGenerator()
	{
		Reset(0);
	}

	SequenceGenerator(size_t InCount)
	{
		Reset(InCount);
	}

	void Reset(size_t NewCount)
	{
		Count = NewCount;
		Progress = 0;
	}
	
	bool Advance(size_t& OutNext)
	{
		OutNext = Progress.fetch_add(1);
		return OutNext < Count;
	}
};


template<typename IntermediaryT>
struct ParallelTaskChain : ParallelTask
{
	ParallelTaskChain* NextTask = nullptr;
	std::unique_ptr<IntermediaryT> IntermediaryData = nullptr;

	virtual void BatonPass()
	{
		if (NextTask)
		{
			std::swap(NextTask->IntermediaryData, IntermediaryData);
			Scheduler::EnqueueParallel(NextTask);
			NextTask = nullptr;
		}
	}

	virtual ~ParallelTaskChain()
	{
		if (NextTask)
		{
			delete NextTask;
		}
	}
};


template<typename IntermediaryT>
struct ParallelTaskBuilder
{
	ParallelTaskChain<IntermediaryT>* Head = nullptr;
	ParallelTaskChain<IntermediaryT>* Tail = nullptr;

	void Link(ParallelTaskChain<IntermediaryT>* Next)
	{
		if (!Head)
		{
			Head = Next;
			Tail = Next;
		}
		else
		{
			Tail->NextTask = Next;
			Tail = Next;
		}
	}

	void Run()
	{
		Scheduler::EnqueueParallel(Head);
		Head = nullptr;
		Tail = nullptr;
	}
};


template<typename IntermediaryT, typename ContainerT>
struct ParallelDomainTaskChain : ParallelTaskChain<IntermediaryT>
{
	using ElementT = typename ContainerT::value_type;
	using IteratorT = typename ContainerT::iterator;
	using AccessorT = std::function<ContainerT*(IntermediaryT&)>;

	DrawableWeakRef PainterWeakRef;
	SDFOctreeWeakRef EvaluatorWeakRef;

	std::string TaskName;
	AccessorT DomainAccessor;
	bool SetupPending = true;
	int SetupCalled = 0;

	std::mutex IterationCS;
	std::atomic_int NextIndex = 0;
	IteratorT NextIter;
	IteratorT StopIter;
	SDFOctree* NextLeaf = nullptr;

	ParallelDomainTaskChain(const char* InTaskName, std::unique_ptr<IntermediaryT>& InitialIntermediaryData, AccessorT InDomainAccessor)
		: TaskName(InTaskName)
		, DomainAccessor(InDomainAccessor)
	{
		std::swap(ParallelTaskChain<IntermediaryT>::IntermediaryData, InitialIntermediaryData);
	}

	ParallelDomainTaskChain(const char* InTaskName, AccessorT InDomainAccessor)
		: TaskName(InTaskName)
		, DomainAccessor(InDomainAccessor)
	{
	}

	virtual void Setup(IntermediaryT& Intermediary)
	{
	}

	virtual void Loop(IntermediaryT& Intermediary, ElementT& Element, const int ElementIndex)
	{
	}

	virtual void Done(IntermediaryT& Intermediary)
	{
	}

	template<typename ForContainerT>
		requires std::contiguous_iterator<IteratorT>
	void RunInner()
	{
		ContainerT* Domain = DomainAccessor(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				Setup(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
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
				Loop(*ParallelTaskChain<IntermediaryT>::IntermediaryData, Element, ClaimedIndex);
			}
			else
			{
				break;
			}
		}
	}

	template<typename ForContainerT>
		requires std::forward_iterator<IteratorT>
	void RunInner()
	{
		ContainerT* Domain = DomainAccessor(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				NextIter = Domain->begin();
				StopIter = Domain->end();
				Setup(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
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
				Loop(*ParallelTaskChain<IntermediaryT>::IntermediaryData, *Cursor, -1);
			}
			else
			{
				break;
			}
		}
	}

	template<typename ForContainerT>
		requires std::same_as<SequenceGenerator, ForContainerT>
	void RunInner()
	{
		ContainerT* Domain = DomainAccessor(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				Setup(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
			}
			IterationCS.unlock();
		}
		size_t Cursor;
		while (Domain->Advance(Cursor))
		{
			ElementT Element = ElementT(Cursor);
			Loop(*ParallelTaskChain<IntermediaryT>::IntermediaryData, Element, Cursor);
		}
	}

	template<typename ForContainerT>
		requires std::same_as<SDFOctree, ForContainerT>
	void RunInner()
	{
		ContainerT* Domain = DomainAccessor(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
		{
			IterationCS.lock();
			if (SetupPending)
			{
				SetupPending = false;
				NextLeaf = Domain->Next;
				Setup(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
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
				Loop(*ParallelTaskChain<IntermediaryT>::IntermediaryData, *Leaf, -1);
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
		RunInner<ContainerT>();
	}

	virtual void Exhausted()
	{
		ProfileScope Fnord(fmt::format("{} (Exhausted)", TaskName));
		Done(*ParallelTaskChain<IntermediaryT>::IntermediaryData);
		ParallelTaskChain<IntermediaryT>::BatonPass();
	}
};


template<typename IntermediaryT, typename ContainerT>
struct ParallelLambdaDomainTaskChain : ParallelDomainTaskChain<IntermediaryT, ContainerT>
{
	using ElementT = typename ContainerT::value_type;
	using AccessorT = std::function<ContainerT*(IntermediaryT&)>;

	using BootThunkT = std::function<void(IntermediaryT&)>;
	using LoopThunkT = std::function<void(IntermediaryT&, ElementT&, const int)>;
	using DoneThunkT = std::function<void(IntermediaryT&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	bool HasBootThunk;

	ParallelLambdaDomainTaskChain(const char* TaskName, std::unique_ptr<IntermediaryT>& InitialIntermediaryData, AccessorT& InDomainAccessor, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, ContainerT>(TaskName, InitialIntermediaryData, InDomainAccessor)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(false)
	{
	}

	ParallelLambdaDomainTaskChain(const char* TaskName, std::unique_ptr<IntermediaryT>& InitialIntermediaryData, AccessorT& InDomainAccessor, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, ContainerT>(TaskName, InitialIntermediaryData, InDomainAccessor)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(true)
	{
	}

	ParallelLambdaDomainTaskChain(const char* TaskName, AccessorT& InDomainAccessor, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, ContainerT>(TaskName, InDomainAccessor)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(false)
	{
	}

	ParallelLambdaDomainTaskChain(const char* TaskName, AccessorT& InDomainAccessor, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, ContainerT>(TaskName, InDomainAccessor)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
		, HasBootThunk(true)
	{
	}

	virtual void Setup(IntermediaryT& Intermediary)
	{
		if (HasBootThunk)
		{
			BootThunk(Intermediary);
		}
	}

	virtual void Loop(IntermediaryT& Intermediary, ElementT& Element, const int ElementIndex)
	{
		LoopThunk(Intermediary, Element, ElementIndex);
	}

	virtual void Done(IntermediaryT& Intermediary)
	{
		DoneThunk(Intermediary);
	}
};


template<typename IntermediaryT>
struct ParallelLambdaOctreeTaskChain : ParallelDomainTaskChain<IntermediaryT, SDFOctree>
{
	using ContainerT = SDFOctree;
	using ElementT = typename SDFOctree::value_type;
	using AccessorT = std::function<ContainerT* (IntermediaryT&)>;

	using BootThunkT = std::function<void(IntermediaryT&)>;
	using LoopThunkT = std::function<void(IntermediaryT&, ElementT&)>;
	using DoneThunkT = std::function<void(IntermediaryT&)>;

	BootThunkT BootThunk;
	LoopThunkT LoopThunk;
	DoneThunkT DoneThunk;

	ParallelLambdaOctreeTaskChain(const char* TaskName, std::unique_ptr<IntermediaryT>& InitialIntermediaryData, AccessorT& InDomainAccessor, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, SDFOctree>(TaskName, InitialIntermediaryData, InDomainAccessor)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
	{
	}

	ParallelLambdaOctreeTaskChain(const char* TaskName, AccessorT& InDomainAccessor, BootThunkT& InBootThunk, LoopThunkT& InLoopThunk, DoneThunkT& InDoneThunk)
		: ParallelDomainTaskChain<IntermediaryT, SDFOctree>(TaskName, InDomainAccessor)
		, BootThunk(InBootThunk)
		, LoopThunk(InLoopThunk)
		, DoneThunk(InDoneThunk)
	{
	}

	virtual void Setup(IntermediaryT& Intermediary)
	{
		BootThunk(Intermediary);
	}

	virtual void Loop(IntermediaryT& Intermediary, ElementT& Element, const int Unused)
	{
		LoopThunk(Intermediary, Element);
	}

	virtual void Done(IntermediaryT& Intermediary)
	{
		DoneThunk(Intermediary);
	}
};
