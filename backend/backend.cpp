
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
// See the License for the specific language governing permissionsand
// limitations under the License.

#if _WIN64
#include <glad/glad_wgl.h>
#define THREADSAFE_CONTEXT true

#elif defined(__GNUC__)
#include <glad/glad_glx.h>
#undef CurrentTime
#define THREADSAFE_CONTEXT false
#endif

#include <glad/glad.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include "backend.h"
#include "gl_boilerplate.h"


#define MINIMUM_VERSION_MAJOR 4
#define MINIMUM_VERSION_MINOR 2


bool PlatformSupportsAsyncRenderer()
{
	return THREADSAFE_CONTEXT;
}


#if _WIN64
HDC RacketDeviceContext;
HGLRC UpgradedContext;

StatusCode Recontextualize()
{
	RacketDeviceContext = wglGetCurrentDC();
	if (gladLoadWGL(RacketDeviceContext))
	{
		std::vector<int> ContextAttributes;

		// Request OpenGL 4.2
		ContextAttributes.push_back(WGL_CONTEXT_MAJOR_VERSION_ARB);
		ContextAttributes.push_back(MINIMUM_VERSION_MAJOR);
		ContextAttributes.push_back(WGL_CONTEXT_MINOR_VERSION_ARB);
		ContextAttributes.push_back(MINIMUM_VERSION_MINOR);

		// Request Core Profile
		ContextAttributes.push_back(WGL_CONTEXT_PROFILE_MASK_ARB);
		ContextAttributes.push_back(WGL_CONTEXT_CORE_PROFILE_BIT_ARB);

		// Terminate attributes list.
		ContextAttributes.push_back(0);

		HGLRC RacketGLContext = wglGetCurrentContext();
		UpgradedContext = wglCreateContextAttribsARB(RacketDeviceContext, RacketGLContext, ContextAttributes.data());
		if (UpgradedContext == NULL)
		{
			std::cout << "Unable to create OpenGL " << MINIMUM_VERSION_MAJOR << "." << MINIMUM_VERSION_MINOR << " core context: ";
			int Error = GetLastError() & 0x0000FFFF;
			if (Error == ERROR_INVALID_VERSION_ARB)
			{
				std::cout << "this OpenGL version is not available on your computer.\n";
			}
			else
			{
				std::cout << "unknown error.\n";
			}
			return StatusCode::FAIL;
		}
		wglMakeCurrent(RacketDeviceContext, UpgradedContext);
		return StatusCode::PASS;
	}
	else
	{
		std::cout << "Unable to load WGL.\n";
		return StatusCode::FAIL;
	}
}


void ConnectContext()
{
	wglMakeCurrent(RacketDeviceContext, UpgradedContext);
}


