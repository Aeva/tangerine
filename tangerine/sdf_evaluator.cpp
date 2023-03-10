
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

#include "sdf_evaluator.h"
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


namespace SDFMath
{
#define SDF_MATH_ONLY
#include "../shaders/math.glsl"
}


vec3 SDFNode::Gradient(vec3 Point)
{
	float AlmostZero = 0.0001;
	vec2 Offset = vec2(1.0, -1.0) * vec2(AlmostZero);

#if 1
	// Tetrahedral method
	vec3 Gradient =
		Offset.xyy * Eval(Point + Offset.xyy) +
		Offset.yyx * Eval(Point + Offset.yyx) +
		Offset.yxy * Eval(Point + Offset.yxy) +
		Offset.xxx * Eval(Point + Offset.xxx);

#else
	// Central differences method
	vec3 Gradient(
		Eval(Point + Offset.xyy) - Eval(Point - Offset.xyy),
		Eval(Point + Offset.yxy) - Eval(Point - Offset.yxy),
		Eval(Point + Offset.yyx) - Eval(Point - Offset.yyx));
#endif

	float LengthSquared = dot(Gradient, Gradient);
	if (LengthSquared == 0.0)
	{
		// Gradient is zero.  Let's try again with a worse method.
		float Dist = Eval(Point);
		return normalize(vec3(
			Eval(Point + Offset.xyy) - Dist,
			Eval(Point + Offset.yxy) - Dist,
			Eval(Point + Offset.yyx) - Dist));
	}
	else
	{
		return Gradient / sqrt(LengthSquared);
	}
}


void SDFNode::AddTerminus(std::vector<float>& TreeParams)
{
	TreeParams.push_back(AsFloat(OPCODE_RETURN));
}


RayHit SDFNode::RayMarch(glm::vec3 RayStart, glm::vec3 RayDir, int MaxIterations, float Epsilon)
{
	RayDir = normalize(RayDir);
	vec3 Position = RayStart;
	float Travel = 0.0;
	for (int i = 0; i < MaxIterations; ++i)
	{
		float Dist = Eval(Position);
		if (Dist <= Epsilon)
		{
			return { true, Travel, Position };
		}
		Travel += Dist;
		Position = RayDir * Travel + RayStart;
	}
	Travel = std::numeric_limits<float>::infinity();
	return { false, Travel, Position };
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

TransformMachine::TransformMachine()
{
	Reset();
}

void TransformMachine::Reset()
{
	FoldState = State::Identity;
	LastFold = identity<mat4>();
	LastFoldInverse = identity<mat4>();
	OffsetPending = false;
	OffsetRun = vec3(0.0);
	RotatePending = false;
	RotateRun = identity<quat>();
	AccumulatedScale = 1.0;
}

void TransformMachine::FoldOffset()
{
	FoldState = (State)max((int)FoldState, (int)State::Offset);
	LastFoldInverse = translate(LastFoldInverse, OffsetRun);
	LastFold = inverse(LastFoldInverse);
	OffsetRun = vec3(0.0);
	OffsetPending = false;
	FoldState = (State)max((int)FoldState, (int)State::Offset);
}

void TransformMachine::FoldRotation()
{
	LastFoldInverse *= transpose(toMat4(RotateRun));
	LastFold = inverse(LastFoldInverse);
	RotateRun = identity<quat>();
	RotatePending = false;
	FoldState = (State)max((int)FoldState, (int)State::Matrix);
}

void TransformMachine::Fold()
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

void TransformMachine::Move(vec3 Offset)
{
	if (RotatePending)
	{
		FoldRotation();
	}
	OffsetRun -= Offset;
	OffsetPending = true;
}

void TransformMachine::Rotate(quat Rotation)
{
	if (OffsetPending)
	{
		FoldOffset();
	}
	RotateRun = Rotation * RotateRun;
	RotatePending = true;
}

void TransformMachine::Scale(float ScaleBy)
{
	Fold();
	LastFoldInverse = scale_slow(LastFoldInverse, vec3(1.0 / ScaleBy));
	LastFold = inverse(LastFoldInverse);
	AccumulatedScale *= ScaleBy;
	FoldState = State::Matrix;
}

vec3 TransformMachine::ApplyInverse(vec3 Point)
{
	Fold();
	vec4 Tmp = LastFoldInverse * vec4(Point, 1.0);
	return Tmp.xyz / Tmp.www;
}

vec3 TransformMachine::Apply(vec3 Point)
{
	Fold();
	vec4 Tmp = LastFold * vec4(Point, 1.0);
	return Tmp.xyz / Tmp.www;
}

AABB TransformMachine::Apply(const AABB InBounds)
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
	UNREACHABLE();
}

