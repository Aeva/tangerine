
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

#include <functional>
#include <vector>
#include <string>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_SWIZZLE
#define GLM_FORCE_INTRINSICS
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/extended_min_max.hpp>
#include <glm/gtx/quaternion.hpp>


struct AABB
{
	glm::vec3 Min;
	glm::vec3 Max;

	glm::vec3 Extent()
	{
		return Max - Min;
	}

	AABB operator+(const glm::vec3& Offset) const
	{
		return {
			Min + Offset,
			Max + Offset
		};
	}
};


struct SDFNode
{
	virtual float Eval(glm::vec3 Point) = 0;

	virtual SDFNode* Clip(glm::vec3 Point, float Radius) = 0;

	virtual AABB Bounds() = 0;

	virtual AABB InnerBounds() = 0;

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point) = 0;

	glm::vec3 Gradient(glm::vec3 Point);

	virtual SDFNode* Move(glm::vec3 Offset) = 0;

	virtual SDFNode* Rotate(glm::quat Rotation) = 0;

	virtual bool HasPaint() = 0;

	virtual glm::vec4 Sample(glm::vec3 Point) = 0;

	virtual ~SDFNode() {};
};


struct SDFOctree
{
	AABB Bounds;
	glm::vec3 Pivot;
	float TargetSize;
	bool Terminus;
	SDFNode* Evaluator;
	SDFOctree* Children[8];
	SDFOctree* Parent;

	static SDFOctree* Create(SDFNode* Evaluator, float TargetSize = 0.25);
	void Populate();
	~SDFOctree();
	SDFNode* Descend(const glm::vec3 Point, const bool Exact=true);

	using CallbackType = std::function<void(SDFOctree&)>;
	void Walk(CallbackType& Callback);

	float Eval(glm::vec3 Point, const bool Exact = true)
	{
		SDFNode* Node = Descend(Point, Exact);
		if (!Exact && !Node)
		{
			return INFINITY;
		}
		return Node->Eval(Point);
	}
	glm::vec3 Gradient(glm::vec3 Point)
	{
		SDFNode* Node = Descend(Point);
		return Node->Gradient(Point);
	}
	glm::vec3 Sample(glm::vec3 Point)
	{
		SDFNode* Node = Descend(Point);
		return Node->Sample(Point);
	}

private:
	SDFOctree(SDFOctree* InParent, SDFNode* InEvaluator, float InTargetSize, AABB InBounds);
};
