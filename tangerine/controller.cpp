
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

#include "controller.h"
#include "tangerine.h"
#include "embedding.h"
#include "errors.h"
#include "glm_common.h"

#include <fmt/format.h>
#include <set>
#include <string>


struct JoystickCompare
{
	bool operator()(const JoystickInfo& LHS, const JoystickInfo& RHS) const
	{
		for (int i = 0; i < 16; ++i)
		{
			if (LHS.GUID.data[i] < RHS.GUID.data[i])
			{
				return true;
			}
			else if (LHS.GUID.data[i] > RHS.GUID.data[i])
			{
				break;
			}
		}
		return false;
	}
};


using JoystickSet = std::set<JoystickInfo, JoystickCompare>;
using JoystickIterator = JoystickSet::iterator;
static JoystickSet AttachedJoysticks;



static JoystickIterator FindJoystickByGUID(SDL_JoystickGUID GUID)
{
	for (JoystickIterator Joystick = AttachedJoysticks.begin(); Joystick != AttachedJoysticks.end(); ++Joystick)
	{
		if (Joystick->GUID == GUID)
		{
			return Joystick;
		}
	}
	return AttachedJoysticks.end();
}


static JoystickIterator FindJoystickByHandle(SDL_Joystick* Handle)
{
	for (JoystickIterator Joystick = AttachedJoysticks.begin(); Joystick != AttachedJoysticks.end(); ++Joystick)
	{
		if (Joystick->Handle == Handle)
		{
			return Joystick;
		}
	}
	return AttachedJoysticks.end();
}


static JoystickIterator FindJoystickByInstanceID(SDL_JoystickID InstanceID)
{
	for (JoystickIterator Joystick = AttachedJoysticks.begin(); Joystick != AttachedJoysticks.end(); ++Joystick)
	{
		if (Joystick->InstanceID == InstanceID)
		{
			return Joystick;
		}
	}
	return AttachedJoysticks.end();
}


static void HandleJoystickDeviceEvent(SDL_JoyDeviceEvent Event)
{
	if (Event.type == SDL_JOYDEVICEADDED)
	{
		SDL_Joystick* Handle = SDL_JoystickOpen(Event.which);
		std::string Name = SDL_JoystickName(Handle);
		SDL_JoystickGUID GUID = SDL_JoystickGetDeviceGUID(Event.which);
		SDL_JoystickID InstanceID = SDL_JoystickInstanceID(Handle);

		fmt::print("Joystick connected: {}\n", Name);

		{
			JoystickIterator FoundJoystick = FindJoystickByHandle(Handle);
			if (FoundJoystick != AttachedJoysticks.end())
			{
				AttachedJoysticks.erase(FoundJoystick);
			}
		}

		JoystickInfo Joystick = { GUID, InstanceID, Handle, Name };
		AttachedJoysticks.insert(Joystick);

		if (ScriptEnvironment* Env = GetMainEnvironment())
		{
			Env->JoystickConnect(Joystick);
		}
	}
	else
	{
		SDL_Joystick* Handle = SDL_JoystickFromInstanceID(Event.which);
		JoystickIterator FoundJoystick = FindJoystickByHandle(Handle);
		if (FoundJoystick != AttachedJoysticks.end())
		{
			if (ScriptEnvironment* Env = GetMainEnvironment())
			{
				Env->JoystickDisconnect(*FoundJoystick);
			}

			fmt::print("Joystick disconnected: {}\n", FoundJoystick->Name);
			if (SDL_JoystickGetAttached(FoundJoystick->Handle))
			{
				SDL_JoystickClose(FoundJoystick->Handle);
			}
			AttachedJoysticks.erase(FoundJoystick);
		}
	}
}


static void HandleJoystickAxisEvent(SDL_JoyAxisEvent Event)
{
	ScriptEnvironment* Env = GetMainEnvironment();
	JoystickIterator Found = FindJoystickByInstanceID(Event.which);
	if (Env != nullptr && Found != AttachedJoysticks.end())
	{
		float Value = glm::clamp(float(Event.value) / 32767.f, -1.0f, 1.0f);
		Env->JoystickAxis(Found->InstanceID, Event.axis, Value);
	}
}


static void HandleJoystickButtonEvent(SDL_JoyButtonEvent Event)
{
	ScriptEnvironment* Env = GetMainEnvironment();
	JoystickIterator Found = FindJoystickByInstanceID(Event.which);
	if (Env != nullptr && Found != AttachedJoysticks.end())
	{
		Env->JoystickButton(Found->InstanceID, Event.button, Event.state == SDL_PRESSED);
	}
}


void RouteControllerEvents(SDL_Event Event)
{
	if (Event.type >= SDL_JOYAXISMOTION && Event.type <= SDL_JOYDEVICEREMOVED)
	{
		switch (Event.type)
		{
		case SDL_JOYDEVICEADDED:
		case SDL_JOYDEVICEREMOVED:
			HandleJoystickDeviceEvent(Event.jdevice);
			break;

		case SDL_JOYAXISMOTION:
			HandleJoystickAxisEvent(Event.jaxis);
			break;

		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			HandleJoystickButtonEvent(Event.jbutton);
			break;

		default:
			break;
		}
	}
}


void EnvInitialControllerConnections(ScriptEnvironment& Env)
{
	for (JoystickIterator Joystick = AttachedJoysticks.begin(); Joystick != AttachedJoysticks.end(); ++Joystick)
	{
		Env.JoystickConnect(*Joystick);
	}
}
