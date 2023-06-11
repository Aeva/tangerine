
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

#include "errors.h"
#include <limits>


enum class GraphicsAPI
{
	Invalid,
	OpenGL4_2,
	OpenGLES2
};


enum class VSyncMode : int
{
	// The following is specific to Tangerine:
	Unknown = std::numeric_limits<int>::min(),

	// The following correspond to valid parameters for SDL_GL_SetSwapInterval:
	Adaptive = -1,
	Disabled = 0,
	Enabled = 1
};


extern GraphicsAPI GraphicsBackend;


StatusCode BootGL(int& WindowWidth, int& WindowHeight, bool HeadlessMode, bool ForceES2, bool CreateDebugContext, VSyncMode RequestedVSyncMode);


void TeardownGL();
