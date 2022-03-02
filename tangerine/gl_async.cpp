
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
#include <processthreadsapi.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

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
	HPBUFFERARB PBuffer;

	GLContext(HDC InDeviceContext, HGLRC InRenderContext, HPBUFFERARB InPBuffer = nullptr)
		: DeviceContext(InDeviceContext)
		, RenderContext(InRenderContext)
		, PBuffer(InPBuffer)
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
		const int PixelFormatAttrs[13] = \
		{
			WGL_DRAW_TO_PBUFFER_ARB, 1,
			WGL_RED_BITS_ARB, 0,
			WGL_GREEN_BITS_ARB, 0,
			WGL_BLUE_BITS_ARB, 0,
			WGL_DEPTH_BITS_ARB, 0,
			WGL_STENCIL_BITS_ARB, 0,
			0
		};

		int PixelFormat = 0;
		unsigned int Count = 0;
		if (!wglChoosePixelFormatARB(DeviceContext, PixelFormatAttrs, nullptr, 1, &PixelFormat, &Count))
		{
			return GLContext(nullptr, nullptr, nullptr);
		}

		HPBUFFERARB NewPBuffer = wglCreatePbufferARB(DeviceContext, PixelFormat, 1, 1, nullptr);
		if (!NewPBuffer)
		{
			return GLContext(nullptr, nullptr, nullptr);
		}

		HDC NewDeviceContext = wglGetPbufferDCARB(PBuffer);
		if (!NewDeviceContext)
		{
			wglDestroyPbufferARB(NewPBuffer);
			return GLContext(nullptr, nullptr, nullptr);
		}

		int MajorVersion;
		int MinorVersion;
		int ProfileMask;
		glGetIntegerv(GL_MAJOR_VERSION, &MajorVersion);
		glGetIntegerv(GL_MINOR_VERSION, &MinorVersion);
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &ProfileMask);

		const int AttrList[7] = \
		{
			WGL_CONTEXT_MAJOR_VERSION_ARB, MajorVersion,
			WGL_CONTEXT_MINOR_VERSION_ARB, MinorVersion,
			WGL_CONTEXT_PROFILE_MASK_ARB, ProfileMask,
			0
		};

		HGLRC NewRenderContext = wglCreateContextAttribsARB(NewDeviceContext, RenderContext, AttrList);
		return GLContext(NewDeviceContext, NewRenderContext, NewPBuffer);
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
		if (PBuffer)
		{
			wglReleasePbufferDCARB(PBuffer, DeviceContext);
			wglDestroyPbufferARB(PBuffer);
		}
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


void WorkerThreadMain(GLContext ThreadContext)
{
#if _WIN64
	SetThreadDescription(GetCurrentThread(), L"Shader Compiler Thread");
#endif
	ThreadContext.MakeCurrent();

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
			Threads.push_back(std::thread(WorkerThreadMain, ThreadContext));
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
