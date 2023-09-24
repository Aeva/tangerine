
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


#include "lights.h"
#include "glm_common.h"


static std::vector<LightWeakRef> ActiveLights;


void LightInterface::Register(LightShared& Light)
{
	ActiveLights.emplace_back(Light);
}


LightInterface::~LightInterface()
{
	for (int i = 0; i < ActiveLights.size(); ++i)
	{
		if (ActiveLights[i].expired())
		{
			ActiveLights.erase(ActiveLights.begin() + i);
			break;
		}
	}
}


void GetActiveLights(std::vector<LightWeakRef>& OutActiveLights)
{
	for (LightWeakRef WeakRef : ActiveLights)
	{
		LightShared Light = WeakRef.lock();
		if (Light && Light->Active)
		{
			OutActiveLights.push_back(Light);
		}
	}
}


void UnloadAllLights()
{
	ActiveLights.clear();
}


void DirectionalLight::Eval(glm::vec3 Point, glm::vec3& OutDir)
{
	OutDir = Direction;
}


LightShared PointLight::Create(glm::vec3 InPosition)
{
	LightShared NewLight = LightShared(new PointLight(InPosition));
	LightInterface::Register(NewLight);

	return NewLight;
}


void PointLight::Eval(glm::vec3 Point, glm::vec3& OutDir)
{
	OutDir = glm::normalize(glm::vec3(Position - Point));
}
