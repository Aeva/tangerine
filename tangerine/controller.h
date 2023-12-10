
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

#include <SDL.h>
#include <SDL_events.h>
#include <SDL_joystick.h>
#include <string>


struct JoystickInfo
{
	SDL_JoystickGUID GUID = { 0 };
	SDL_JoystickID InstanceID = -1;
	SDL_Joystick* Handle = nullptr;
	std::string Name = "uninitialized entry";
};


inline bool operator==(SDL_JoystickGUID LHS, SDL_JoystickGUID RHS)
{
	for (int i = 0; i < 16; ++i)
	{
		if (LHS.data[i] != RHS.data[i])
		{
			return false;
		}
	}
	return true;
}


void RouteControllerEvents(SDL_Event Event);


void EnvInitialControllerConnections(struct ScriptEnvironment& Env);
