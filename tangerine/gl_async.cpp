
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
#if _WIN64
#include <glad/glad_wgl.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

#include "gl_async.h"


#ifdef ENABLE_ASYNC_SHADER_COMPILE


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


bool AsyncCompileEnabled;


void AsyncCompile(ShaderProgram* NewProgram)
{
	if (AsyncCompileEnabled)
	{
		{
			PendingCS.lock();
			Pending.push(NewProgram);
			PendingCS.unlock();
		}
		Fence++;
		Fence.notify_one();
	}
	else
	{
		StatusCode Result = NewProgram->Compile();
		NewProgram->IsValid.store(Result == StatusCode::PASS);
	}
}


void WorkerThreadMain(HDC MainDeviceContext, HGLRC ThreadContext)
{
	wglMakeCurrent(MainDeviceContext, ThreadContext);
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
			if (Result == StatusCode::PASS)
			{
				glFinish();
				Program->IsValid.store(true);
			}
		}

		Fence.wait(Fence.load());
	}

	wglDeleteContext(ThreadContext);
}


void StartWorkerThreads()
{
	HDC MainDeviceContext = wglGetCurrentDC();
	HGLRC MainGLContext = wglGetCurrentContext();

	gladLoadWGL(MainDeviceContext);

	int MajorVersion;
	int MinorVersion;
	int ProfileMask;
	glGetIntegerv(GL_MAJOR_VERSION, &MajorVersion);
	glGetIntegerv(GL_MINOR_VERSION, &MinorVersion);
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &ProfileMask);

	static const int AttrList[7] = \
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, MajorVersion,
		WGL_CONTEXT_MINOR_VERSION_ARB, MinorVersion,
		WGL_CONTEXT_PROFILE_MASK_ARB, ProfileMask,
		0
	};

	int ThreadsCreated = 0;

	static const int ThreadCount = max(std::thread::hardware_concurrency() - 1, 1);
	Threads.reserve(ThreadCount);
	Live.store(1);
	Fence.store(0);
	for (int i = 0; i < ThreadCount; ++i)
	{
		HGLRC ThreadContext = wglCreateContextAttribsARB(MainDeviceContext, MainGLContext, AttrList);
		if (ThreadContext)
		{
			Threads.push_back(std::thread(WorkerThreadMain, MainDeviceContext, ThreadContext));
			++ThreadsCreated;
		}
	}

	AsyncCompileEnabled = ThreadsCreated > 0;
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