std::string TransformMachine::Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point)
{
	Fold();
	switch (FoldState)
	{
	case State::Identity:
		return Point;

	case State::Offset:
		return CompileOffset(WithOpcodes, TreeParams, Point);

	case State::Matrix:
		return CompileMatrix(WithOpcodes, TreeParams, Point);
	}
	UNREACHABLE();
}

std::string TransformMachine::Pretty(std::string Brush)
{
	Fold();
	switch (FoldState)
	{
	case State::Identity:
		return Brush;

	case State::Offset:
		return fmt::format("Move({})", Brush);

	case State::Matrix:
		return fmt::format("Matrix({})", Brush);
	}
	UNREACHABLE();
}

bool TransformMachine::operator==(TransformMachine& Other)
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

AABB TransformMachine::ApplyOffset(const AABB InBounds)
{
	vec3 Offset = LastFold[3].xyz;
	return {
		InBounds.Min + Offset,
		InBounds.Max + Offset
	};
}

AABB TransformMachine::ApplyMatrix(const AABB InBounds)
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

std::string TransformMachine::CompileOffset(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point)
{
	if (WithOpcodes)
	{
		TreeParams.push_back(AsFloat(OPCODE_OFFSET));
	}
	const int Offset = TreeParams.size();
	TreeParams.push_back(LastFold[3].x);
	TreeParams.push_back(LastFold[3].y);
	TreeParams.push_back(LastFold[3].z);
	std::string Params = MakeParamList(Offset, 3);
	return fmt::format("({} - vec3({}))", Point, Params);
}

std::string TransformMachine::CompileMatrix(const bool WithOpcodes, std::vector<float>& TreeParams, std::string Point)
{
	if (WithOpcodes)
	{
		TreeParams.push_back(AsFloat(OPCODE_MATRIX));
	}
	const int Offset = TreeParams.size();
	for (int i = 0; i < 16; ++i)
	{
		float Cell = value_ptr(LastFoldInverse)[i];
		TreeParams.push_back(Cell);
	}
	std::string Params = MakeParamList(Offset, 16);
	return fmt::format("MatrixTransform({}, mat4({}))", Point, Params);
}


template<typename ParamsT>
struct BrushNode : public SDFNode
{
	uint32_t Opcode;
	std::string BrushFnName;
	ParamsT NodeParams;
	BrushMixin BrushFn;
	AABB BrushAABB;
	TransformMachine Transform;

	vec3 Color = vec3(-1.0);

	BrushNode(uint32_t InOpcode, const std::string& InBrushFnName, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB)
		: Opcode(InOpcode)
		, BrushFnName(InBrushFnName)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
	{
	}

	BrushNode(uint32_t InOpcode, const std::string& InBrushFnName, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB,
		TransformMachine& InTransform, vec3& InColor)
		: Opcode(InOpcode)
		, BrushFnName(InBrushFnName)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
		, Transform(InTransform)
		, Color(InColor)
	{
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn(Transform.ApplyInverse(Point)) * Transform.AccumulatedScale;
	}

	virtual SDFNode* Clip(vec3 Point, float Radius)
	{
		if (Eval(Point) <= Radius)
		{
			return Copy();
		}
		else
		{
			return nullptr;
		}
	}

