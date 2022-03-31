
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

#include <array>
#include <functional>
#include <cmath>
#include <fmt/format.h>
#include "extern.h"
#include "profiling.h"

#include "sdfs.h"
#include <glm/gtc/type_ptr.hpp>


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
using SetMixin = std::function<float(float, float)>;


const vec4 NullColor = vec4(1.0, 1.0, 1.0, 0.0);


template<typename ParamsT>
int StoreParams(std::vector<float>& TreeParams, const ParamsT& NodeParams)
{
	const int Offset = (int)TreeParams.size();
	for (const float& Param : NodeParams)
	{
		TreeParams.push_back(Param);
	}
	return Offset;
}


std::string MakeParamList(int Offset, int Count)
{
	std::string Params = fmt::format("PARAMS[{}]", Offset++);
	for (int i = 1; i < Count; ++i)
	{
		Params = fmt::format("{}, PARAMS[{}]", Params, Offset++);
	}
	return Params;
}


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.


struct TransformMachine
{
	enum class State
	{
		Identity = 0,
		Offset = 1,
		Matrix = 2
	};

	State FoldState;

	mat4 LastFold;
	mat4 LastFoldInverse;

	bool OffsetPending;
	vec3 OffsetRun;
	bool RotatePending;
	quat RotateRun;

	TransformMachine()
		: FoldState(State::Identity)
		, LastFold(identity<mat4>())
		, LastFoldInverse(identity<mat4>())
		, OffsetPending(false)
		, OffsetRun(vec3(0.0))
		, RotatePending(false)
		, RotateRun(identity<quat>())
	{
	}

	void FoldOffset()
	{
		FoldState = (State)max((int)FoldState, (int)State::Offset);
		LastFoldInverse = translate(LastFoldInverse, OffsetRun);
		LastFold = inverse(LastFoldInverse);
		OffsetRun = vec3(0.0);
		OffsetPending = false;
		FoldState = (State)max((int)FoldState, (int)State::Offset);
	}

	void FoldRotation()
	{
		LastFoldInverse *= transpose(toMat4(RotateRun));
		LastFold = inverse(LastFoldInverse);
		RotateRun = identity<quat>();
		RotatePending = false;
		FoldState = (State)max((int)FoldState, (int)State::Matrix);
	}

	void Fold()
	{
		if (RotatePending)
		{
			FoldRotation();
		}
		else if (OffsetPending)
		{
			FoldOffset();
		}
	}

	void Move(vec3 Offset)
	{
		if (RotatePending)
		{
			FoldRotation();
		}
		OffsetRun -= Offset;
		OffsetPending = true;
	}

	void Rotate(quat Rotation)
	{
		if (OffsetPending)
		{
			FoldOffset();
		}
		RotateRun *= Rotation;
		RotatePending = true;
	}

	vec3 ApplyInverse(vec3 Point)
	{
		Fold();
		vec4 Tmp = LastFoldInverse * vec4(Point, 1.0);
		return Tmp.xyz / Tmp.www;
	}

	vec3 Apply(vec3 Point)
	{
		Fold();
		vec4 Tmp = LastFold * vec4(Point, 1.0);
		return Tmp.xyz / Tmp.www;
	}

	AABB Apply(const AABB InBounds)
	{
		Fold();
		switch (FoldState)
		{
		case State::Identity:
			return InBounds;

		case State::Offset:
			return ApplyOffset(InBounds);

		case State::Matrix:
			return ApplyMatrix(InBounds);
		}
	}

	std::string Compile(std::vector<float>& TreeParams, std::string Point)
	{
		Fold();
		switch (FoldState)
		{
		case State::Identity:
			return Point;

		case State::Offset:
			return CompileOffset(TreeParams, Point);

		case State::Matrix:
			return CompileMatrix(TreeParams, Point);
		}
	}

