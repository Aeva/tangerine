
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
	HDC Device;
	HGLRC GL;

	GLContext(HDC InDevice, HGLRC InContext)
		: Device(InDevice)
		, GL(InContext)
	{
		if (wglCreateContextAttribsARB == nullptr)
		{
			gladLoadWGL(Device);
		}
	}

	static GLContext GetCurrentContext()
	{
		HDC CurrentDevice = wglGetCurrentDC();
		HGLRC CurrentGL = wglGetCurrentContext();
		return GLContext(CurrentDevice, CurrentGL);
	}

	GLContext CreateShared()
	{
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

		HGLRC NewContext = wglCreateContextAttribsARB(Device, GL, AttrList);
		return GLContext(Device, NewContext);
	}

	bool IsValid()
	{
		return GL != 0;
	}

	void MakeCurrent()
	{
		wglMakeCurrent(Device, GL);
	}

	void Shutdown()
	{
		wglDeleteContext(GL);
		GL = 0;
	}
};
#endif


#include <thread>
#include <atomic>
#include <mutex>
#include <queue>


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


std::vector<std::thread> Threads;
std::atomic_int Live;
std::atomic_int Fence;


inline int max(int LHS, int RHS)
{
	return LHS >= RHS ? LHS : RHS;
}


struct PendingWork
{
	std::unique_ptr<ShaderProgram> Shader;
	std::shared_ptr<ShaderEnvelope> Outbox;
};


std::mutex PendingCS;
std::queue<PendingWork> Pending;


bool AsyncCompileEnabled;


void AsyncCompile(std::unique_ptr<ShaderProgram> NewProgram, std::shared_ptr<ShaderEnvelope> Outbox)
{
	if (AsyncCompileEnabled)
	{
		{
			PendingCS.lock();
			Pending.push({std::move(NewProgram), Outbox});
			PendingCS.unlock();
		}
		Fence++;
		Fence.notify_all();
	}
	else
	{
		Compile<false>(NewProgram, Outbox);
	}
}


void WorkerThreadMain(GLContext ThreadContext)
{
	ThreadContext.MakeCurrent();

	{
		// This is meant to prevent a recompile on first draw.
		SetPipelineDefaults();
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_GREATER);
	}

	while (Live.load() == 1)
	{
		std::unique_ptr<ShaderProgram> Shader = nullptr;
		std::shared_ptr<ShaderEnvelope> Outbox = nullptr;
		{
			PendingCS.lock();
			if (!Pending.empty())
			{
				Shader = std::move(Pending.front().Shader);
				Outbox = Pending.front().Outbox;
				Pending.pop();
			}
			PendingCS.unlock();
		}

		if (Shader)
		{
			Compile<true>(Shader, Outbox);
		}

		Fence.wait(Fence.load());
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
	Fence.store(0);
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
	Fence++;
	Fence.notify_all();
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
