
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
#include "errors.h"


#define ENABLE_OCTREE_COALESCENCE 1


template<typename T>
inline float AsFloat(T Word)
{
	static_assert(sizeof(T) == sizeof(float));
	return *((float*)(&Word));
}


struct AABB
{
	glm::vec3 Min;
	glm::vec3 Max;

	glm::vec3 Extent() const
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


struct RayHit
{
	bool Hit;
	glm::vec3 Position;
};


struct SDFNode
{
	virtual float Eval(glm::vec3 Point) = 0;

	virtual SDFNode* Clip(glm::vec3 Point, float Radius) = 0;

	virtual SDFNode* Copy() = 0;

	virtual AABB Bounds() = 0;

	virtual AABB InnerBounds() = 0;

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point) = 0;

	virtual uint32_t StackSize(const uint32_t Depth = 1) = 0;

	void AddTerminus(std::vector<float>& TreeParams);

	virtual std::string Pretty() = 0;

	glm::vec3 Gradient(glm::vec3 Point);

	virtual void Move(glm::vec3 Offset) = 0;

	virtual void Rotate(glm::quat Rotation) = 0;

	virtual void ApplyMaterial(glm::vec3 Color, bool Force) = 0;

	virtual bool HasPaint() = 0;

	virtual bool HasFiniteBounds() = 0;

	virtual glm::vec4 Sample(glm::vec3 Point) = 0;

	virtual int LeafCount() = 0;

	virtual bool operator==(SDFNode& Other) = 0;

	RayHit RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations = 100, float Epsilon = 0.001);

	bool operator!=(SDFNode& Other)
	{
		return !(*this == Other);
	}

	void Hold()
	{
		++RefCount;
	}

	void Release()
	{
		Assert(RefCount > 0);
		--RefCount;
		if (RefCount == 0)
		{
			delete this;
		}
	}

	virtual ~SDFNode()
	{
		Assert(RefCount == 0);
	};

protected:
	size_t RefCount = 0;
};


namespace SDF
{
	void Align(SDFNode* Tree, glm::vec3 Anchors);

	void RotateX(SDFNode* Tree, float Degrees);

	void RotateY(SDFNode* Tree, float Degrees);

	void RotateZ(SDFNode* Tree, float Degrees);

	SDFNode* Sphere(float Radius);

	SDFNode* Ellipsoid(float RadipodeX, float RadipodeY, float RadipodeZ);

	SDFNode* Box(float ExtentX, float ExtentY, float ExtentZ);

	SDFNode* Torus(float MajorRadius, float MinorRadius);

	SDFNode* Cylinder(float Radius, float Extent);

	SDFNode* Plane(float NormalX, float NormalY, float NormalZ);

	SDFNode* Union(SDFNode* LHS, SDFNode* RHS);

	SDFNode* Diff(SDFNode* LHS, SDFNode* RHS);

	SDFNode* Inter(SDFNode* LHS, SDFNode* RHS);

	SDFNode* BlendUnion(float Threshold, SDFNode* LHS, SDFNode* RHS);

	SDFNode* BlendDiff(float Threshold, SDFNode* LHS, SDFNode* RHS);

	SDFNode* BlendInter(float Threshold, SDFNode* LHS, SDFNode* RHS);
}


struct SDFOctree
{
	AABB Bounds;
	glm::vec3 Pivot;
	float TargetSize;
	bool Terminus;
	int LeafCount;
	SDFNode* Evaluator;
	SDFOctree* Children[8];
	SDFOctree* Parent;

	static SDFOctree* Create(SDFNode* Evaluator, float TargetSize = 0.25);
	void Populate(int Depth);
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
	SDFOctree(SDFOctree* InParent, SDFNode* InEvaluator, float InTargetSize, AABB InBounds, int Depth);
};
