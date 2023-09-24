
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

#include "colors.h"
#include <memory>
#include <vector>


struct LightInterface
{
	bool Active = true;

	void Show()
	{
		Active = true;
	}

	void Hide()
	{
		Active = false;
	}

	virtual void Eval(glm::vec3 Point, glm::vec3& OutDir) = 0;

	virtual ~LightInterface();

protected:
	static void Register(std::shared_ptr<LightInterface>& Light);
};


using LightShared = std::shared_ptr<LightInterface>;
using LightWeakRef = std::weak_ptr<LightInterface>;


void GetActiveLights(std::vector<LightWeakRef>& ActiveLights);
void UnloadAllLights();


struct DirectionalLight : public LightInterface
{
	glm::vec3 Direction;

	virtual void Eval(glm::vec3 Point, glm::vec3& OutDir);
};


struct PointLight : public LightInterface
{
	glm::vec3 Position;

	virtual void Eval(glm::vec3 Point, glm::vec3& OutDir);

	static LightShared Create(glm::vec3 InPosition);

protected:
	PointLight(glm::vec3 InPosition)
		: Position(InPosition)
	{
	}
};