	virtual SDFNode* Copy()
	{
		return new BrushNode(Opcode, BrushFnName, NodeParams, BrushFn, BrushAABB, Transform, Color);
	}

	virtual AABB Bounds()
	{
		return Transform.Apply(BrushAABB);
	}

	virtual AABB InnerBounds()
	{
		return Bounds();
	}

	virtual void GatherBoxels(std::vector<Boxel>& Boxels)
	{
		Boxels.emplace_back(Bounds(), 4.0);
	}

	std::string CompileScale(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& NestedExpr)
	{
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(OPCODE_SCALE));
		}
		float Scale = Transform.AccumulatedScale;
		TreeParams.push_back(Scale);
		return fmt::format("({} * {})", NestedExpr, Scale);
	}

	std::string CompilePaint(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& NestedExpr)
	{
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(OPCODE_PAINT));
		}
		const int Offset = (int)TreeParams.size();
		TreeParams.push_back(Color.r);
		TreeParams.push_back(Color.g);
		TreeParams.push_back(Color.b);
		std::string ColorParams = MakeParamList(Offset, 3);
		return fmt::format("MaterialDist(vec3({}), {})", ColorParams, NestedExpr);
	}

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point)
	{
		std::string TransformedPoint = Transform.Compile(WithOpcodes, TreeParams, Point);

		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(Opcode));
		}
		const int Offset = StoreParams(TreeParams, NodeParams);
		std::string Params = MakeParamList(Offset, (int)NodeParams.size());
		std::string CompiledShape = fmt::format("{}({}, {})", BrushFnName, TransformedPoint, Params);
		if (Transform.AccumulatedScale != 1.0)
		{
			CompiledShape = CompileScale(WithOpcodes, TreeParams, CompiledShape);
		}
		if (HasPaint())
		{
			return CompilePaint(WithOpcodes, TreeParams, CompiledShape);
		}
		else
		{
			return CompiledShape;
		}
	}

	virtual uint32_t StackSize(const uint32_t Depth)
	{
		return Depth;
	}

	virtual std::string Pretty()
	{
		return Transform.Pretty(BrushFnName);
	}

	virtual void Move(vec3 Offset)
	{
		Transform.Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Transform.Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		Transform.Scale(Scale);
	}

	virtual void ApplyMaterial(glm::vec3 InColor, bool Force)
	{
		if (!HasPaint() || Force)
		{
			Color = InColor;
		}
	}

	virtual bool HasPaint()
	{
		return Color != vec3(-1.0);
	}

	virtual bool HasFiniteBounds()
	{
		return !(any(isinf(BrushAABB.Min)) || any(isinf(BrushAABB.Max)));
	}

	virtual vec4 Sample(vec3 Point)
	{
		if (HasPaint())
		{
			return vec4(Color, 1.0);
		}
		else
		{
			return NullColor;
		}
	}

	virtual int LeafCount()
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


enum class SetFamily : uint32_t
{
	Union = OPCODE_UNION,
	Inter = OPCODE_INTER,
	Diff = OPCODE_DIFF
};


template<SetFamily Family, bool BlendMode>
struct SetNode : public SDFNode
{
	uint32_t Opcode;
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
		LHS->Hold();
		RHS->Hold();
		Opcode = (uint32_t)Family;
		if (BlendMode)
		{
			Opcode += OPCODE_SMOOTH;
		}
		if (Family != SetFamily::Diff && RHS->StackSize() > LHS->StackSize())
		{
			// When possible, swap the left and right operands to ensure the tree is left leaning.
			// This can reduce the total stack size needed to render the model in interpreted mode,
			// which both improves loading time and the interpreter's steady state performance.
			// This also reduces the number of shader variants compiled for the non-interpreted
			// mode by ensuring equivalent trees have the same form more often.
			std::swap(LHS, RHS);
		}
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

