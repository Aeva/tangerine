
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
inline float min(float LHS, float RHS)
{
	return std::fminf(LHS, RHS);
}
inline float max(float LHS, float RHS)
{
	return std::fmaxf(LHS, RHS);
}
inline vec2 max(vec2 LHS, float RHS)
{
	return max(LHS, vec2(RHS));
}
inline vec3 max(vec3 LHS, float RHS)
{
	return max(LHS, vec3(RHS));
}


namespace SDF
{
#define SDF_MATH_ONLY
#include "../shaders/math.glsl"
}


vec3 SDFNode::Gradient(vec3 Point)
{
	float AlmostZero = 0.0001;
	float Dist = Eval(Point);
	return normalize(vec3(
		Eval(vec3(Point.x + AlmostZero, Point.y, Point.z)) - Dist,
		Eval(vec3(Point.x, Point.y + AlmostZero, Point.z)) - Dist,
		Eval(vec3(Point.x, Point.y, Point.z + AlmostZero)) - Dist));
}


using BrushMixin = std::function<float(vec3)>;
using TransformMixin = std::function<vec3(vec3)>;
using SetMixin = std::function<float(float, float)>;


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.


struct TransformNode : public SDFNode
{
	TransformMixin TransformFn;
	SDFNode* Child;

	TransformNode(TransformMixin& InTransformFn, SDFNode* InChild)
		: TransformFn(InTransformFn)
		, Child(InChild)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(TransformFn(Point));
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		SDFNode* NewChild = Child->Clip(TransformFn(Point), Radius);
		if (NewChild)
		{
			return new TransformNode(TransformFn, NewChild);
		}
		else
		{
			return nullptr;
		}
	}

	virtual ~TransformNode()
	{
		delete Child;
	}
};


struct BrushNode : public SDFNode
{
	BrushMixin BrushFn;

	BrushNode(BrushMixin& InBrushFn)
		: BrushFn(InBrushFn)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn(Point);
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (Eval(Point) <= Radius)
		{
			return new BrushNode(BrushFn);
		}
		else
		{
			return nullptr;
		}
	}
};


enum class SetFamily
{
	Union,
	Diff,
	Inter
};


template<SetFamily Family, bool BlendMode>
struct SetNode : public SDFNode
{
	SetMixin SetFn;
	SDFNode* LHS;
	SDFNode* RHS;
	float Threshold;

	SetNode(SetMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS, float InThreshold = 0.0)
		: SetFn(InSetFn)
		, LHS(InLHS)
		, RHS(InRHS)
		, Threshold(InThreshold)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return SetFn(
			LHS->Eval(Point),
			RHS->Eval(Point));
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (BlendMode)
		{
			// If both of these clip tests pass, then the point should be in the blending region
			// for all blending set operator types.  If one of these returns nullptr, the other
			// should be deleted.  If we don't return a new blending set node here, fail through
			// to the regular set operator behavior to return an operand, when applicable.

			SDFNode* NewLHS = LHS->Clip(Point, Radius + Threshold);
			SDFNode* NewRHS = RHS->Clip(Point, Radius + Threshold);
			if (NewLHS && NewRHS)
			{
				return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS);
			}
			else if (NewLHS)
			{
				delete NewLHS;
			}
			else if (NewRHS)
			{
				delete NewRHS;
			}
			if (Family == SetFamily::Inter)
			{
				return nullptr;
			}
		}

		SDFNode* NewLHS = LHS->Clip(Point, Radius);
		SDFNode* NewRHS = RHS->Clip(Point, Radius);

		if (NewLHS && NewRHS)
		{
			// Note, this shouldn't be possible to hit when BlendMode == true.
			return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS);
		}
		else if (Family == SetFamily::Union)
		{
			// Return whichever operand matched or nullptr.
			return NewLHS != nullptr ? NewLHS : NewRHS;
		}
		else if (Family == SetFamily::Diff)
		{
			// We can only return the LHS side, which may be nullptr.
			if (NewRHS)
			{
				delete NewRHS;
			}
			return NewLHS;
		}
		else if (Family == SetFamily::Inter)
		{
			// Neither operand is valid.
			if (NewLHS)
			{
				delete NewLHS;
			}
			else if (NewRHS)
			{
				delete NewRHS;
			}
			return nullptr;
		}
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


// The following functions construct transform nodes.
extern "C" TANGERINE_API void* MakeTranslation(float X, float Y, float Z, void* Child)
{
	vec3 Offset(X, Y, Z);
	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point - Offset;
		});

	return new TransformNode(Eval, (SDFNode*)Child);
}


extern "C" TANGERINE_API void* MakeMatrixTransform(
	float X1, float Y1, float Z1, float W1,
	float X2, float Y2, float Z2, float W2,
	float X3, float Y3, float Z3, float W3,
	float X4, float Y4, float Z4, float W4,
	void* Child)
{
	mat4 Matrix(
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4);

	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = Matrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});

	return new TransformNode(Eval, (SDFNode*)Child);
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(float Radius)
{
	BrushMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(float RadipodeX, float RadipodeY, float RadipodeZ)
{
	BrushMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeBoxBrush(float ExtentX, float ExtentY, float ExtentZ)
{
	BrushMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeTorusBrush(float MajorRadius, float MinorRadius)
{
	float Radius = MajorRadius + MinorRadius;
	BrushMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);
	return new BrushNode(Eval);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(float Radius, float Extent)
{
	BrushMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);
	return new BrushNode(Eval);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode<SetFamily::Union, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode<SetFamily::Diff, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode<SetFamily::Inter, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothUnionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Union, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothCutOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Diff, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::SmoothIntersectionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Inter, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}
