
// Copyright 2022 Aeva Palecek
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
#include <processthreadsapi.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

#include <shaderc/shaderc.hpp>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "gl_debug.h"
#include "gl_async.h"


template<bool ThreadSafe>
void Compile(std::unique_ptr<ShaderProgram>& NewProgram, std::shared_ptr<ShaderEnvelope>& Outbox)
{
	StatusCode Result = NewProgram->Compile();
	if (Result == StatusCode::PASS)
	{
		if (ThreadSafe)
		{
			glFinish();
		}
		Outbox->Shader = std::move(NewProgram);
		Outbox->Ready.store(true);
	}
	else
	{
		Outbox->Failed.store(true);
	}
}


#ifdef ENABLE_ASYNC_SHADER_COMPILE


#if _WIN64
struct GLContext
{
	HDC DeviceContext;
	HGLRC RenderContext;

	GLContext(HDC InDeviceContext, HGLRC InRenderContext)
		: DeviceContext(InDeviceContext)
		, RenderContext(InRenderContext)
	{
		if (wglCreateContextAttribsARB == nullptr)
		{
			gladLoadWGL(DeviceContext);
		}
	}

	static GLContext GetCurrentContext()
	{
		HDC CurrentDC = wglGetCurrentDC();
		HGLRC CurrentRC = wglGetCurrentContext();
		return GLContext(CurrentDC, CurrentRC);
	}

	GLContext CreateShared()
	{
		int MajorVersion;
		int MinorVersion;
		int ProfileMask;
		glGetIntegerv(GL_MAJOR_VERSION, &MajorVersion);
		glGetIntegerv(GL_MINOR_VERSION, &MinorVersion);
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &ProfileMask);

#if ENABLE_DEBUG_CONTEXTS
		const int ContextFlags = WGL_CONTEXT_DEBUG_BIT_ARB;
#else
		const int ContextFlags = 0;
#endif

		const int AttrList[9] = \
		{
			WGL_CONTEXT_MAJOR_VERSION_ARB, MajorVersion,
			WGL_CONTEXT_MINOR_VERSION_ARB, MinorVersion,
			WGL_CONTEXT_PROFILE_MASK_ARB, ProfileMask,
			WGL_CONTEXT_FLAGS_ARB, ContextFlags,
			0
		};

		HGLRC NewRenderContext = wglCreateContextAttribsARB(DeviceContext, RenderContext, AttrList);
		if (!NewRenderContext)
		{
			return GLContext(nullptr, nullptr);
		}
		return GLContext(DeviceContext, NewRenderContext);
	}

	bool IsValid()
	{
		return RenderContext != 0;
	}

	void MakeCurrent()
	{
		wglMakeCurrent(DeviceContext, RenderContext);
	}

	void Shutdown()
	{
		wglDeleteContext(RenderContext);
		RenderContext = 0;
	}
};
#endif


ShaderProgram* ShaderEnvelope::Access()
{
	if (Ready.load())
	{
		return Shader.get();
	}
	return nullptr;
}


ShaderEnvelope::~ShaderEnvelope()
{
	if (Shader)
	{
		Shader->Reset();
	}
}


inline int max(int LHS, int RHS)
{
	return LHS >= RHS ? LHS : RHS;
}


struct PendingWork
{
	std::unique_ptr<ShaderProgram> Shader;
	std::shared_ptr<ShaderEnvelope> Outbox;
};


std::atomic_int Live;
std::vector<std::thread> Threads;

std::mutex PendingCS;
std::condition_variable PendingCV;
std::queue<PendingWork> Pending;


bool AsyncCompileEnabled;


void AsyncCompile(std::unique_ptr<ShaderProgram> NewProgram, std::shared_ptr<ShaderEnvelope> Outbox)
{
	if (AsyncCompileEnabled)
	{
		{
			std::lock_guard<std::mutex> ScopedLock(PendingCS);
			Pending.push({std::move(NewProgram), Outbox});
		}
		PendingCV.notify_one();
	}
	else
	{
		Compile<false>(NewProgram, Outbox);
	}
}


void WorkerThreadMain(GLContext ThreadContext, size_t ThreadIndex)
{
#if _WIN64
	SetThreadDescription(GetCurrentThread(), L"Shader Compiler Thread");
#endif
	ThreadContext.MakeCurrent();
	ConnectDebugCallback(ThreadIndex);

	{
		// This is meant to prevent a recompile on first draw.
		SetPipelineDefaults();
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_GREATER);
	}

	while (Live.load() == 1)
	{
		std::unique_lock<std::mutex> Lock(PendingCS);

		if (Pending.empty())
		{
			PendingCV.wait(Lock);
			if (Pending.empty())
			{
				Lock.unlock();
				continue;
			}
		}

		std::unique_ptr<ShaderProgram> Shader = std::move(Pending.front().Shader);
		std::shared_ptr<ShaderEnvelope> Outbox = Pending.front().Outbox;
		Pending.pop();

		Lock.unlock();

		Compile<true>(Shader, Outbox);
	}

	ThreadContext.Shutdown();
}


void StartWorkerThreads()
{
	GLContext MainContext = GLContext::GetCurrentContext();

	int ThreadsCreated = 0;

	static const int ThreadCount = max(std::thread::hardware_concurrency() - 1, 1);
	Threads.reserve(ThreadCount);
	Live.store(1);
	for (int i = 0; i < ThreadCount; ++i)
	{
		GLContext ThreadContext = MainContext.CreateShared();
		if (ThreadContext.IsValid())
		{
			Threads.push_back(std::thread(WorkerThreadMain, ThreadContext, i+1));
			++ThreadsCreated;
		}
	}

	AsyncCompileEnabled = ThreadsCreated > 0;
}


void JoinWorkerThreads()
{
	Live.store(0);
	PendingCV.notify_all();
	for (auto& Thread : Threads)
	{
		Thread.join();
	}
}


#else


void AsyncCompile(std::unique_ptr<ShaderProgram> NewProgram, std::shared_ptr<ShaderEnvelope> Outbox)
{
	Compile<false>(NewProgram, Outbox);
}


void StartWorkerThreads(SDL_Window* Window)
{
}


void JoinWorkerThreads()
{
}


#endif
