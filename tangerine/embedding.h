
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

#include "controller.h"
#include <string>

#ifndef EMBED_LUA
#define EMBED_LUA 1
#endif

#ifndef EMBED_RACKET
#define EMBED_RACKET 0
#endif

#define EMBED_MULTI (EMBED_LUA + EMBED_RACKET) > 1


enum class Language
{
	Unknown,
	Lua,
	Racket
};


struct ScriptEnvironment
{
	bool CanAdvance = false;
	virtual void Advance(double DeltaTimeMs, double ElapsedTimeMs) {};

	virtual void JoystickConnect(const JoystickInfo& Joystick) {};
	virtual void JoystickDisconnect(const JoystickInfo& Joystick) {};
	virtual void JoystickAxis(SDL_JoystickID JoystickID, int Axis, float Value) {};
	virtual void JoystickButton(SDL_JoystickID JoystickID, int Button, bool Pressed) {};

	virtual Language GetLanguage() = 0;
	virtual void LoadFromPath(std::string Path) = 0;
	virtual void LoadFromString(std::string Source) = 0;
	virtual ~ScriptEnvironment() {}
};


struct NullEnvironment : public ScriptEnvironment
{
	virtual Language GetLanguage()
	{
		return Language::Unknown;
	}
	virtual void LoadFromPath(std::string Path) {}
	virtual void LoadFromString(std::string Source) {}
};