#elif defined(__GNUC__)
Display* RacketDisplay;
GLXDrawable RacketDrawable;
GLXContext UpgradedContext;
StatusCode Recontextualize()
{
	if (gladLoadGLX(nullptr, 0))
	{
		RacketDisplay = glXGetCurrentDisplay();
		RacketDrawable = glXGetCurrentDrawable();
		GLXContext RacketGLContext = glXGetCurrentContext();

		int Screen;
		glXQueryContext(RacketDisplay, RacketGLContext, GLX_SCREEN, &Screen);

		GLXFBConfig Config;
		{
			int ConfigId;
			glXQueryContext(RacketDisplay, RacketGLContext, GLX_FBCONFIG_ID, &ConfigId);

			std::vector<int> ConfigAttributes;
			ConfigAttributes.push_back(GLX_FBCONFIG_ID);
			ConfigAttributes.push_back(ConfigId);
			ConfigAttributes.push_back(None);

			int Count;
			GLXFBConfig* Found = glXChooseFBConfig(RacketDisplay, Screen, ConfigAttributes.data(), &Count);
			if (Count != 1)
			{
				XFree(Found);
				return StatusCode::FAIL;
			}
			Config = *Found;
			XFree(Found);
		}

		std::vector<int> ContextAttributes;

		// Request OpenGL 4.2
		ContextAttributes.push_back(GLX_CONTEXT_MAJOR_VERSION_ARB);
		ContextAttributes.push_back(MINIMUM_VERSION_MAJOR);
		ContextAttributes.push_back(GLX_CONTEXT_MINOR_VERSION_ARB);
		ContextAttributes.push_back(MINIMUM_VERSION_MINOR);

		// Request Core Profile
		ContextAttributes.push_back(GLX_CONTEXT_PROFILE_MASK_ARB);
		ContextAttributes.push_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB);

		// Terminate attributes list.
		ContextAttributes.push_back(0);

		UpgradedContext = glXCreateContextAttribsARB(RacketDisplay, Config, RacketGLContext, true, ContextAttributes.data());
		if (UpgradedContext == NULL)
		{
			std::cout << "Unable to create OpenGL " << MINIMUM_VERSION_MAJOR << "." << MINIMUM_VERSION_MINOR << " core context.\n";
			return StatusCode::FAIL;
		}

		glXMakeCurrent(RacketDisplay, RacketDrawable, UpgradedContext);

		return StatusCode::PASS;
	}
	else
	{
		std::cout << "Unable to load GLX.\n";
		return StatusCode::FAIL;
	}
}


void ConnectContext()
{
	glXMakeCurrent(RacketDisplay, RacketDrawable, UpgradedContext);
}
#endif


GLuint NullVAO;
ShaderPipeline TestShader;
Buffer ViewInfo("ViewInfo Buffer");


struct ViewInfoUpload
{
	glm::mat4 WorldToView;
	glm::mat4 ViewToWorld;
	glm::mat4 ViewToClip;
	glm::mat4 ClipToView;
	glm::vec4 CameraOrigin;
	glm::vec4 ScreenSize;
	float CurrentTime;
};


// Application specific setup stuff.
StatusCode SetupInner()
{
	// For drawing without a VBO bound.
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

	std::string SimpleScene = \
		"float SceneDist(vec3 Point)\n"
		"{\n"
		"	return SphereBrush(Point, 0.1);\n"
		"}\n";

	RETURN_ON_FAIL(TestShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/test.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", SimpleScene, "shaders/test.fs.glsl")} },
		"Test Shader"));

	return StatusCode::PASS;
}


std::mutex NewShaderLock;
std::atomic_bool NewShaderReady;
std::string NewShaderSource;
void SetupNewShader()
{
	NewShaderLock.lock();
	ShaderPipeline NewShader;
	StatusCode Result = NewShader.Setup(
		{ {GL_VERTEX_SHADER, ShaderSource("shaders/test.vs.glsl", true)},
		  {GL_FRAGMENT_SHADER, GeneratedShader("shaders/math.glsl", NewShaderSource, "shaders/test.fs.glsl")} },
		"Generated Shader");
	NewShaderReady.store(false);
	NewShaderLock.unlock();

	if (Result == StatusCode::FAIL)
	{
		NewShader.Reset();
	}
	else
	{
		TestShader.Reset();
		TestShader = NewShader;
	}
}