	bool operator==(TransformMachine& Other)
	{
		Fold();
		Other.Fold();
		if (FoldState == Other.FoldState)
		{
			if (FoldState == State::Identity)
			{
				return true;
			}
			else
			{
				return LastFold == Other.LastFold;
			}
		}
		return false;
	}

private:

	AABB ApplyOffset(const AABB InBounds)
	{
		vec3 Offset = LastFold[3].xyz;
		return {
			InBounds.Min + Offset,
			InBounds.Max + Offset
		};
	}

	AABB ApplyMatrix(const AABB InBounds)
	{
		const vec3 A = InBounds.Min;
		const vec3 B = InBounds.Max;

		const vec3 Points[7] = \
		{
			B,
			vec3(B.x, A.yz),
			vec3(A.x, B.y, A.z),
			vec3(A.xy, B.z),
			vec3(A.x, B.yz),
			vec3(B.x, A.y, B.z),
			vec3(B.xy, A.z)
		};

		AABB Bounds;
		Bounds.Min = Apply(A);
		Bounds.Max = Bounds.Min;

		for (const vec3& Point : Points)
		{
			const vec3 Tmp = Apply(Point);
			Bounds.Min = min(Bounds.Min, Tmp);
			Bounds.Max = max(Bounds.Max, Tmp);
		}

		return Bounds;
	}

	std::string CompileOffset(std::vector<float>& TreeParams, std::string Point)
	{
		const int Offset = TreeParams.size();
		TreeParams.push_back(LastFold[3].x);
		TreeParams.push_back(LastFold[3].y);
		TreeParams.push_back(LastFold[3].z);
		std::string Params = MakeParamList(Offset, 3);
		return fmt::format("({} - vec3({}))", Point, Params);
	}

	std::string CompileMatrix(std::vector<float>& TreeParams, std::string Point)
	{
		const int Offset = TreeParams.size();
		for (int i = 0; i < 16; ++i)
		{
			float Cell = value_ptr(LastFoldInverse)[i];
			TreeParams.push_back(Cell);
		}
		std::string Params = MakeParamList(Offset, 16);
		return fmt::format("MatrixTransform({}, mat4({}))", Point, Params);
	}
};


template<typename ParamsT>
struct BrushNode : public SDFNode
{
	std::string BrushFnName;
	ParamsT NodeParams;
	BrushMixin BrushFn;
	AABB BrushAABB;
	TransformMachine Transform;

	BrushNode(const std::string& InBrushFnName, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB)
		: BrushFnName(InBrushFnName)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
	{
	}

