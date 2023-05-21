
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

#include <memory>
#include <functional>
#include <vector>
#include <string>
#include "glm_common.h"
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


struct TransformMachine
{
	enum class State
	{
		Identity = 0,
		Offset = 1,
		Matrix = 2
	};

	State FoldState;
	float AccumulatedScale;

	glm::mat4 LastFold;
	glm::mat4 LastFoldInverse;

	bool OffsetPending;
	glm::vec3 OffsetRun;
	bool RotatePending;
	glm::quat RotateRun;

	TransformMachine();
	void Reset();
	void FoldOffset();
	void FoldRotation();
	void Fold();
	void Move(glm::vec3 Offset);
	void Rotate(glm::quat Rotation);
	void Scale(float ScaleBy);
	glm::vec3 ApplyInverse(glm::vec3 Point);
	glm::vec3 Apply(glm::vec3 Point);
	AABB Apply(const AABB InBounds);
	std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point);
	std::string Pretty(std::string Brush);
	bool operator==(TransformMachine& Other);

private:

	AABB ApplyOffset(const AABB InBounds);
	AABB ApplyMatrix(const AABB InBounds);
	std::string CompileOffset(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point);
	std::string CompileMatrix(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point);
};


struct RayHit
{
	bool Hit;
	float Travel;
	glm::vec3 Position;
};


struct SDFNode;
using SDFNodeShared = std::shared_ptr<SDFNode>;


struct SDFNode
{
	virtual float Eval(glm::vec3 Point) = 0;

	virtual SDFNodeShared Clip(glm::vec3 Point, float Radius) = 0;

	virtual SDFNodeShared Copy() = 0;

	virtual AABB Bounds() = 0;

	virtual AABB InnerBounds() = 0;

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point) = 0;

	virtual uint32_t StackSize(const uint32_t Depth = 1) = 0;

	void AddTerminus(std::vector<float>& TreeParams);

	virtual std::string Pretty() = 0;

	glm::vec3 Gradient(glm::vec3 Point);

	virtual void Move(glm::vec3 Offset) = 0;

	virtual void Rotate(glm::quat Rotation) = 0;

	virtual void Scale(float Scale) = 0;

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

	virtual ~SDFNode()
	{
	};
};


namespace SDF
{
	void Align(SDFNodeShared& Tree, glm::vec3 Anchors);

	void RotateX(SDFNodeShared& Tree, float Degrees);

	void RotateY(SDFNodeShared& Tree, float Degrees);

	void RotateZ(SDFNodeShared& Tree, float Degrees);

	SDFNodeShared Sphere(float Radius);

	SDFNodeShared Ellipsoid(float RadipodeX, float RadipodeY, float RadipodeZ);

	SDFNodeShared Box(float ExtentX, float ExtentY, float ExtentZ);

	SDFNodeShared Torus(float MajorRadius, float MinorRadius);

	SDFNodeShared Cylinder(float Radius, float Extent);

	SDFNodeShared Plane(float NormalX, float NormalY, float NormalZ);

	SDFNodeShared Cone(float Radius, float Height);

	SDFNodeShared Coninder(float RadiusL, float RadiusH, float Height);

	SDFNodeShared Union(SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared Diff(SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared Inter(SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared BlendUnion(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared BlendDiff(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared BlendInter(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS);

	SDFNodeShared Flate(SDFNodeShared& Node, float Radius);
}


struct SDFOctree
{
	AABB Bounds;
	glm::vec3 Pivot;
	float TargetSize;
	bool Terminus;
	int LeafCount;
	SDFNodeShared Evaluator;
	SDFOctree* Children[8];
	SDFOctree* Parent;

	static SDFOctree* Create(SDFNodeShared& Evaluator, float TargetSize = 0.25);
	void Populate(int Depth);
	~SDFOctree();
	SDFNodeShared Descend(const glm::vec3 Point, const bool Exact=true);

	using CallbackType = std::function<void(SDFOctree&)>;
	void Walk(CallbackType& Callback);

	float Eval(glm::vec3 Point, const bool Exact = true)
	{
		SDFNodeShared Node = Descend(Point, Exact);
		if (!Exact && !Node)
		{
			return INFINITY;
		}
		return Node->Eval(Point);
	}
	glm::vec3 Gradient(glm::vec3 Point)
	{
		SDFNodeShared Node = Descend(Point);
		return Node->Gradient(Point);
	}
	glm::vec3 Sample(glm::vec3 Point)
	{
		SDFNodeShared Node = Descend(Point);
		return Node->Sample(Point);
	}

private:
	SDFOctree(SDFOctree* InParent, SDFNodeShared& InEvaluator, float InTargetSize, AABB InBounds, int Depth);
};
