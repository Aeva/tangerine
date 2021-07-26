
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
#endif
#include <glad/glad.h>
#include <glm/vec4.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include "backend.h"
#include "gl_boilerplate.h"


#if _WIN64
HDC RacketDeviceContext;
HGLRC UpgradedContext;

StatusCode Recontextualize()
{
	RacketDeviceContext = wglGetCurrentDC();
	HGLRC RacketGLContext = wglGetCurrentContext();
	if (gladLoadWGL(RacketDeviceContext))
	{
		std::vector<int> ContextAttributes;

		// Request OpenGL 4.2
		ContextAttributes.push_back(WGL_CONTEXT_MAJOR_VERSION_ARB);
		ContextAttributes.push_back(4);
		ContextAttributes.push_back(WGL_CONTEXT_MINOR_VERSION_ARB);
		ContextAttributes.push_back(2);

		// Request Core Profile
		ContextAttributes.push_back(WGL_CONTEXT_PROFILE_MASK_ARB);
		ContextAttributes.push_back(WGL_CONTEXT_CORE_PROFILE_BIT_ARB);

		// Terminate attributes list.
		ContextAttributes.push_back(0);

		UpgradedContext = wglCreateContextAttribsARB(RacketDeviceContext, RacketGLContext, ContextAttributes.data());
		wglMakeCurrent(RacketDeviceContext, UpgradedContext);
		return StatusCode::PASS;
	}
	else
	{
		std::cout << "Unable to create a modern OpenGL context :(\n";
		return StatusCode::FAIL;
	}
}
#endif


GLuint NullVAO;
ShaderPipeline TestShader;
Buffer ViewInfo("ViewInfo Buffer");


struct ViewInfoUpload
{
	glm::vec4 ScreenSize;
};


// Application specific setup stuff.
StatusCode SetupInner()
{
	// For drawing without a VBO bound.
	glGenVertexArrays(1, &NullVAO);
	glBindVertexArray(NullVAO);

	RETURN_ON_FAIL(TestShader.Setup(
		{ {GL_VERTEX_SHADER, "shaders/test.vs.glsl"},
		  {GL_FRAGMENT_SHADER, "shaders/test.fs.glsl"} },
		"Test Shader"));

	return StatusCode::PASS;
}


std::atomic_bool RenderThreadLive = true;
std::atomic_int ScreenWidth = 200;
std::atomic_int ScreenHeight = 200;
std::thread* RenderThread;
void Renderer()
{
#if _WIN64
	wglMakeCurrent(RacketDeviceContext, UpgradedContext);
#endif

	while (RenderThreadLive.load())
	{
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
			ViewInfoUpload BufferData = {
				glm::vec4(Width, Height, 1.0f / Width, 1.0f / Height),
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

		glFinish();
#if _WIN64
		SwapBuffers(RacketDeviceContext);
#endif
	}
}


// Load OpenGL and then perform additional setup.
StatusCode Setup()
{
	static bool Initialized = false;
	if (!Initialized)
	{
		Initialized = true;

#if _WIN64
		RETURN_ON_FAIL(Recontextualize());
#endif

		if (gladLoadGL())
		{
			std::cout << glGetString(GL_RENDERER) << "\n";
			std::cout << glGetString(GL_VERSION) << "\n";

			RETURN_ON_FAIL(SetupInner());

			RenderThread = new std::thread(Renderer);
			return StatusCode::PASS;
		}
		else
		{
			std::cout << "Failed to load OpenGL!\n";
			return StatusCode::FAIL;
		}
	}
}


void Resize(int NewWidth, int NewHeight)
{
	ScreenWidth.store(NewWidth);
	ScreenHeight.store(NewHeight);
}


void Shutdown()
{
	std::cout << "Shutting down...\n";
	RenderThreadLive.store(false);
	RenderThread->join();
	wglDeleteContext(UpgradedContext);
}
