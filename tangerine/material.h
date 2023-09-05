
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

#include "sdf_evaluator.h"


struct MaterialSolidColor : public MaterialInterface
{
	glm::vec3 BaseColor;

	MaterialSolidColor(glm::vec3 InBaseColor)
		: MaterialInterface(MaterialType::SolidColor)
		, BaseColor(InBaseColor)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct MaterialPBRBR : public MaterialInterface
{
	glm::vec3 BaseColor;

	MaterialPBRBR(glm::vec3 InBaseColor)
		: MaterialInterface(MaterialType::PBRBR)
		, BaseColor(InBaseColor)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct MaterialDebugNormals : public MaterialInterface
{
	MaterialDebugNormals()
		: MaterialInterface(MaterialType::DebugNormals)
	{
	}

	static glm::vec4 StaticEval(glm::vec3 Normal);

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};