	virtual SDFNode* Copy()
	{
		return new SetNode<Family, BlendMode>(SetFn, LHS->Copy(), RHS->Copy(), Threshold);
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

	virtual void GatherBoxels(std::vector<Boxel>& Boxels)
	{
		LHS->GatherBoxels(Boxels);
		RHS->GatherBoxels(Boxels);

		if (BlendMode)
		{
			AABB BoundsLHS = LHS->Bounds();
			AABB BoundsRHS = RHS->Bounds();
			AABB Liminal;
			Liminal.Min = max(BoundsLHS.Min, BoundsRHS.Min) - vec3(Threshold);
			Liminal.Max = min(BoundsLHS.Max, BoundsRHS.Max) + vec3(Threshold);

			Boxels.emplace_back(Liminal, Threshold / 3.0);
		}
	}

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point)
	{
		const std::string CompiledLHS = LHS->Compile(WithOpcodes, TreeParams, Point);
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(OPCODE_PUSH));
		}
		const std::string CompiledRHS = RHS->Compile(WithOpcodes, TreeParams, Point);
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(Opcode));
		}
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
				return fmt::format("SmoothDiffOp({}, {}, PARAMS[{}])", CompiledLHS, CompiledRHS, Offset);
			}
			else if (Family == SetFamily::Inter)
			{
				return fmt::format("SmoothInterOp({}, {}, PARAMS[{}])", CompiledLHS, CompiledRHS, Offset);
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
				return fmt::format("DiffOp({}, {})", CompiledLHS, CompiledRHS);
			}
			else if (Family == SetFamily::Inter)
			{
				return fmt::format("InterOp({}, {})", CompiledLHS, CompiledRHS);
			}
		}
	}

	virtual uint32_t StackSize(const uint32_t Depth)
	{
		return max(max(Depth + 1, LHS->StackSize(Depth)), RHS->StackSize(Depth + 1));
	}

	virtual std::string Pretty()
	{
		std::string Name = "Unknown";
		if (Family == SetFamily::Union)
		{
			Name = "Union";
		}
		else if (Family == SetFamily::Diff)
		{
			Name = "Diff";
		}
		else if (Family == SetFamily::Inter)
		{
			Name = "Inter";
		}
		if (BlendMode)
		{
			Name = fmt::format("Blend{}", Name);
		}
		std::string PrettyL = LHS->Pretty();
		std::string PrettyR = RHS->Pretty();
		return fmt::format("{}(\n\t{},\n\t{})", Name, PrettyL, PrettyR);
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

	virtual void Scale(float Scale)
	{
		Threshold *= Scale;
		for (SDFNode* Child : { LHS, RHS })
		{
			Child->Scale(Scale);
		}
	}

	virtual void ApplyMaterial(glm::vec3 Color, bool Force)
	{
		for (SDFNode* Child : { LHS, RHS })
		{
			Child->ApplyMaterial(Color, Force);
		}
	}

	virtual bool HasPaint()
	{
		return LHS->HasPaint() || RHS->HasPaint();
	}

	virtual bool HasFiniteBounds()
	{
		return LHS->HasFiniteBounds() || RHS->HasFiniteBounds();
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

	virtual int LeafCount()
	{
		return LHS->LeafCount() + RHS->LeafCount();
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
		Assert(RefCount == 0);
		LHS->Release();
		RHS->Release();
	}
};


#if 0
struct PaintNode : public SDFNode
{
	vec3 Color;
	SDFNode* Child;

	PaintNode(vec3 InColor, SDFNode* InChild)
		: Color(InColor)
		, Child(InChild)
	{
		Child->Hold();
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

	virtual SDFNode* Copy()
	{
		return new PaintNode(Color, Child->Copy());
	}

	virtual AABB Bounds()
	{
		return Child->Bounds();
	}

	virtual AABB InnerBounds()
	{
		return Child->InnerBounds();
	}

	virtual void GatherBoxels(std::vector<Boxel>& Boxels)
	{
		Child->GatherBoxels(Boxels);
	}

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point)
	{
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(OPCODE_PAINT));
		}
		const int Offset = (int)TreeParams.size();
		TreeParams.push_back(Color.r);
		TreeParams.push_back(Color.g);
		TreeParams.push_back(Color.b);
		std::string ColorParams = MakeParamList(Offset, 3);
		return fmt::format("MaterialDist(vec3({}), {})", ColorParams, Child->Compile(WithOpcodes, TreeParams, Point));
	}

	virtual uint32_t StackSize(const uint32_t Depth)
	{
		return Child->StackSize(Depth);
	}

	virtual std::string Pretty()
	{
		return Child->Pretty();
	}

	virtual void Move(vec3 Offset)
	{
		Child->Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Child->Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		Child->Rotate(Rotation);
	}

	virtual bool HasPaint()
	{
		return true;
	}

	virtual bool HasFiniteBounds()
	{
		return Child->HasFiniteBounds();
	}

	virtual vec4 Sample(vec3 Point)
	{
		return vec4(Color, 1.0);
	}

	virtual int LeafCount()
	{
		return Child->LeafCount();
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
		Assert(RefCount == 0);
		Child->Release();
	}
};
#endif


