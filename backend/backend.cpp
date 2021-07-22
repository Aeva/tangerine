
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

#include <glad/glad.h>
#include <iostream>
#include "backend.h"


StatusCode Setup()
{
	static bool Initialized = false;
	if (!Initialized)
	{
		Initialized = true;

		if (gladLoadGL())
		{
			std::cout << glGetString(GL_RENDERER) << "\n";
			std::cout << glGetString(GL_VERSION) << "\n";
		}
		else
		{
			std::cout << "Failed to load OpenGL!\n";
			return StatusCode::FAIL;
		}
	}
	return StatusCode::PASS;
}


StatusCode Render(double CurrentTime, int Width, int Height)
{
	static double LastTime = CurrentTime;
	double DeltaTime = CurrentTime - LastTime;
	LastTime = CurrentTime;
	std::cout << DeltaTime << "\n";

	glClearColor(1.0, 0.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	glFinish();
	return StatusCode::PASS;
}


void Shutdown()
{
	std::cout << "Shutting down...\n";
}
