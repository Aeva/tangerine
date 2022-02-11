
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

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/extended_min_max.hpp>
#include <glm/gtx/quaternion.hpp>

#if _WIN64
#define TANGERINE_API __declspec(dllexport)
#elif defined(__GNUC__)
#define TANGERINE_API __attribute__ ((visibility ("default")))
#endif


struct AABB
{
	glm::vec3 Min;
	glm::vec3 Max;
};


struct SDFNode
{
	virtual float Eval(glm::vec3 Point) = 0;

	virtual SDFNode* Clip(glm::vec3 Point, float Radius) = 0;

	virtual AABB Bounds() = 0;

	virtual void* Quote() = 0;

	glm::vec3 Gradient(glm::vec3 Point);

	virtual ~SDFNode() {};
};
