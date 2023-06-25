
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


struct AsyncTask
{
	virtual void Run() = 0;
	virtual void Done() = 0;
	virtual void Abort() = 0;
	virtual ~AsyncTask() {};

	// This will be set by Scheduler::Enqueue.
	bool IsFence = false;
	bool Unstoppable = false;
};


namespace Scheduler
{
	int GetThreadIndex();

	void Setup(const bool ForceSingleThread);
	void Teardown();
	void Advance();
	void Purge();

	std::atomic_bool& GetState();

	bool Live();
	void Enqueue(AsyncTask* Task, bool IsFence = false, bool Unstoppable = false);
}
