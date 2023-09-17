
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
#include "colors.h"


struct MaterialVertexGroup
{
	MaterialShared Material;
	std::vector<size_t> Vertices;

	MaterialVertexGroup(MaterialShared InMaterial)
		: Material(InMaterial)
	{
	}
};


struct ChthonicMaterialInterface : public MaterialInterface
{
	ChthonicMaterialInterface(MaterialType InType)
		: MaterialInterface(InType)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View) = 0;
};


struct MaterialSolidColor : public ChthonicMaterialInterface
{
	ColorSampler BaseColor;

	MaterialSolidColor(ColorSampler InBaseColor)
		: ChthonicMaterialInterface(MaterialType::SolidColor)
		, BaseColor(InBaseColor)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct MaterialPBRBR : public ChthonicMaterialInterface
{
	ColorSampler BaseColor;

	MaterialPBRBR(ColorSampler InBaseColor)
		: ChthonicMaterialInterface(MaterialType::PBRBR)
		, BaseColor(InBaseColor)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct MaterialDebugNormals : public ChthonicMaterialInterface
{
	MaterialDebugNormals()
		: ChthonicMaterialInterface(MaterialType::DebugNormals)
	{
	}

	static glm::vec4 StaticEval(glm::vec3 Normal);

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct MaterialDebugGradient : public ChthonicMaterialInterface
{
	SDFNodeShared SDF;
	float Interval;
	ColorRamp Ramp;

	MaterialDebugGradient(SDFNodeShared InSDF, float InInterval, ColorRamp InRamp)
		: ChthonicMaterialInterface(MaterialType::DebugGradient)
		, SDF(InSDF->Copy(true))
		, Interval(InInterval)
		, Ramp(InRamp)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View);
};


struct PhotonicMaterialInterface : public ChthonicMaterialInterface
{
	PhotonicMaterialInterface(MaterialType InType)
		: ChthonicMaterialInterface(InType)
	{
	}

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View, glm::vec3 Light) = 0;

	virtual glm::vec4 Eval(glm::vec3 Point, glm::vec3 Normal, glm::vec3 View)
	{
		return Eval(Point, Normal, View, glm::vec3(0.0, 0.0, 1.0));
	}
};