std::atomic_int ScreenWidth(200);
std::atomic_int ScreenHeight(200);
void RenderInner()
{
	if (NewShaderReady.load())
	{
		SetupNewShader();
	}

	double DeltaTime;
	double CurrentTime;
	{
		using Clock = std::chrono::high_resolution_clock;
		static Clock::time_point StartTimePoint = Clock::now();
		static Clock::time_point LastTimePoint = StartTimePoint;
		Clock::time_point CurrentTimePoint = Clock::now();
		{
			std::chrono::duration<double, std::milli> FrameDelta = CurrentTimePoint - LastTimePoint;
			DeltaTime = FrameDelta.count();
		}
		{
			std::chrono::duration<double, std::milli> EpochDelta = CurrentTimePoint - StartTimePoint;
			CurrentTime = EpochDelta.count();
		}
		LastTimePoint = CurrentTimePoint;
	}

	static int FrameNumber = 0;
	++FrameNumber;

	static int Width = 0;
	static int Height = 0;
	{
		int NewWidth = ScreenWidth.load();
		int NewHeight = ScreenHeight.load();
		if (NewWidth != Width || NewHeight != Height)
		{
			Width = NewWidth;
			Height = NewHeight;
			glViewport(0, 0, Width, Height);
		}
	}

	{
		const glm::vec3 CameraOrigin = glm::vec3(0.0, -5.0, 0.0);
		const glm::vec3 CameraFocus = glm::vec3(0.0, 0.0, 0.0);
		const glm::vec3 UpVector = glm::vec3(0.0, 0.0, 1.0);
		const glm::mat4 WorldToView = glm::lookAt(CameraOrigin, CameraFocus, UpVector);
		const glm::mat4 ViewToWorld = glm::inverse(WorldToView);

		const float AspectRatio = float(Width) / float(Height);
		const glm::mat4 ViewToClip = glm::infinitePerspective(glm::radians(45.f), AspectRatio, 1.0f);
		const glm::mat4 ClipToView = inverse(ViewToClip);

		ViewInfoUpload BufferData = {
			WorldToView,
			ViewToWorld,
			ViewToClip,
			ClipToView,
			glm::vec4(CameraOrigin, 1.0f),
			glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height),
			float(CurrentTime),
		};
		ViewInfo.Upload((void*)&BufferData, sizeof(BufferData));
		ViewInfo.Bind(GL_UNIFORM_BUFFER, 0);
	}

	{
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Test Draw Pass");
		TestShader.Activate();
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glPopDebugGroup();
	}

#if _WIN64
	SwapBuffers(RacketDeviceContext);
#elif defined(__GNUC__)
	glXSwapBuffers(RacketDisplay, RacketDrawable);
#endif
}


std::atomic_bool RenderLive(true);
bool RenderFrame()
{
#if !THREADSAFE_CONTEXT
	if (RenderLive.load())
	{
		ConnectContext();
		RenderInner();
		return true;
	}
#endif
	return false;
}


#if THREADSAFE_CONTEXT
std::thread* RenderThread;
#endif
void StartRenderThread()
{
#if THREADSAFE_CONTEXT
	ConnectContext();
	while (RenderLive.load())
	{
		RenderInner();
	}
#endif
}


// Load OpenGL and then perform additional setup.
StatusCode Setup()
{
	static bool Initialized = false;
	if (!Initialized)
	{
		Initialized = true;
		RETURN_ON_FAIL(Recontextualize());

		if (gladLoadGL())
		{
			std::cout << glGetString(GL_RENDERER) << "\n";
			std::cout << glGetString(GL_VERSION) << "\n";

			RETURN_ON_FAIL(SetupInner());

#if THREADSAFE_CONTEXT
			RenderThread = new std::thread(StartRenderThread);
#endif
			return StatusCode::PASS;
		}
		else
		{
			std::cout << "Failed to load OpenGL.\n";
			return StatusCode::FAIL;
		}
	}
	else
	{
		return StatusCode::PASS;
	}
}


void Resize(int NewWidth, int NewHeight)
{
	ScreenWidth.store(NewWidth);
	ScreenHeight.store(NewHeight);
}


void NewShader(const char* GeneratedSource)
{
	NewShaderLock.lock();
	NewShaderSource = GeneratedSource;
	NewShaderReady.store(true);
	NewShaderLock.unlock();
}


void Shutdown()
{
	RenderLive.store(false);
#if THREADSAFE_CONTEXT
	RenderThread->join();
	delete RenderThread;
#endif

#if _WIN64
	wglDeleteContext(UpgradedContext);
#endif
}
