
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


using Clock = std::chrono::high_resolution_clock;


template<typename T, unsigned QueueSize = 1024>
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
};


struct FinalizerTask : public DeleteTask
{
	FinalizerThunk Finalizer;
	virtual void Run()
	{
		Finalizer();
	}
};


static AtomicQueue<AsyncTask> Inbox;
static AtomicQueue<AsyncTask> Outbox;
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

		if (AsyncTask* Task = Inbox.TryPop())
		{
			Task->Run();
			Outbox.BlockingPush(Task);
		}
		else
		{
#if MULTI_RENDERER
			if (CurrentRenderer == Renderer::Sodapop)
#endif // MULTI_RENDERER
#if RENDERER_SODAPOP
			{
				Sodapop::Hammer();
			}
			else if (DedicatedThread)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(4));
			}
#endif // RENDERER_SODAPOP
			if (DedicatedThread)
			{
				std::this_thread::yield();
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
		const int ThreadCount = std::max(int(std::thread::hardware_concurrency()), 2);
		for (int ThreadIndex = 1; ThreadIndex < ThreadCount; ++ThreadIndex)
		{
			// This is meant to be one thread per reported thread of execution, assuming a dual
			// core processor or better.  This includes the main thread, so we start at index 1.
			Pool.emplace_back(WorkerThread<true>, ThreadIndex);
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