struct FlateNode : public SDFNode
{
	SDFNode* Child;
	float Radius;

	FlateNode(SDFNode* InChild, float InRadius)
		: Child(InChild)
		, Radius(InRadius)
	{
		Child->Hold();
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(Point) - Radius;
	}

	virtual SDFNode* Clip(vec3 Point, float ClipRadius)
	{
		if (Eval(Point) <= ClipRadius)
		{
			SDFNode* NewChild = Child->Clip(Point, ClipRadius + Radius);
			return new FlateNode(NewChild, Radius);
		}
		else
		{
			return nullptr;
		}
	}

	virtual SDFNode* Copy()
	{
		return new FlateNode(Child->Copy(), Radius);
	}

	virtual AABB Bounds()
	{
		AABB ChildBounds = Child->Bounds();
		ChildBounds.Max += vec3(Radius * 2);
		ChildBounds.Min -= vec3(Radius * 2);
		return ChildBounds;
	}

	virtual AABB InnerBounds()
	{
		AABB ChildBounds = Child->InnerBounds();
		ChildBounds.Max += vec3(Radius * 2);
		ChildBounds.Min -= vec3(Radius * 2);
		return ChildBounds;
	}

	virtual void GatherBoxels(std::vector<Boxel>& Boxels)
	{
		std::vector<Boxel> UnmodifiedBoxels;
		Child->GatherBoxels(UnmodifiedBoxels);
		Boxels.reserve(UnmodifiedBoxels.size());

		for (const Boxel& UnmodifiedBoxel : UnmodifiedBoxels)
		{
			Boxel Tmp = UnmodifiedBoxel;
			Tmp.Max += vec3(Radius * 2);
			Tmp.Min -= vec3(Radius * 2);
			Boxels.push_back(Tmp);
		}
	}

	virtual std::string Compile(const bool WithOpcodes, std::vector<float>& TreeParams, std::string& Point)
	{
		std::string CompiledChild = Child->Compile(WithOpcodes, TreeParams, Point);
		if (WithOpcodes)
		{
			TreeParams.push_back(AsFloat(OPCODE_FLATE));
		}
		const int Offset = TreeParams.size();
		TreeParams.push_back(Radius);
		return fmt::format("FlateOp({}, PARAMS[{}])", CompiledChild, Offset);
	}

	virtual uint32_t StackSize(const uint32_t Depth)
	{
		return Child->StackSize(Depth);
	}

	virtual std::string Pretty()
	{
		return fmt::format("FlateOp({})", Child->Pretty());
	}

	virtual void Move(vec3 Offset)
	{
		Child->Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Child->Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		Child->Scale(Scale);
	}

	virtual void ApplyMaterial(vec3 InColor, bool Force)
	{
		Child->ApplyMaterial(InColor, Force);
	}

	virtual bool HasPaint()
	{
		return Child->HasPaint();
	}

