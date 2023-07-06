
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

#include "scheduler.h"
#include "errors.h"
#include "sodapop.h"

#include <atomic_queue/atomic_queue.h>
#include <fmt/format.h>

#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <memory>


#ifndef SCHEDULER_QUEUE_SIZE
// The standard queue size is 2*20 entries, or about 4 MB per empty queue.
// This number is set arbitrarily high, as it determines the effective number
// of models that can be operated on by the thread pool at once, and therefore
// determines the maximum number of model instances that can have recurring
// lighting tasks before the system deadlocks.  Should a maximum of ~1 million
// recurring tasks be insufficient some day, raise this to a higher power of 2.
#define SCHEDULER_QUEUE_SIZE 1048576
#endif


using Clock = std::chrono::high_resolution_clock;


template<typename T, unsigned QueueSize = SCHEDULER_QUEUE_SIZE>
struct AtomicQueue
{
	atomic_queue::AtomicQueue<T*, QueueSize> Queue;

	bool TryPush(T* Message)
	{
		return Queue.try_push(Message);
	}

	T* TryPop()
	{
		T* Message = nullptr;
		Queue.try_pop(Message);
		return Message;
	}

	void BlockingPush(T* Message)
	{
		while (!Queue.try_push(Message))
		{
			atomic_queue::spin_loop_pause();
		}
	}

	T* BlockingPop()
	{
		T* Message = nullptr;
		while (!Queue.try_pop(Message))
		{
			atomic_queue::spin_loop_pause();
		}
		return Message;
	}

	size_t RecentCount()
	{
		return Queue.was_size();
	}
};


struct FinalizerTask : public DeleteTask
{
	FinalizerThunk Finalizer;
	virtual void Run()
	{
		Finalizer();
	}
};


struct ParallelTaskProxy
{
	std::shared_ptr<ParallelTask> TaskPrototype;

	ParallelTaskProxy(std::shared_ptr<ParallelTask> InTaskPrototype)
		: TaskPrototype(InTaskPrototype)
	{
	}

	void Run()
	{
		TaskPrototype->Run();
		TaskPrototype.reset();
	}
};


static AtomicQueue<AsyncTask> Inbox;
static AtomicQueue<AsyncTask> Outbox;
static AtomicQueue<ParallelTaskProxy> ParallelQueue;
static AtomicQueue<ContinuousTask> ContinuousQueue;
static AtomicQueue<DeleteTask> DeleteQueue;

static std::atomic_bool State;
static std::atomic_bool PauseThreads;
static std::atomic_int ActiveThreads;

static std::vector<std::thread> Pool;
static thread_local int ThreadIndex = -1;


int Scheduler::GetThreadIndex()
{
	Assert(ThreadIndex > -1);
	return ThreadIndex;
}


int Scheduler::GetThreadPoolSize()
{
	// This is meant to be one thread per reported thread of execution, assuming a dual
	// core processor or better.  The main thread is assumed to be always active, so
	// the thread pool should only occupy the remaining threads.
	static const int ProcessorCountEstimate = std::max(int(std::thread::hardware_concurrency()), 2);
	static const int ThreadPoolSize = ProcessorCountEstimate - 1;
	return ThreadPoolSize;
}


template <bool DedicatedThread>
void WorkerThread(const int InThreadIndex)
{
	if (DedicatedThread)
	{
		ThreadIndex = InThreadIndex;
	}
	else
	{
		Assert(ThreadIndex == InThreadIndex);
	}

	Clock::time_point ThreadStart = Clock::now();

	if (DedicatedThread)
	{
		++ActiveThreads;
	}

	while (State.load())
	{
		if (DedicatedThread && PauseThreads.load())
		{
			--ActiveThreads;
			while (PauseThreads.load())
			{
				std::this_thread::yield();
				continue;
			}
			++ActiveThreads;
			continue;
		}

		if (ParallelTaskProxy* Task = ParallelQueue.TryPop())
		{
			Task->Run();
			delete Task;
		}
		else if (AsyncTask* Task = Inbox.TryPop())
		{
			Task->Run();
			Outbox.BlockingPush(Task);
		}
		else
		{
			if (ContinuousTask* Task = ContinuousQueue.TryPop())
			{
				if (Task->Run())
				{
					ContinuousQueue.BlockingPush(Task);
				}
				else
				{
					delete Task;
				}
			}
			else if (DedicatedThread)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(4));
			}
			else
			{
				std::chrono::duration<double, std::milli> Delta = Clock::now() - ThreadStart;
				if (Delta.count() > 8.0 )
				{
					break;
				}
			}
		}
	}

	if (DedicatedThread)
	{
		--ActiveThreads;
	}
}


std::atomic_bool& Scheduler::GetState()
{
	return State;
}


bool Scheduler::Live()
{
	return State.load();
}