	BrushNode(const std::string& InBrushFnName, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB, TransformMachine& InTransform)
		: BrushFnName(InBrushFnName)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
		, Transform(InTransform)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn(Transform.ApplyInverse(Point));
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (Eval(Point) <= Radius)
		{
			return new BrushNode(BrushFnName, NodeParams, BrushFn, BrushAABB, Transform);
		}
		else
		{
			return nullptr;
		}
	}

	virtual AABB Bounds()
	{
		return Transform.Apply(BrushAABB);
	}

	virtual AABB InnerBounds()
	{
		return Bounds();
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		std::string TransformedPoint = Transform.Compile(TreeParams, Point);
		const int Offset = StoreParams(TreeParams, NodeParams);
		std::string Params = MakeParamList(Offset, (int)NodeParams.size());
		return fmt::format("{}({}, {})", BrushFnName, TransformedPoint, Params);
	}

	virtual void Move(vec3 Offset)
	{
		Transform.Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Transform.Rotate(Rotation);
	}

	virtual bool HasPaint()
	{
		return false;
	}

	virtual vec4 Sample(vec3 Point)
	{
		return NullColor;
	}

	virtual int Complexity()
	{
		return 1;
	}

	virtual bool operator==(SDFNode& Other)
	{
		BrushNode* OtherBrush = dynamic_cast<BrushNode*>(&Other);
		if (OtherBrush && OtherBrush->BrushFnName == BrushFnName && OtherBrush->Transform == Transform)
		{
			for (int i = 0; i < NodeParams.size(); ++i)
			{
				if (OtherBrush->NodeParams[i] != NodeParams[i])
				{
					return false;
				}
			}
			return true;
		}
		return false;
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

	SetNode(SetMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS, float InThreshold)
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
		if (Eval(Point) <= Radius)
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
					return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS, Threshold);
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
				return new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS, Threshold);
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
		return nullptr;
	}

	virtual AABB Bounds()
	{
		AABB BoundsLHS = LHS->Bounds();
		AABB BoundsRHS = RHS->Bounds();

		AABB Combined;
		if (Family == SetFamily::Union)
		{
			Combined.Min = min(BoundsLHS.Min, BoundsRHS.Min);
			Combined.Max = max(BoundsLHS.Max, BoundsRHS.Max);
		}
		else if (Family == SetFamily::Diff)
		{
			Combined = BoundsLHS;
		}
		else if (Family == SetFamily::Inter)
		{
			Combined.Min = max(BoundsLHS.Min, BoundsRHS.Min);
			Combined.Max = min(BoundsLHS.Max, BoundsRHS.Max);
		}

		if (BlendMode)
		{
			AABB Liminal;
			Liminal.Min = max(BoundsLHS.Min, BoundsRHS.Min) - vec3(Threshold);
			Liminal.Max = min(BoundsLHS.Max, BoundsRHS.Max) + vec3(Threshold);

			Combined.Min = min(Combined.Min, Liminal.Min);
			Combined.Max = max(Combined.Max, Liminal.Max);
		}

		return Combined;
	}

	virtual AABB InnerBounds()
	{
		AABB BoundsLHS = LHS->InnerBounds();
		AABB BoundsRHS = RHS->InnerBounds();

		AABB Combined;
		if (Family == SetFamily::Union)
		{
			Combined.Min = min(BoundsLHS.Min, BoundsRHS.Min);
			Combined.Max = max(BoundsLHS.Max, BoundsRHS.Max);
		}
		else if (Family == SetFamily::Diff)
		{
			Combined = BoundsLHS;
		}
		else if (Family == SetFamily::Inter)
		{
			Combined.Min = max(BoundsLHS.Min, BoundsRHS.Min);
			Combined.Max = min(BoundsLHS.Max, BoundsRHS.Max);
		}

		return Combined;
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		const std::string CompiledLHS = LHS->Compile(TreeParams, Point);
		const std::string CompiledRHS = RHS->Compile(TreeParams, Point);

		if (BlendMode)
		{
			const int Offset = (int)TreeParams.size();
			TreeParams.push_back(Threshold);

			if (Family == SetFamily::Union)
			{
				return fmt::format("SmoothUnionOp({}, {}, PARAMS[{}])", CompiledLHS, CompiledRHS, Offset);
			}
			else if (Family == SetFamily::Diff)
			{
				return fmt::format("SmoothCutOp({}, {}, PARAMS[{}])", CompiledLHS, CompiledRHS, Offset);
			}
			else if (Family == SetFamily::Inter)
			{
				return fmt::format("SmoothIntersectionOp({}, {}, PARAMS[{}])", CompiledLHS, CompiledRHS, Offset);
			}
		}
		else
		{
			if (Family == SetFamily::Union)
			{
				return fmt::format("UnionOp({}, {})", CompiledLHS, CompiledRHS);
			}
			else if (Family == SetFamily::Diff)
			{
				return fmt::format("CutOp({}, {})", CompiledLHS, CompiledRHS);
			}
			else if (Family == SetFamily::Inter)
			{
				return fmt::format("IntersectionOp({}, {})", CompiledLHS, CompiledRHS);
			}
		}
	}

	virtual void Move(vec3 Offset)
	{
		for (SDFNode* Child : { LHS, RHS })
		{
			Child->Move(Offset);
		}
	}

	virtual void Rotate(quat Rotation)
	{
		for (SDFNode* Child : { LHS, RHS })
		{
			Child->Rotate(Rotation);
		}
	}

	virtual bool HasPaint()
	{
		return LHS->HasPaint() || RHS->HasPaint();
	}

	virtual vec4 Sample(vec3 Point)
	{
		if (Family == SetFamily::Diff)
		{
			return LHS->Sample(Point);
		}
		else
		{
			float EvalLHS = LHS->Eval(Point);
			float EvalRHS = RHS->Eval(Point);

			if (EvalLHS <= EvalRHS)
			{
				return LHS->Sample(Point);
			}
			else
			{
				return RHS->Sample(Point);
			}
		}
	}

	virtual int Complexity()
	{
		return LHS->Complexity() + RHS->Complexity();
	}

	virtual bool operator==(SDFNode& Other)
	{
		SetNode<Family, BlendMode>* OtherSet = dynamic_cast<SetNode<Family, BlendMode>*>(&Other);
		if (OtherSet && Threshold == OtherSet->Threshold)
		{
			return *LHS == *(OtherSet->LHS) && *RHS == *(OtherSet->RHS);
		}
		return false;
	}

	virtual ~SetNode()
	{
		delete LHS;
		delete RHS;
	}
};


