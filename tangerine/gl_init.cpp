
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

#include "gl_init.h"
#include "gl_debug.h"
#include "errors.h"
#include "sdf_rendering.h"

#include <glad/gl.h>

#ifdef MINIMAL_DLL
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengles2.h>

#include <iostream>


SDL_Window* Window = nullptr;
SDL_GLContext Context = nullptr;
GraphicsAPI GraphicsBackend = GraphicsAPI::Invalid;

extern GLsizei MultiSampleCount;


template<GraphicsAPI Backend>
StatusCode CreateWindowGL(int& WindowWidth, int& WindowHeight, GLsizei MultiSampleCountHint, bool HeadlessMode, bool CreateDebugContext)
{
	SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
	if (Backend == GraphicsAPI::OpenGL4_2)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	}
	else if (Backend == GraphicsAPI::OpenGLES2)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	}
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	if (Backend == GraphicsAPI::OpenGLES2)
	{
		// Note: the GL4 backend only resolves to the back buffer at the very end,
		// and thus the backbuffer does not need stencil bits or MSAA to be enabled.

#if ENABLE_RMLUI
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#endif
		if (MultiSampleCountHint > 1)
		{
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, MultiSampleCountHint);
		}
	}
	if (CreateDebugContext)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	}
	Uint32 WindowFlags = SDL_WINDOW_OPENGL;
	if (HeadlessMode)
	{
		WindowFlags |= SDL_WINDOW_HIDDEN;
	}
	else
	{
		WindowFlags |= SDL_WINDOW_RESIZABLE;

		SDL_DisplayMode DisplayMode;
		SDL_GetCurrentDisplayMode(0, &DisplayMode);
		const int MinDisplaySize = std::min(DisplayMode.w, DisplayMode.h);
		const int MaxWindowSize = std::max(480, MinDisplaySize - 128);
		WindowWidth = std::min(WindowWidth, MaxWindowSize);
		WindowHeight = std::min(WindowHeight, MaxWindowSize);
	}

	Window = SDL_CreateWindow(
		"Tangerine",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		WindowWidth, WindowHeight,
		WindowFlags);

	if (Window == nullptr)
	{
		if (MultiSampleCountHint > 1)
		{
			return CreateWindowGL<Backend>(WindowWidth, WindowHeight, MultiSampleCountHint >> 1, HeadlessMode, CreateDebugContext);
		}
		else
		{
			if (Backend == GraphicsAPI::OpenGL4_2)
			{
				std::cout << "Failed to create a SDL2 window for OpenGL 4.2!\n";
			}
			else if (Backend == GraphicsAPI::OpenGLES2)
			{
				std::cout << "Failed to create a SDL2 window for OpenGL ES2!\n";
			}
			else
			{
				std::cout << "Failed to create a SDL2 window!\n";
			}
			return StatusCode::FAIL;
		}
	}
	else
	{
		if (MultiSampleCountHint > 1)
		{
			MultiSampleCount = MultiSampleCountHint;
		}
		else
		{
			MultiSampleCount = 0;
		}
		return StatusCode::PASS;
	}
}


void SetSwapInterval(VSyncMode RequestedVSyncMode)
{
	const VSyncMode Preference[4] = \
	{
		RequestedVSyncMode,
		VSyncMode::Adaptive,
		VSyncMode::Enabled,
		VSyncMode::Disabled,
	};

	VSyncMode SelectedMode = VSyncMode::Unknown;

	for (const VSyncMode Mode : Preference)
	{
		if (Mode != VSyncMode::Unknown)
		{
			const int Interval = int(Mode);
			const int Result = SDL_GL_SetSwapInterval(Interval);
			if (Result == 0)
			{
				SelectedMode = Mode;
				break;
			}
		}
	}

	if (SelectedMode != RequestedVSyncMode && RequestedVSyncMode != VSyncMode::Unknown)
	{
		std::cout << "The requested vsync mode is unavailable.\n";
	}

	if (SelectedMode == VSyncMode::Unknown)
	{
		std::cout << "Unable to set the vsync mode.  The system default will be used.\n";
	}
	else if (SelectedMode == VSyncMode::Adaptive)
	{
		std::cout << "Adaptive vsync is enabled.  Late frames will tear.\n";
	}
	else if (SelectedMode == VSyncMode::Enabled)
	{
		std::cout << "Standard vsync is enabled.  Late frames will stall.\n";
	}
	else if (SelectedMode == VSyncMode::Disabled)
	{
		std::cout << "Vsync is disabled.  Expect horrendous tearing.\n";
	}
}


