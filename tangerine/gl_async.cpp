
// Copyright 2021 Aeva Palecek
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

#include <glad/glad.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include "gl_async.h"


#if ENABLE_ASYNC_SHADER_COMPILE


#include <thread>
#include <atomic>
#include <mutex>
#include <queue>


std::vector<std::thread> Threads;
std::atomic_int Live;
std::atomic_int Fence;


inline int max(int LHS, int RHS)
{
	return LHS >= RHS ? LHS : RHS;
}


std::mutex PendingCS;
std::queue<ShaderProgram*> Pending;


void AsyncCompile(ShaderProgram* NewProgram)
{
	{
		PendingCS.lock();
		Pending.push(NewProgram);
		PendingCS.unlock();
	}
	Fence++;
	Fence.notify_one();
}


std::mutex ContextLock;


void WorkerThreadMain(SDL_Window* Window)
{
	SDL_GLContext Context;
	{
		ContextLock.lock();
		Context = SDL_GL_CreateContext(Window);
		SDL_GL_MakeCurrent(Window, Context);
		ContextLock.unlock();
	}

	while (Live.load() == 1)
	{
		ShaderProgram* Program = nullptr;
		{
			PendingCS.lock();
			if (!Pending.empty())
			{
				Program = Pending.front();
				Pending.pop();
			}
			PendingCS.unlock();
		}

		if (Program)
		{
			StatusCode Result = Program->Compile();
			glFinish();
			if (Result == StatusCode::PASS)
			{
				Program->ProgramID = 0;
				Program->Warmed.store(true);
			}
		}

		Fence.wait(Fence.load());
	}

	SDL_GL_DeleteContext(Context);
}


void StartWorkerThreads(SDL_Window* Window)
{
	static const int ThreadCount = max(std::thread::hardware_concurrency() - 1, 1);
	Threads.reserve(ThreadCount);
	Live.store(1);
	Fence.store(0);
	for (int i = 0; i < ThreadCount; ++i)
	{
		Threads.push_back(std::thread(WorkerThreadMain, Window));
	}
}


void JoinWorkerThreads()
{
	Live.store(0);
	Fence++;
	Fence.notify_all();
	for (auto& Thread : Threads)
	{
		Thread.join();
	}
}


#else


void AsyncCompile(ShaderProgram* NewProgram)
{
	StatusCode Result = NewProgram->Compile();
	NewProgram->IsValid.store(Result == StatusCode::PASS);
}


void StartWorkerThreads(SDL_Window* Window)
{
}


void JoinWorkerThreads()
{
}


#endif