struct PaintNode : public SDFNode
{
	vec3 Color;
	SDFNode* Child;

	PaintNode(vec3 InColor, SDFNode* InChild)
		: Color(InColor)
		, Child(InChild)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(Point);
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		SDFNode* NewChild = Child->Clip(Point, Radius);
		if (NewChild)
		{
			return new PaintNode(Color, NewChild);
		}
		else
		{
			return nullptr;
		}
	}

	virtual AABB Bounds()
	{
		return Child->Bounds();
	}

	virtual AABB InnerBounds()
	{
		return Child->InnerBounds();
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		const int Offset = (int)TreeParams.size();
		TreeParams.push_back(Color.r);
		TreeParams.push_back(Color.g);
		TreeParams.push_back(Color.b);
		std::string ColorParams = MakeParamList(Offset, 3);
		return fmt::format("MaterialDist(vec3({}), {})", ColorParams, Child->Compile(TreeParams, Point));
	}

	virtual void Move(vec3 Offset)
	{
		Child->Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Child->Rotate(Rotation);
	}

	virtual bool HasPaint()
	{
		return true;
	}

	virtual vec4 Sample(vec3 Point)
	{
		return vec4(Color, 1.0);
	}

	virtual int Complexity()
	{
		return Child->Complexity();
	}

	virtual bool operator==(SDFNode& Other)
	{
		PaintNode* OtherPaint = dynamic_cast<PaintNode*>(&Other);
		if (OtherPaint)
		{
			return Color == OtherPaint->Color && *Child == *(OtherPaint->Child);
		}
		return false;
	}

	virtual ~PaintNode()
	{
		delete Child;
	}
};


// The following API functions provide a means for Racket to compose executable
// signed distance functions.  These are intended for tasks like calculating
// voxel membership and mesh generation, where the frequency of evaluating the
// distance field would be prohibetively slow to perform from racket.

// Evaluate a SDF tree.
extern "C" TANGERINE_API float EvalTree(void* Handle, float X, float Y, float Z)
{
	ProfileScope("EvalTree");
	return ((SDFNode*)Handle)->Eval(vec3(X, Y, Z));
}

// Returns a clipped SDF tree.  This will need to be freed separately from the
// original SDF tree.
extern "C" TANGERINE_API void* ClipTree(void* Handle, float X, float Y, float Z, float Radius)
{
	ProfileScope("ClipTree");
	vec3 Point = vec3(X, Y, Z);

	SDFNode* Clipped = ((SDFNode*)Handle)->Clip(Point, Radius);
	if (Clipped && abs(Clipped->Eval(Point)) > Radius)
	{
		delete Clipped;
		return nullptr;
	}
	else
	{
		return Clipped;
	}
}


// Delete a CSG operator tree that was constructed with the functions below.
extern "C" TANGERINE_API void DiscardTree(void* Handle)
{
	ProfileScope("DiscardTree");
	delete (SDFNode*)Handle;
}


