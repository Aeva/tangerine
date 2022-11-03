
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

#pragma once

#include "glm_common.h"

#define MOUSE_ENTER  0
#define MOUSE_EXIT   1
#define MOUSE_MOVE   2
#define MOUSE_DOWN   3
#define MOUSE_UP     4
#define MOUSE_SCROLL 5
#define MOUSE_EVENTS 6

#define MOUSE_FLAG(EVENT) (1 << EVENT)


struct MouseEvent
{
	int Type = 0;
	int Button = 0;
	int Clicks = 0;
	glm::vec3 RayOrigin = { 0.0, 0.0, 0.0 };
	glm::vec3 RayDir = { 0.0, 0.0, 0.0 };
	glm::vec3 Cursor = { 0.0, 0.0, 0.0 };
	bool AnyHit = false;

	MouseEvent() {};
	MouseEvent(struct SDL_MouseButtonEvent& Event, glm::vec3& InRayOrigin, glm::vec3& InRayDir);
};