void Scheduler::Enqueue(AsyncTask* Task, bool Unstoppable)
{
	Assert(ThreadIndex == 0);
	Assert(State.load());
	Assert(!PauseThreads.load());
	{
		Task->Unstoppable = Unstoppable;
		Inbox.BlockingPush(Task);
	}
	fmt::print("[{}] New async task\n", (void*)Task);
}


void Scheduler::Enqueue(ContinuousTask* Task)
{
	ContinuousQueue.BlockingPush(Task);
}


void Scheduler::EnqueueDelete(DeleteTask* Task)
{
	DeleteQueue.BlockingPush(Task);
}


void Scheduler::EnqueueDelete(FinalizerThunk Finalizer)
{
	if (ThreadIndex == 0)
	{
		Finalizer();
	}
	else
	{
		FinalizerTask* PendingDelete = new FinalizerTask();
		PendingDelete->Finalizer = Finalizer;
		EnqueueDelete(PendingDelete);
	}
}


static void ParallelTaskDeleter(ParallelTask* TaskPrototype)
{
	TaskPrototype->Exhausted();
	delete TaskPrototype;
}


void Scheduler::EnqueueParallel(ParallelTask* TaskPrototype)
{
	int PoolSize = GetThreadPoolSize();
	std::vector<ParallelTaskProxy*> Pending;
	Pending.reserve(PoolSize);

	std::shared_ptr<ParallelTask> SharedPrototype(TaskPrototype, ParallelTaskDeleter);

	for (int i = 0; i < PoolSize; ++i)
	{
		ParallelTaskProxy* Proxy = new ParallelTaskProxy(SharedPrototype);
		Pending.push_back(Proxy);
	}

	for (ParallelTaskProxy* Proxy : Pending)
	{
		ParallelQueue.BlockingPush(Proxy);
	}
}


void FlushPendingDeletes()
{
	while (DeleteTask* PendingDelete = DeleteQueue.TryPop())
	{
		PendingDelete->Run();
		delete PendingDelete;
	}
}


void Scheduler::Advance()
{
	Assert(ThreadIndex == 0);

	FlushPendingDeletes();

	Assert(State.load());
	Assert(!PauseThreads.load());
	{
		if (Pool.size() == 0)
		{
			WorkerThread<false>(ThreadIndex);
		}

		while (AsyncTask* Task = Outbox.TryPop())
		{
			fmt::print("[{}] Async task complete\n", (void*)Task);
			Task->Done();
			delete Task;
		}
	}
}


void Scheduler::Setup(const bool ForceSingleThread)
{
	Assert(!State.load());
	ThreadIndex = 0; // Main thread;

	State.store(true);

	if (!ForceSingleThread)
	{
		for (int ThreadIndex = 0; ThreadIndex < GetThreadPoolSize(); ++ThreadIndex)
		{
			Pool.emplace_back(WorkerThread<true>, ThreadIndex + 1);
		}
	}
}


void DiscardQueuesInner()
{
	while (DeleteTask* PendingDelete = DeleteQueue.TryPop())
	{
		PendingDelete->Run();
		delete PendingDelete;
	}
	while (ContinuousTask* PendingRepeater = ContinuousQueue.TryPop())
	{
		delete PendingRepeater;
	}
	while (AsyncTask* PendingTask = Inbox.TryPop())
	{
		if (PendingTask->Unstoppable)
		{
			PendingTask->Run();
			PendingTask->Done();
		}
		else
		{
			PendingTask->Abort();
		}
		delete PendingTask;
	}
	while (AsyncTask* CompletedTask = Outbox.TryPop())
	{
		if (CompletedTask->Unstoppable)
		{
			CompletedTask->Done();
		}
		else
		{
			CompletedTask->Abort();
		}
		delete CompletedTask;
	}
}


void DiscardQueues()
{
	Assert(PauseThreads.load() || !State.load());
	while (ActiveThreads.load() > 0)
	{
		// Continually drain the queues to prevent deadlocking while we wait for the thread pool to deactivate.
		DiscardQueuesInner();
		std::this_thread::yield();
	}

	// Flush everything once more for good measure.
	DiscardQueuesInner();
}


void Scheduler::Teardown()
{
	Assert(ThreadIndex == 0);
	Assert(State.load());
	State.store(false);

	fmt::print("Shutting down the thread pool.\n");

	DiscardQueues();

	for (std::thread& Worker : Pool)
	{
		Worker.join();
	}
	Pool.clear();
}


void Scheduler::DropEverything()
{
	Assert(ThreadIndex == 0);
	Assert(State.load());

	PauseThreads.store(true);

	DiscardQueues();

	PauseThreads.store(false);
}


void Scheduler::Stats(size_t& InboxLoad, size_t& OutboxLoad, size_t& ParallelLoad, size_t& ContinuousLoad, size_t& DeleteLoad)
{
	InboxLoad = Inbox.RecentCount();
	OutboxLoad = Outbox.RecentCount();
	ParallelLoad = ParallelQueue.RecentCount();
	ContinuousLoad = ContinuousQueue.RecentCount();
	DeleteLoad = DeleteQueue.RecentCount();
}
