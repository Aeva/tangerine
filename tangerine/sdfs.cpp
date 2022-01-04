
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

#include <functional>
#include <cmath>
#include "sdfs.h"


using TreeHandle = void*;
using namespace glm;
using namespace std::placeholders;


// These are to patch over some differences between glsl and glm.
float min(float LHS, float RHS)
{
	return std::fminf(LHS, RHS);
}
float max(float LHS, float RHS)
{
	return std::fmaxf(LHS, RHS);
}
vec2 max(vec2 LHS, float RHS)
{
	return max(LHS, vec2(RHS));
}
vec3 max(vec3 LHS, float RHS)
{
	return max(LHS, vec3(RHS));
}


namespace SDF
{
#define SDF_MATH_ONLY
#include "../shaders/math.glsl"
}


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.
struct BrushNode : public SDFNode
{
	using EvalMixin = std::function<float(vec3)>;
	using TransformMixin = std::function<vec3(vec3)>;

	EvalMixin BrushFn;
	TransformMixin* Transform;

	BrushNode(EvalMixin& InBrushFn, void* InTransform)
		: BrushFn(InBrushFn)
		, Transform((TransformMixin*)InTransform)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn((*Transform)(Point));
	}

	virtual ~BrushNode()
	{
		delete Transform;
	}
};


struct SetNode : public SDFNode
{
	using EvalMixin = std::function<float(float, float)>;

	EvalMixin SetFn;
	SDFNode* LHS;
	SDFNode* RHS;

	SetNode(EvalMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS)
		: SetFn(InSetFn)
		, LHS(InLHS)
		, RHS(InRHS)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return SetFn(
			LHS->Eval(Point),
			RHS->Eval(Point));
	}

	virtual ~SetNode()
	{
		delete LHS;
		delete RHS;
	}
};


// The following API functions provide a means for Racket to compose executable
// signed distance functions.  These are intended for tasks like calculating
// voxel membership and mesh generation, where the frequency of evaluating the
// distance field would be prohibetively slow to perform from racket.


// Evaluate a SDF tree.
extern "C" TANGERINE_API float EvalTree(void* Handle, float X, float Y, float Z)
{
	return ((SDFNode*)Handle)->Eval(vec3(X, Y, Z));
}


// Delete a CSG operator tree that was constructed with the functions below.
extern "C" TANGERINE_API void DiscardTree(void* Handle)
{
	delete (SDFNode*)Handle;
}


// The following functions provide transform constructors to be used by brush nodes.
extern "C" TANGERINE_API void* MakeIdentity()
{
	return new BrushNode::TransformMixin(
		[](vec3 Point) -> vec3
		{
			return Point;
		});
}

extern "C" TANGERINE_API void* MakeTranslation(float X, float Y, float Z)
{
	vec3 Offset(X, Y, Z);
	return new BrushNode::TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point - Offset;
		});
}

extern "C" TANGERINE_API void* MakeMatrixTransform(
	float X1, float Y1, float Z1, float W1,
	float X2, float Y2, float Z2, float W2,
	float X3, float Y3, float Z3, float W3,
	float X4, float Y4, float Z4, float W4)
{
	mat4 Matrix(
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4);

	return new BrushNode::TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = Matrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(void* Transform, float Radius)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(void* Transform, float RadipodeX, float RadipodeY, float RadipodeZ)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeBoxBrush(void* Transform, float ExtentX, float ExtentY, float ExtentZ)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeTorusBrush(void* Transform, float MajorRadius, float MinorRadius)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);
	return new BrushNode(Eval, Transform);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(void* Transform, float Radius, float Extent)
{
	BrushNode::EvalMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);
	return new BrushNode(Eval, Transform);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothUnionOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothCutOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	SetNode::EvalMixin Eval = std::bind(SDF::SmoothIntersectionOp, _1, _2, Threshold);
	return new SetNode(Eval, (SetNode*)LHS, (SetNode*)RHS);
}


// This constructs a simple CSG tree as Racket would for debugging.
void TestTreeEval()
{
	void* SphereTransform = MakeMatrixTransform(
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0);

	void* Sphere = MakeSphereBrush(SphereTransform, 1.0);

	void* BoxTransform = MakeMatrixTransform(
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0);

	void* Box = MakeBoxBrush(BoxTransform, 1.0, 1.0, 1.0);

	void* Union = MakeUnionOp(Sphere, Box);

	float Dist = ((SDFNode*)Union)->Eval(vec3(0.0));

	DiscardTree(Union);
}