// The following functions apply transforms to the evaluator tree.  These will return
// a new handle if the tree root was a brush with no transforms applied, otherwise these
// will return the original tree handle.
extern "C" TANGERINE_API void MoveTree(void* Handle, float X, float Y, float Z)
{
	ProfileScope("Move");
	SDFNode* Tree = ((SDFNode*)Handle);
	Tree->Move(vec3(X, Y, Z));
}

extern "C" TANGERINE_API void RotateTree(void* Handle, float X, float Y, float Z, float W)
{
	ProfileScope("RotateTree");
	SDFNode* Tree = ((SDFNode*)Handle);
	Tree->Rotate(quat(W, X, Y, Z));
}

extern "C" TANGERINE_API void AlignTree(void* Handle, float X, float Y, float Z)
{
	ProfileScope("AlignTree");
	SDFNode* Tree = ((SDFNode*)Handle);
	const vec3 Alignment = vec3(X, Y, Z) * vec3(0.5) + vec3(0.5);
	const AABB Bounds = Tree->InnerBounds();
	const vec3 Offset = mix(Bounds.Min, Bounds.Max, Alignment) * vec3(-1.0);
	Tree->Move(Offset);
}


AABB SymmetricalBounds(vec3 High)
{
	return { High * vec3(-1), High };
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(float Radius)
{
	std::array<float, 1> Params = { Radius };

	BrushMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);

	AABB Bounds = SymmetricalBounds(vec3(Radius));
	return new BrushNode("SphereBrush", Params, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(float RadipodeX, float RadipodeY, float RadipodeZ)
{
	std::array<float, 3> Params = { RadipodeX, RadipodeY, RadipodeZ };

	BrushMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));

	AABB Bounds = SymmetricalBounds(vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode("UnwrappedEllipsoidBrush", Params, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeBoxBrush(float ExtentX, float ExtentY, float ExtentZ)
{
	std::array<float, 3> Params = { ExtentX, ExtentY, ExtentZ };

	BrushMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));

	AABB Bounds = SymmetricalBounds(vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode("UnwrappedBoxBrush", Params, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeTorusBrush(float MajorRadius, float MinorRadius)
{
	std::array<float, 2> Params = { MajorRadius, MinorRadius };

	BrushMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);

	float Radius = MajorRadius + MinorRadius;
	AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, MinorRadius));

	return new BrushNode("TorusBrush", Params, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(float Radius, float Extent)
{
	std::array<float, 2> Params = { Radius, Extent };

	BrushMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);

	AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Extent));
	return new BrushNode("CylinderBrush", Params, Eval, Bounds);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode<SetFamily::Union, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode<SetFamily::Diff, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SetMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode<SetFamily::Inter, false>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
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


// Misc nodes
extern "C" TANGERINE_API void* MakePaint(float Red, float Green, float Blue, void* Child)
{
	return new PaintNode(vec3(Red, Green, Blue), (SDFNode*)Child);
}


// SDFOctree function implementations
SDFOctree* SDFOctree::Create(SDFNode* Evaluator, float TargetSize)
{
	// Determine the octree's bounding cube from the evaluator's bounding box.
	AABB Bounds = Evaluator->Bounds();
	vec3 Extent = Bounds.Max - Bounds.Min;
	float Span = max(max(Extent.x, Extent.y), Extent.z);
	vec3 Padding = (vec3(Span) - Extent) * vec3(0.5);
	Bounds.Min -= Padding;
	Bounds.Max += Padding;

	SDFOctree* Tree = new SDFOctree(nullptr, Evaluator, TargetSize, Bounds, 1);
	if (Tree->Evaluator)
	{
		return Tree;
	}
	else
	{
		delete Tree;
		return nullptr;
	}
}

SDFOctree::SDFOctree(SDFOctree* InParent, SDFNode* InEvaluator, float InTargetSize, AABB InBounds, int Depth)
	: Parent(InParent)
	, TargetSize(InTargetSize)
	, Bounds(InBounds)
{
	vec3 Extent = Bounds.Max - Bounds.Min;
	float Span = max(max(Extent.x, Extent.y), Extent.z);
	Pivot = vec3(Span * 0.5) + Bounds.Min;

	float Radius = length(vec3(Span)) * 0.5;
	Evaluator = InEvaluator->Clip(Pivot, Radius);

	Terminus = Span <= TargetSize || Evaluator == nullptr;
	if (Terminus)
	{
		for (int i = 0; i < 8; ++i)
		{
			Children[i] = nullptr;
		}
	}
	else
	{
		Populate(Depth);
	}
}

void SDFOctree::Populate(int Depth)
{
	bool Uniform = true;
	bool Penultimate = true;
	std::vector<SDFOctree*> Live;
	Live.reserve(8);
	for (int i = 0; i < 8; ++i)
	{
		AABB ChildBounds = Bounds;
		if (i & 1)
		{
			ChildBounds.Min.x = Pivot.x;
		}
		else
		{
			ChildBounds.Max.x = Pivot.x;
		}
		if (i & 2)
		{
			ChildBounds.Min.y = Pivot.y;
		}
		else
		{
			ChildBounds.Max.y = Pivot.y;
		}
		if (i & 4)
		{
			ChildBounds.Min.z = Pivot.z;
		}
		else
		{
			ChildBounds.Max.z = Pivot.z;
		}
		Children[i] = new SDFOctree(this, Evaluator, TargetSize, ChildBounds, Depth + 1);
		if (Children[i]->Evaluator == nullptr)
		{
			delete Children[i];
			Children[i] = nullptr;
		}
		else
		{
			Uniform &= *Evaluator == *(Children[i]->Evaluator);
			Penultimate &= Children[i]->Terminus;
			Live.push_back(Children[i]);
		}
	}

	if (Live.size() == 0)
	{
		delete Evaluator;
		Evaluator = nullptr;
		Penultimate = false;
		Terminus = true;
	}
	else
	{
		Bounds = Live[0]->Bounds;
		for (int i = 1; i < Live.size(); ++i)
		{
			Bounds.Min = min(Bounds.Min, Live[i]->Bounds.Min);
			Bounds.Max = max(Bounds.Max, Live[i]->Bounds.Max);
		}

#if ENABLE_OCTREE_COALESCENCE
		if ((Penultimate && Uniform) || Evaluator->Complexity() <= max(Depth, 3))
		{
			for (int i = 0; i < 8; ++i)
			{
				if (Children[i] != nullptr)
				{
					delete Children[i];
					Children[i] = nullptr;
				}
			}
			Terminus = true;
		}
#endif
	}
}

SDFOctree::~SDFOctree()
{
	for (int i = 0; i < 8; ++i)
	{
		if (Children[i])
		{
			delete Children[i];
			Children[i] = nullptr;
		}
	}
	if (Evaluator)
	{
		delete Evaluator;
		Evaluator = nullptr;
	}
}

SDFNode* SDFOctree::Descend(const vec3 Point, const bool Exact)
{
	if (!Terminus)
	{
		int i = 0;
		if (Point.x > Pivot.x)
		{
			i |= 1;
		}
		if (Point.y > Pivot.y)
		{
			i |= 2;
		}
		if (Point.z > Pivot.z)
		{
			i |= 4;
		}
		SDFOctree* Child = Children[i];
		if (Child)
		{
			SDFNode* Found = Child->Descend(Point);
			return Found || !Exact ? Found : Evaluator;
		}
		else if (!Exact)
		{
			return nullptr;
		}
	}
	return Evaluator;
};

void SDFOctree::Walk(SDFOctree::CallbackType& Callback)
{
	if (Terminus)
	{
		Callback(*this);
	}
	else
	{
		for (SDFOctree* Child : Children)
		{
			if (Child)
			{
				Child->Walk(Callback);
			}
		}
	}
}