	virtual bool HasFiniteBounds()
	{
		return Child->HasFiniteBounds();
	}

	virtual vec4 Sample(vec3 Point)
	{
		return Child->Sample(Point);
	}

	virtual int LeafCount()
	{
		return Child->LeafCount();
	}

	virtual bool operator==(SDFNode& Other)
	{
		FlateNode* OtherFlate = dynamic_cast<FlateNode*>(&Other);
		return (OtherFlate && OtherFlate->Radius == Radius && *Child == *(OtherFlate->Child));
	}

	virtual ~FlateNode()
	{
		Assert(RefCount == 0);
		Child->Release();
	}
};


AABB SymmetricalBounds(vec3 High)
{
	return { High * vec3(-1), High };
}


namespace SDF
{
	void Align(SDFNode* Tree, vec3 Anchors)
	{
		const vec3 Alignment = Anchors * vec3(0.5) + vec3(0.5);
		const AABB Bounds = Tree->InnerBounds();
		const vec3 Offset = mix(Bounds.Min, Bounds.Max, Alignment) * vec3(-1.0);
		Tree->Move(Offset);
	}

	// The following functions perform rotations on SDFNode trees.
	void RotateX(SDFNode* Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, S, 0, 0));
	}

	void RotateY(SDFNode* Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, 0, S, 0));
	}

	void RotateZ(SDFNode* Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, 0, 0, S));
	}


	// The following functions construct Brush nodes.
	SDFNode* Sphere(float Radius)
	{
		std::array<float, 1> Params = { Radius };

		BrushMixin Eval = std::bind(SDFMath::SphereBrush, _1, Radius);

		AABB Bounds = SymmetricalBounds(vec3(Radius));
		return new BrushNode(OPCODE_SPHERE, "SphereBrush", Params, Eval, Bounds);
	}

	SDFNode* Ellipsoid(float RadipodeX, float RadipodeY, float RadipodeZ)
	{
		std::array<float, 3> Params = { RadipodeX, RadipodeY, RadipodeZ };

		using EllipsoidBrushPtr = float(*)(vec3, vec3);
		BrushMixin Eval = std::bind((EllipsoidBrushPtr)SDFMath::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));

		AABB Bounds = SymmetricalBounds(vec3(RadipodeX, RadipodeY, RadipodeZ));
		return new BrushNode(OPCODE_ELLIPSOID, "EllipsoidBrush", Params, Eval, Bounds);
	}

	SDFNode* Box(float ExtentX, float ExtentY, float ExtentZ)
	{
		std::array<float, 3> Params = { ExtentX, ExtentY, ExtentZ };

		using BoxBrushPtr = float(*)(vec3, vec3);
		BrushMixin Eval = std::bind((BoxBrushPtr)SDFMath::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));

		AABB Bounds = SymmetricalBounds(vec3(ExtentX, ExtentY, ExtentZ));
		return new BrushNode(OPCODE_BOX, "BoxBrush", Params, Eval, Bounds);
	}

	SDFNode* Torus(float MajorRadius, float MinorRadius)
	{
		std::array<float, 2> Params = { MajorRadius, MinorRadius };

		BrushMixin Eval = std::bind(SDFMath::TorusBrush, _1, MajorRadius, MinorRadius);

		float Radius = MajorRadius + MinorRadius;
		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, MinorRadius));

		return new BrushNode(OPCODE_TORUS, "TorusBrush", Params, Eval, Bounds);
	}

	SDFNode* Cylinder(float Radius, float Extent)
	{
		std::array<float, 2> Params = { Radius, Extent };

		BrushMixin Eval = std::bind(SDFMath::CylinderBrush, _1, Radius, Extent);

		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Extent));
		return new BrushNode(OPCODE_CYLINDER, "CylinderBrush", Params, Eval, Bounds);
	}

	SDFNode* Plane(float NormalX, float NormalY, float NormalZ)
	{
		vec3 Normal = normalize(vec3(NormalX, NormalY, NormalZ));
		std::array<float, 3> Params = { Normal.x, Normal.y, Normal.z };

		using PlanePtr = float(*)(vec3, vec3);
		BrushMixin Eval = std::bind((PlanePtr)SDFMath::Plane, _1, Normal);

		AABB Unbound = SymmetricalBounds(vec3(INFINITY, INFINITY, INFINITY));
		if (Normal.x == -1.0)
		{
			Unbound.Min.x = 0.0;
		}
		else if (Normal.x == 1.0)
		{
			Unbound.Max.x = 0.0;
		}
		else if (Normal.y == -1.0)
		{
			Unbound.Min.y = 0.0;
		}
		else if (Normal.y == 1.0)
		{
			Unbound.Max.y = 0.0;
		}
		else if (Normal.z == -1.0)
		{
			Unbound.Min.z = 0.0;
		}
		else if (Normal.z == 1.0)
		{
			Unbound.Max.z = 0.0;
		}
		return new BrushNode(OPCODE_PLANE, "Plane", Params, Eval, Unbound);
	}

	SDFNode* Cone(float Radius, float Height)
	{
		float Tangent = Radius / Height;
		std::array<float, 2> Params = { Tangent, Height };

		BrushMixin Eval = std::bind(SDFMath::ConeBrush, _1, Tangent, Height);

		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Height * .5));
		return new BrushNode(OPCODE_CONE, "ConeBrush", Params, Eval, Bounds);
	}

	SDFNode* Coninder(float RadiusL, float RadiusH, float Height)
	{
		float HalfHeight = Height * .5;
		std::array<float, 3> Params = { RadiusL, RadiusH, HalfHeight };

		BrushMixin Eval = std::bind(SDFMath::ConinderBrush, _1, RadiusL, RadiusH, HalfHeight);

		float MaxRadius = max(RadiusL, RadiusH);
		AABB Bounds = SymmetricalBounds(vec3(MaxRadius, MaxRadius, HalfHeight));
		return new BrushNode(OPCODE_CONINDER, "ConinderBrush", Params, Eval, Bounds);
	}

	// The following functions construct CSG set operator nodes.
	SDFNode* Union(SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::UnionOp, _1, _2);
		return new SetNode<SetFamily::Union, false>(Eval, LHS, RHS, 0.0);
	}

	SDFNode* Diff(SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::DiffOp, _1, _2);
		return new SetNode<SetFamily::Diff, false>(Eval, LHS, RHS, 0.0);
	}

	SDFNode* Inter(SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::InterOp, _1, _2);
		return new SetNode<SetFamily::Inter, false>(Eval, LHS, RHS, 0.0);
	}

	SDFNode* BlendUnion(float Threshold, SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::SmoothUnionOp, _1, _2, Threshold);
		return new SetNode<SetFamily::Union, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
	}

	SDFNode* BlendDiff(float Threshold, SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::SmoothDiffOp, _1, _2, Threshold);
		return new SetNode<SetFamily::Diff, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
	}

	SDFNode* BlendInter(float Threshold, SDFNode* LHS, SDFNode* RHS)
	{
		SetMixin Eval = std::bind(SDFMath::SmoothInterOp, _1, _2, Threshold);
		return new SetNode<SetFamily::Inter, true>(Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
	}

	SDFNode* Flate(SDFNode* Node, float Radius)
	{
		return new FlateNode(Node, Radius);
	}
}


// SDFOctree function implementations
SDFOctree* SDFOctree::Create(SDFNode* Evaluator, float TargetSize)
{
	if (Evaluator->HasFiniteBounds())
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
	else
	{
		fmt::print("Unable to construct SDF octree for infinite area evaluator.");
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
	if (Evaluator)
	{
		Evaluator->Hold();
		LeafCount = Evaluator->LeafCount();
	}
	else
	{
		LeafCount = 0;
	}

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
		Evaluator->Release();
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
		if ((Penultimate && Uniform) || LeafCount <= max(Depth, 3))
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
		Evaluator->Release();
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