StatusCode BootGL(int& WindowWidth, int& WindowHeight, bool HeadlessMode, bool ForceES2, bool CreateDebugContext, VSyncMode RequestedVSyncMode)
{
	const bool MultiSamplingWasRequested = MultiSampleCount > 1;
	GraphicsBackend = GraphicsAPI::Invalid;

	if (!ForceES2)
	{
		StatusCode Result = CreateWindowGL<GraphicsAPI::OpenGL4_2>(
			WindowWidth, WindowHeight, MultiSampleCount, HeadlessMode, CreateDebugContext);
		if (Result == StatusCode::PASS)
		{
			Context = SDL_GL_CreateContext(Window);
		}
		if (Context)
		{
			SDL_GL_MakeCurrent(Window, Context);
			std::cout << "Done!\n";

			SetSwapInterval(RequestedVSyncMode);

			std::cout << "Setting up OpenGL... ";
			if (gladLoadGL((GLADloadfunc) SDL_GL_GetProcAddress))
			{
				GraphicsBackend = GraphicsAPI::OpenGL4_2;
				std::cout << "Created OpenGL 4.2 Rendering Context.\n";
			}
		}
	}

	if (GraphicsBackend == GraphicsAPI::Invalid)
	{
		if (!ForceES2)
		{
			std::cout << "Failed to create OpenGL 4.2 Rendering Context!\n";

			if (Context)
			{
				SDL_GL_DeleteContext(Context);
				Context = nullptr;
			}
			if (Window)
			{
				SDL_DestroyWindow(Window);
				Window = nullptr;
			}

			std::cout << "Setting up OpenGL... ";
		}

		StatusCode Result = CreateWindowGL<GraphicsAPI::OpenGLES2>(
			WindowWidth, WindowHeight, MultiSampleCount, HeadlessMode, CreateDebugContext);
		if (Result == StatusCode::PASS)
		{
			Context = SDL_GL_CreateContext(Window);
		}
		if (Context)
		{
			SDL_GL_MakeCurrent(Window, Context);
			std::cout << "Done!\n";

			SetSwapInterval(RequestedVSyncMode);

			std::cout << "Setting up OpenGL... ";
			if (gladLoadGLES2((GLADloadfunc) SDL_GL_GetProcAddress))
			{
				GraphicsBackend = GraphicsAPI::OpenGLES2;
				std::cout << "Created OpenGL ES2 Rendering Context.\n";
			}
			else
			{
				std::cout << "Failed to create OpenGL ES2 Rendering Context!\n";
			}
		}
	}

	if (GraphicsBackend == GraphicsAPI::Invalid)
	{
		if (Context)
		{
			SDL_GL_DeleteContext(Context);
			Context = nullptr;
		}
		if (Window)
		{
			SDL_DestroyWindow(Window);
			Window = nullptr;
		}
		return StatusCode::FAIL;
	}
	else
	{
		if (CreateDebugContext)
		{
			ConnectDebugCallback(0);
		}

		std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";

		if (GraphicsBackend == GraphicsAPI::OpenGL4_2)
		{
			// This is OpenGL 4 only, because ES2 doesn't create any of it's own MSAA targets.
			// With ES2, the sampling rate the windowing system is the one we run with.
			GLint MaxSamples = 0;
			glGetIntegerv(GL_MAX_SAMPLES, &MaxSamples);
			if (MultiSampleCount > MaxSamples)
			{
				MultiSampleCount = GLsizei(MaxSamples);
			}
		}

		if (MultiSampleCount > 1)
		{
			std::cout << "MSAA is enabled with " << MultiSampleCount << " samples per pixel.\n";
		}
		else if (MultiSamplingWasRequested)
		{
			std::cout << "MSAA render targets are not available on this system.\n";
		}
		else
		{
			std::cout << "MSAA has been disabled by user.\n";
		}

		return StatusCode::PASS;
	}
}


void TeardownGL()
{
	if (Context)
	{
		SDL_GL_DeleteContext(Context);
	}
}
