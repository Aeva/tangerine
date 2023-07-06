
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

#include <atomic>
#include <functional>
#include <algorithm>


struct AsyncTask
{
	virtual void Run() = 0;
	virtual void Done() = 0;
	virtual void Abort() = 0;

	virtual ~AsyncTask()
	{
	}

	// This will be set by Scheduler::Enqueue.
	bool Unstoppable = false;
};


struct ParallelTask
{
	virtual void Run() = 0;
	virtual ParallelTask* Fork() = 0;

	virtual ~ParallelTask()
	{
	}
};


struct ContinuousTask
{
	virtual bool Run() = 0;

	virtual ~ContinuousTask()
	{
	}
};


struct DeleteTask
{
	virtual void Run() = 0;

	virtual ~DeleteTask()
	{
	}
};


using FinalizerThunk = std::function<void()>;


namespace Scheduler
{
	int GetThreadIndex();
	int GetThreadPoolSize();

	void Setup(const bool ForceSingleThread);
	void Teardown();
	void Advance();
	void DropEverything();

	std::atomic_bool& GetState();

	bool Live();
	void Enqueue(AsyncTask* Task, bool Unstoppable = false);
	void Enqueue(ParallelTask* Task);
	void Enqueue(ContinuousTask* Task);

	void EnqueueDelete(DeleteTask* Task);
	void EnqueueDelete(FinalizerThunk Finalizer);

	void Stats(size_t& InboxLoad, size_t& OutboxLoad, size_t& ParallelLoad, size_t& ContinuousLoad, size_t& DeleteLoad);
}


template<typename DerivedT, typename ContainerT, typename... ExtraArgsT>
struct ParallelIterationTask : ParallelTask
{
	using IteratorT = ContainerT::iterator;
	using ElementT = ContainerT::value_type;

	struct TaskShared
	{
		std::atomic_int RefCount = 1;
		std::atomic_int NextSlice = 0;
		std::vector<std::pair<IteratorT, IteratorT> > Slices;
	};

	TaskShared* Shared = nullptr;

	ParallelIterationTask(ContainerT* Domain)
	{
		Shared = new TaskShared();

		const int SliceCount = Scheduler::GetThreadPoolSize();
		int MaxSliceSize = (Domain->size() + SliceCount - 1) / SliceCount;

		Shared->Slices.resize(SliceCount);

		IteratorT SliceStart = Domain->begin();
		for (int GroupIndex = 0; GroupIndex < SliceCount; ++GroupIndex)
		{
			int StartIndex = GroupIndex * MaxSliceSize;
			int EndIndex = std::min(StartIndex + MaxSliceSize, int(Domain->size()));
			int SliceSpan = EndIndex - StartIndex;
			IteratorT SliceEnd = std::next(SliceStart, SliceSpan);
			Shared->Slices.push_back({ SliceStart, SliceEnd });
			SliceStart = SliceEnd;
		}
	}

	virtual ParallelTask* Fork()
	{
		Shared->RefCount.fetch_add(1);
		return new DerivedT(*dynamic_cast<DerivedT*>(this));
	}

	virtual ~ParallelIterationTask()
	{
#if 0

		const int ThisRef = Shared->RefCount.fetch_sub(1);
		if (ThisRef == 1)
		{
			Complete();
			delete Shared;
		}
#endif
	}

	virtual void RunInner(ExtraArgsT... ExtraArgs)
	{
		const int SliceIndex = Shared->NextSlice.fetch_add(1);
		if (SliceIndex < Shared->Slices.size())
		{
			IteratorT Cursor = Shared->Slices[SliceIndex].first;
			IteratorT SliceEnd = Shared->Slices[SliceIndex].second;
			for (; Cursor != SliceEnd; ++Cursor)
			{
				LoopThunk(*Cursor, ExtraArgs...);
			}
		}

		const int ThisRef = Shared->RefCount.fetch_sub(1);
		if (ThisRef == 1)
		{
			Complete();
			delete Shared;
		}
	}

	virtual void Run() = 0;

	virtual void LoopThunk(const ElementT& Element, ExtraArgsT... ExtraArgs) = 0;

	virtual void Complete()
	{
	}
};
