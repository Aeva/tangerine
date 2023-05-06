
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

#include <fmt/format.h>

#include <atomic>
#include <mutex>
#include <thread>

#include <vector>
#include <deque>

#include <chrono>

using Clock = std::chrono::high_resolution_clock;


static std::atomic_bool State;
static std::vector<std::thread> Pool;
static thread_local int ThreadIndex = -1;

static std::mutex InboxCS;
static std::deque<AsyncTask*> Inbox;

static std::mutex OutboxCS;
static std::deque<AsyncTask*> Outbox;


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

	while (State.load())
	{
		AsyncTask* Task = nullptr;
		{
			InboxCS.lock();
			if (Inbox.size() > 0)
			{
				Task = Inbox.front();
				Inbox.pop_front();
			}
			InboxCS.unlock();
		}

		if (Task)
		{
			Task->Run();
			{
				OutboxCS.lock();
				Outbox.push_back(Task);
				OutboxCS.unlock();
			}
			std::chrono::duration<double, std::milli> Delta = Clock::now() - ThreadStart;
			if (!DedicatedThread && Delta.count() > 8.0 )
			{
				break;
			}
		}
		else
		{
			if (DedicatedThread)
			{
				std::this_thread::yield();
			}
			else
			{
				break;
			}
		}
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


void Scheduler::Enqueue(AsyncTask* Task)
{
	Assert(ThreadIndex == 0);
	Assert(State.load());
	{
		InboxCS.lock();
		Inbox.push_back(Task);
		InboxCS.unlock();
	}
	fmt::print("[{}] New async task\n", (void*)Task);
}


void Scheduler::Advance()
{
	Assert(ThreadIndex == 0);
	Assert(State.load());
	{
		if (Pool.size() == 0)
		{
			WorkerThread<false>(ThreadIndex);
		}

		OutboxCS.lock();
		while (!Outbox.empty())
		{
			AsyncTask* Task = Outbox.front();
			Outbox.pop_front();

			fmt::print("[{}] Async task complete\n", (void*)Task);
			Task->Done();
			delete Task;
		}
		OutboxCS.unlock();
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


void DiscardInbox()
{
	Assert(ThreadIndex == 0);

	InboxCS.lock();
	for (AsyncTask* Task : Inbox)
	{
		Task->Abort();
		delete Task;
	}
	Inbox.clear();
	InboxCS.unlock();
}


void DiscardOutbox()
{
	Assert(ThreadIndex == 0);

	OutboxCS.lock();
	for (AsyncTask* Task : Outbox)
	{
		Task->Abort();
		delete Task;
	}
	Outbox.clear();
	OutboxCS.unlock();
}


void Scheduler::Teardown()
{
	Assert(ThreadIndex == 0);
	Assert(State.load());
	State.store(false);

	fmt::print("Shutting down the thread pool.\n");

	DiscardInbox();

	for (std::thread& Worker : Pool)
	{
		Worker.join();
	}
	Pool.clear();

	DiscardOutbox();
}


void Scheduler::Purge()
{
	Assert(ThreadIndex == 0);
	Assert(State.load());

	DiscardInbox();
	DiscardOutbox();
}