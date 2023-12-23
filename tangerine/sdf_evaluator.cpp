
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

#include <array>
#include <functional>
#include <chrono>
#include <cmath>
#include <fmt/format.h>
#include "extern.h"
#include "profiling.h"

#include "sdf_evaluator.h"
#include "material.h"
#include "transform.h"
#include <glm/gtc/type_ptr.hpp>


using namespace glm;
using namespace std::placeholders;


MaterialShared GetDefaultMaterial()
{
	static MaterialShared DefaultMaterial = MaterialShared(new MaterialPBRBR(glm::vec3(1.0, 1.0, 1.0)));
	return DefaultMaterial;
}


void ProgramBuffer::Push(const OpcodeT InOpcode)
{
	Words.emplace_back(InOpcode);
}


void ProgramBuffer::Push(const float InScalar)
{
	Words.emplace_back(InScalar);
}


void ProgramBuffer::Push(const vec3& InVector)
{
	for (int i = 0; i < 3; ++i)
	{
		Push(InVector[i]);
	}
}


void ProgramBuffer::Push(const mat4& InMatrix)
{
	for (int i = 0; i < 16; ++i)
	{
		float Cell = value_ptr(InMatrix)[i];
		Push(Cell);
	}
}


OpcodeT ProgramBuffer::ReadOpcodeAt(const size_t ProgramCounter) const
{
#if USE_VARIANT_INSTEAD_OF_UNION
	if (ProgramCounter < Words.size() && std::holds_alternative<OpcodeT>(Words[ProgramCounter]))
	{
		return std::get<OpcodeT>(Words[ProgramCounter]);
	}
	else
	{
		return OpcodeT::Stop;
	}
#else
	return Words[ProgramCounter].Opcode;
#endif
}


float ProgramBuffer::ReadScalarAt(const size_t ProgramCounter) const
{
#if USE_VARIANT_INSTEAD_OF_UNION
	return std::get<float>(Words[ProgramCounter]);
#else
	return Words[ProgramCounter].Scalar;
#endif
}


vec3 ProgramBuffer::ReadVectorAt(const size_t ProgramCounter) const
{
#if USE_VARIANT_INSTEAD_OF_UNION
	vec3 Vector = \
	{
		std::get<float>(Words[ProgramCounter + 0]),
		std::get<float>(Words[ProgramCounter + 1]),
		std::get<float>(Words[ProgramCounter + 2])
	};
	return Vector;
#else
	vec3 Vector = \
	{
		Words[ProgramCounter + 0].Scalar,
		Words[ProgramCounter + 1].Scalar,
		Words[ProgramCounter + 2].Scalar
	};
	return Vector;
#endif
}


mat4 ProgramBuffer::ReadMatrixAt(const size_t ProgramCounter) const
{
#if USE_VARIANT_INSTEAD_OF_UNION
	mat4 Matrix = mat4(
		std::get<float>(Words[ProgramCounter + 0]),
		std::get<float>(Words[ProgramCounter + 1]),
		std::get<float>(Words[ProgramCounter + 2]),
		std::get<float>(Words[ProgramCounter + 3]),
		std::get<float>(Words[ProgramCounter + 4]),
		std::get<float>(Words[ProgramCounter + 5]),
		std::get<float>(Words[ProgramCounter + 6]),
		std::get<float>(Words[ProgramCounter + 7]),
		std::get<float>(Words[ProgramCounter + 8]),
		std::get<float>(Words[ProgramCounter + 9]),
		std::get<float>(Words[ProgramCounter + 10]),
		std::get<float>(Words[ProgramCounter + 11]),
		std::get<float>(Words[ProgramCounter + 12]),
		std::get<float>(Words[ProgramCounter + 13]),
		std::get<float>(Words[ProgramCounter + 14]),
		std::get<float>(Words[ProgramCounter + 15]));
	return Matrix;
#else
	mat4 Matrix = mat4(
		Words[ProgramCounter + 0].Scalar,
		Words[ProgramCounter + 1].Scalar,
		Words[ProgramCounter + 2].Scalar,
		Words[ProgramCounter + 3].Scalar,
		Words[ProgramCounter + 4].Scalar,
		Words[ProgramCounter + 5].Scalar,
		Words[ProgramCounter + 6].Scalar,
		Words[ProgramCounter + 7].Scalar,
		Words[ProgramCounter + 8].Scalar,
		Words[ProgramCounter + 9].Scalar,
		Words[ProgramCounter + 10].Scalar,
		Words[ProgramCounter + 11].Scalar,
		Words[ProgramCounter + 12].Scalar,
		Words[ProgramCounter + 13].Scalar,
		Words[ProgramCounter + 14].Scalar,
		Words[ProgramCounter + 15].Scalar);
	return Matrix;
#endif
}


namespace SDFMath
{
	float Sphere(vec3 Point, float Radius)
	{
		return length(Point) - Radius;
	}


	float Ellipsoid(vec3 Point, vec3 Radipodes)
	{
		float K0 = length(vec3(Point / Radipodes));
		float K1 = length(vec3(Point / (Radipodes * Radipodes)));
		return K0 * (K0 - 1.0) / K1;
	}


	// This exists to simplify parameter generation.
	float Ellipsoid(vec3 Point, float RadipodeX, float RadipodeY, float RadipodeZ)
	{
		return Ellipsoid(Point, vec3(RadipodeX, RadipodeY, RadipodeZ));
	}


	float Box(vec3 Point, vec3 Extent)
	{
		vec3 A = abs(Point) - Extent;
		return length(max(A, 0.0)) + min(max(max(A.x, A.y), A.z), 0.0);
	}


	// This exists to simplify parameter generation.
	float Box(vec3 Point, float ExtentX, float ExtentY, float ExtentZ)
	{
		return Box(Point, vec3(ExtentX, ExtentY, ExtentZ));
	}


	float Torus(vec3 Point, float MajorRadius, float MinorRadius)
	{
		return length(vec2(length(vec2(Point.xy())) - MajorRadius, Point.z)) - MinorRadius;
	}


	float Cylinder(vec3 Point, float Radius, float Extent)
	{
		vec2 D = abs(vec2(length(vec2(Point.xy())), Point.z)) - vec2(Radius, Extent);
		return min(max(D.x, D.y), 0.0) + length(max(D, 0.0));
	}


	float Plane(vec3 Point, vec3 Normal)
	{
		return dot(Point, Normal);
	}


	float Plane(vec3 Point, float NormalX, float NormalY, float NormalZ)
	{
		return Plane(Point, vec3(NormalX, NormalY, NormalZ));
	}


	float Cone(vec3 Point, float Tangent, float Height)
	{
		vec2 Q = Height * vec2(Tangent, -1.0);
		vec2 W = vec2(length(vec2(Point.xy())), Height * -.5 + Point.z);
		vec2 A = W - Q * clamp(float(dot(W, Q) / dot(Q, Q)), 0.0f, 1.0f);
		vec2 B = W - Q * vec2(clamp(float(W.x / Q.x), 0.0f, 1.0f), 1.0);
		float K = sign(Q.y);
		float D = min(dot(A, A), dot(B, B));
		float S = max(K * (W.x * Q.y - W.y * Q.x), K * (W.y - Q.y));
		return sqrt(D) * sign(S);
	}


	float Coninder(vec3 Point, float RadiusL, float RadiusH, float Height)
	{
		vec2 Q = vec2(length(vec2(Point.xy())), Point.z);
		vec2 K1 = vec2(RadiusH, Height);
		vec2 K2 = vec2(RadiusH - RadiusL, 2.0 * Height);
		vec2 CA = vec2(Q.x - min(Q.x, (Q.y < 0.0) ? RadiusL : RadiusH), abs(Q.y) - Height);
		vec2 CB = Q - K1 + K2 * clamp(dot(K1 - Q, K2) / dot(K2, K2), 0.0f, 1.0f);
		float S = (CB.x < 0.0 && CA.y < 0.0) ? -1.0f : 1.0f;
		return S * sqrt(min(dot(CA, CA), dot(CB, CB)));
	}


	float Union(float LHS, float RHS, float Unused)
	{
		return min(LHS, RHS);
	}


	float Inter(float LHS, float RHS, float Unused)
	{
		return max(LHS, RHS);
	}


	float Diff(float LHS, float RHS, float Unused)
	{
		return max(LHS, -RHS);
	}


	float BlendUnion(float LHS, float RHS, float Threshold)
	{
		float H = max(Threshold - abs(LHS - RHS), 0.0);
		return min(LHS, RHS) - H * H * 0.25 / Threshold;
	}


	float BlendInter(float LHS, float RHS, float Threshold)
	{
		float H = max(Threshold - abs(LHS - RHS), 0.0);
		return max(LHS, RHS) + H * H * 0.25 / Threshold;
	}


	float BlendDiff(float LHS, float RHS, float Threshold)
	{
		float H = max(Threshold - abs(LHS + RHS), 0.0);
		return max(LHS, -RHS) + H * H * 0.25 / Threshold;
	}


	float Flate(float Dist, float Radius)
	{
		return Dist - Radius;
	}
}


vec3 SDFNode::Gradient(vec3 Point)
{
	float AlmostZero = 0.0001f;
	vec2 Offset = vec2(1.0, -1.0) * vec2(AlmostZero);

#if 1
	// Tetrahedral method
	vec3 Gradient =
		Offset.xyy() * Eval(Point + Offset.xyy()) +
		Offset.yyx() * Eval(Point + Offset.yyx()) +
		Offset.yxy() * Eval(Point + Offset.yxy()) +
		Offset.xxx() * Eval(Point + Offset.xxx());

#else
	// Central differences method
	vec3 Gradient(
		Eval(Point + Offset.xyy()) - Eval(Point - Offset.xyy()),
		Eval(Point + Offset.yxy()) - Eval(Point - Offset.yxy()),
		Eval(Point + Offset.yyx()) - Eval(Point - Offset.yyx()));
#endif

	float LengthSquared = dot(Gradient, Gradient);
	if (LengthSquared == 0.0)
	{
		// Gradient is zero.  Let's try again with a worse method.
		float Dist = Eval(Point);
		return normalize(vec3(
			Eval(Point + Offset.xyy()) - Dist,
			Eval(Point + Offset.yxy()) - Dist,
			Eval(Point + Offset.yyx()) - Dist));
	}
	else
	{
		return Gradient / sqrt(LengthSquared);
	}
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
using SetMixin = std::function<float(float, float, float)>;


struct EvaluatorTransform : public Transform
{
	AABB Apply(const AABB InBounds) const;
	void Compile(ProgramBuffer& Program) const;
};


AABB EvaluatorTransform::Apply(const AABB InBounds) const
{
	if (Rotation == identity<quat>())
	{
		return \
		{
			(InBounds.Min * Scalation) + Translation,
			(InBounds.Max * Scalation) + Translation
		};
	}
	else
	{
		const vec3 A = InBounds.Min;
		const vec3 B = InBounds.Max;

		const vec3 Points[7] = \
		{
			B,
			vec3(B.x, A.yz()),
			vec3(A.x, B.y, A.z),
			vec3(A.xy(), B.z),
			vec3(A.x, B.yz()),
			vec3(B.x, A.y, B.z),
			vec3(B.xy(), A.z)
		};

		AABB Bounds;
		Bounds.Min = Transform::Apply(A);
		Bounds.Max = Bounds.Min;

		for (const vec3& Point : Points)
		{
			const vec3 Tmp = Transform::Apply(Point);
			Bounds.Min = min(Bounds.Min, Tmp);
			Bounds.Max = max(Bounds.Max, Tmp);
		}

		return Bounds;
	}
}


void EvaluatorTransform::Compile(ProgramBuffer& Program) const
{
	const bool HasRotation = Rotation != identity<quat>();
	const bool HasScalation = Scalation != 1.0;
	const bool HasTranslation = Translation != vec3(0.0, 0.0, 0.0);
	const bool CompileMatrix = HasRotation || HasScalation;
	const bool CompileOffset = HasTranslation && !CompileMatrix;

	if (CompileMatrix)
	{
		const mat4 Matrix = inverse(ToMatrix());
		Program.Push(OpcodeT::Matrix);
		Program.Push(Matrix);
	}
	else if (CompileOffset)
	{
		const vec3 Offset = -Translation;
		Program.Push(OpcodeT::Offset);
		Program.Push(Offset);
	}
}


template<typename ParamsT>
struct BrushNode : public SDFNode
{
	OpcodeT Opcode;
	ParamsT NodeParams;
	BrushMixin BrushFn;
	AABB BrushAABB;
	EvaluatorTransform LocalToWorld;
	MaterialShared Material = nullptr;

	BrushNode(OpcodeT InOpcode, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB)
		: Opcode(InOpcode)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
	{
		StackSize = 1;
	}

	BrushNode(OpcodeT InOpcode, const ParamsT& InNodeParams, BrushMixin& InBrushFn, AABB& InBrushAABB,
		EvaluatorTransform& InLocalToWorld, MaterialShared& InMaterial)
		: Opcode(InOpcode)
		, NodeParams(InNodeParams)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
		, LocalToWorld(InLocalToWorld)
		, Material(InMaterial)
	{
		StackSize = 1;
	}

	virtual float Eval(vec3 Point)
	{
		return BrushFn(LocalToWorld.Transform::ApplyInv(Point)) * LocalToWorld.Scalation;
	}

	virtual SDFNodeShared Clip(vec3 Point, float Radius)
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

	virtual SDFNodeShared Copy()
	{
		return SDFNodeShared(new BrushNode(Opcode, NodeParams, BrushFn, BrushAABB, LocalToWorld, Material));
	}

	virtual AABB Bounds()
	{
		return LocalToWorld.Apply(BrushAABB);
	}

	virtual AABB InnerBounds()
	{
		return Bounds();
	}

	void Compile(ProgramBuffer& Program)
	{
		LocalToWorld.Compile(Program);
		Program.Push(Opcode);
		Program.Push(NodeParams);

		if (LocalToWorld.Scalation != 1.0)
		{
			const float InvScalation = LocalToWorld.Scalation;
			Program.Push(OpcodeT::ScaleField);
			Program.Push(InvScalation);
		}
	}

	virtual void Move(vec3 Offset)
	{
		LocalToWorld.Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		LocalToWorld.Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		LocalToWorld.Scale(Scale);
	}

	virtual void ApplyMaterial(MaterialShared InMaterial, bool Force)
	{
		if (!HasPaint() || Force)
		{
			Material = InMaterial;
		}
	}

	virtual void WalkMaterials(SDFNode::MaterialWalkCallback& Callback)
	{
		Callback(GetMaterial(vec3(0.0, 0.0, 0.0)));
	}

	virtual MaterialShared GetMaterial(glm::vec3 Point)
	{
		if (Material != nullptr)
		{
			return Material;
		}
		else
		{
			return GetDefaultMaterial();
		}
	}

	virtual bool HasPaint()
	{
		return Material != nullptr;
	}

	virtual bool HasFiniteBounds()
	{
		return !(any(isinf(BrushAABB.Min)) || any(isinf(BrushAABB.Max)));
	}

	virtual int LeafCount()
	{
		return 1;
	}

	virtual bool operator==(SDFNode& Other)
	{
		BrushNode* OtherBrush = dynamic_cast<BrushNode*>(&Other);
		if (OtherBrush && OtherBrush->Opcode == Opcode && OtherBrush->Material == Material && OtherBrush->LocalToWorld == LocalToWorld)
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


template<bool ApplyToNegative>
struct StencilMaskNode : public SDFNode
{
	SDFNodeShared Child = nullptr;
	SDFNodeShared StencilMask = nullptr;
	MaterialShared Material = nullptr;

	StencilMaskNode(SDFNodeShared InChild, SDFNodeShared InStencilMask, MaterialShared InMaterial)
		: Child(InChild)
		, StencilMask(InStencilMask)
		, Material(InMaterial)
	{
		StackSize = Child->StackSize;
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(Point);
	}

	virtual SDFNodeShared Clip(vec3 Point, float Radius)
	{
		SDFNodeShared NewChild = Child->Clip(Point, Radius);
		if (NewChild)
		{
			// TODO : we can probably cull on the StencilMask here too
			return SDFNodeShared(new StencilMaskNode<ApplyToNegative>(NewChild, StencilMask->Copy(), Material));
		}
		else
		{
			return nullptr;
		}
	}

	virtual SDFNodeShared Copy()
	{
		return SDFNodeShared(new StencilMaskNode<ApplyToNegative>(Child->Copy(), StencilMask->Copy(), Material));
	}

	virtual AABB Bounds()
	{
		return Child->Bounds();
	}

	virtual AABB InnerBounds()
	{
		return Child->InnerBounds();
	}

	virtual void Compile(ProgramBuffer& Program)
	{
		return Child->Compile(Program);
	}

	virtual void Move(vec3 Offset)
	{
		Child->Move(Offset);
		StencilMask->Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		Child->Rotate(Rotation);
		StencilMask->Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		Child->Scale(Scale);
		StencilMask->Scale(Scale);
	}

	virtual void ApplyMaterial(MaterialShared Material, bool Force)
	{
		// TODO : I'm unsure what the correct behavior is here.
	}

	virtual void WalkMaterials(SDFNode::MaterialWalkCallback& Callback)
	{
		Child->WalkMaterials(Callback);
		Callback(Material);
	}

	virtual MaterialShared GetMaterial(glm::vec3 Point)
	{
		const bool InteriorPoint = (StencilMask->Eval(Point) < 0.0);
		const bool ApplyOverride = InteriorPoint == ApplyToNegative;

		if (ApplyOverride)
		{
			return Material;
		}
		else
		{
			return Child->GetMaterial(Point);
		}
	}

	virtual bool HasPaint()
	{
		return true;
	}

	virtual bool HasFiniteBounds()
	{
		return Child->HasFiniteBounds();
	}

	virtual int LeafCount()
	{
		return Child->LeafCount();
	}

	virtual bool operator==(SDFNode& Other)
	{
		StencilMaskNode* OtherNode = dynamic_cast<StencilMaskNode*>(&Other);

		if (OtherNode)
		{
			const bool ChildrenMatch = (*Child == *(OtherNode->Child));
			const bool StencilsMatch = (*StencilMask == *(OtherNode->StencilMask));
			const bool MaterialsMatch = (*Material == *(OtherNode->Material));
			return ChildrenMatch && StencilsMatch && MaterialsMatch;
		}

		return false;
	}

	virtual ~StencilMaskNode()
	{
		Child.reset();
		StencilMask.reset();
		Material.reset();
	}
};


enum class SetFamily
{
	Union,
	Inter,
	Diff
};


template<SetFamily Family, bool BlendMode>
struct SetNode : public SDFNode
{
	OpcodeT Opcode;
	SetMixin SetFn;
	SDFNodeShared LHS;
	SDFNodeShared RHS;
	float Threshold;

	SetNode(SetMixin& InSetFn, SDFNodeShared InLHS, SDFNodeShared InRHS, float InThreshold)
		: SetFn(InSetFn)
		, LHS(InLHS)
		, RHS(InRHS)
		, Threshold(InThreshold)
	{
		if (Family == SetFamily::Union)
		{
			Opcode = BlendMode ? OpcodeT::BlendUnion : OpcodeT::Union;
		}
		else if (Family == SetFamily::Inter)
		{
			Opcode = BlendMode ? OpcodeT::BlendInter : OpcodeT::Inter;
		}
		else if (Family == SetFamily::Diff)
		{
			Opcode = BlendMode ? OpcodeT::BlendDiff : OpcodeT::Diff;
		}
		else
		{
			Assert(false);
		}
#if 1
		// TODO : Unclear if this optimization is still beneficial anymore.
		if (Family != SetFamily::Diff && RHS->StackSize > LHS->StackSize)
		{
			// When possible, swap the left and right operands to ensure the tree is left leaning.
			// This can reduce the total stack size needed to render the model in interpreted mode,
			// which both improves loading time and the interpreter's steady state performance.
			// This also reduces the number of shader variants compiled for the non-interpreted
			// mode by ensuring equivalent trees have the same form more often.
			std::swap(LHS, RHS);
		}
#endif
		StackSize = max(LHS->StackSize, RHS->StackSize + 1);
	}

	virtual float Eval(vec3 Point)
	{
		return SetFn(
			LHS->Eval(Point),
			RHS->Eval(Point),
			Threshold);
	}

	virtual SDFNodeShared Clip(vec3 Point, float Radius)
	{
		if (Eval(Point) <= Radius)
		{
			if (BlendMode)
			{
				// If both of these clip tests pass, then the point should be in the blending region
				// for all blending set operator types.  If one of these returns nullptr, the other
				// should be deleted.  If we don't return a new blending set node here, fail through
				// to the regular set operator behavior to return an operand, when applicable.

				SDFNodeShared NewLHS = LHS->Clip(Point, Radius + Threshold);
				SDFNodeShared NewRHS = RHS->Clip(Point, Radius + Threshold);
				if (NewLHS && NewRHS)
				{
					return SDFNodeShared(new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS, Threshold));
				}
				else if (NewLHS)
				{
					NewLHS.reset();
				}
				else if (NewRHS)
				{
					NewRHS.reset();
				}
				if (Family == SetFamily::Inter)
				{
					return nullptr;
				}
			}

			SDFNodeShared NewLHS = LHS->Clip(Point, Radius);
			SDFNodeShared NewRHS = RHS->Clip(Point, Radius);

			if (NewLHS && NewRHS)
			{
				// Note, this shouldn't be possible to hit when BlendMode == true.
				return SDFNodeShared(new SetNode<Family, BlendMode>(SetFn, NewLHS, NewRHS, Threshold));
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
					NewRHS.reset();
				}
				return NewLHS;
			}
			else if (Family == SetFamily::Inter)
			{
				// Neither operand is valid.
				if (NewLHS)
				{
					NewLHS.reset();
				}
				else if (NewRHS)
				{
					NewRHS.reset();
				}
				return nullptr;
			}
		}
		return nullptr;
	}

	virtual SDFNodeShared Copy()
	{
		return SDFNodeShared(new SetNode<Family, BlendMode>(SetFn, LHS->Copy(), RHS->Copy(), Threshold));
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

	virtual void Compile(ProgramBuffer& Program)
	{
		LHS->Compile(Program);
		RHS->Compile(Program);
		Program.Push(Opcode);
		if (BlendMode)
		{
			Program.Push(Threshold);
		}
	}

	virtual void Move(vec3 Offset)
	{
		LHS->Move(Offset);
		RHS->Move(Offset);
	}

	virtual void Rotate(quat Rotation)
	{
		LHS->Rotate(Rotation);
		RHS->Rotate(Rotation);
	}

	virtual void Scale(float Scale)
	{
		Threshold *= Scale;
		LHS->Scale(Scale);
		RHS->Scale(Scale);
	}

	virtual void ApplyMaterial(MaterialShared Material, bool Force)
	{
		LHS->ApplyMaterial(Material, Force);
		RHS->ApplyMaterial(Material, Force);
	}

	virtual void WalkMaterials(SDFNode::MaterialWalkCallback& Callback)
	{
		LHS->WalkMaterials(Callback);
		RHS->WalkMaterials(Callback);
	}

	virtual MaterialShared GetMaterial(glm::vec3 Point)
	{
		if (Family == SetFamily::Diff)
		{
			return LHS->GetMaterial(Point);
		}
		else
		{
			const float EvalLHS = LHS->Eval(Point);
			const float EvalRHS = RHS->Eval(Point);
			const float Dist = SetFn(EvalLHS, EvalRHS, Threshold);

			bool TakeLeft;
			if (BlendMode)
			{
				TakeLeft = abs(EvalLHS - Dist) <= abs(EvalRHS - Dist);
			}
			else
			{
				TakeLeft = TakeLeft = (Dist == EvalLHS);
			}

			if (Family == SetFamily::Union)
			{
				if (TakeLeft)
				{
					return LHS->GetMaterial(Point);
				}
				else
				{
					return RHS->GetMaterial(Point);
				}
			}
			else
			{
				const MaterialShared SampleLHS = LHS->GetMaterial(Point);
				const MaterialShared SampleRHS = RHS->GetMaterial(Point);

				const bool LHSValid = LHS->HasPaint();
				const bool RHSValid = RHS->HasPaint();

				if (LHSValid && RHSValid)
				{
					return TakeLeft ? SampleLHS : SampleRHS;
				}
				else if (LHSValid)
				{
					return SampleLHS;
				}
				else
				{
					return SampleRHS;
				}
			}
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

	virtual int LeafCount()
	{
		return LHS->LeafCount() + RHS->LeafCount();
	}

	virtual bool operator==(SDFNode& Other)
	{
		SetNode* OtherSet = dynamic_cast<SetNode*>(&Other);
		if (OtherSet && Opcode == OtherSet->Opcode && Threshold == OtherSet->Threshold)
		{
			return *LHS == *(OtherSet->LHS) && *RHS == *(OtherSet->RHS);
		}
		return false;
	}

	virtual ~SetNode()
	{
		LHS.reset();
		RHS.reset();
	}
};


struct FlateNode : public SDFNode
{
	SDFNodeShared Child;
	float Radius;

	FlateNode(SDFNodeShared InChild, float InRadius)
		: Child(InChild)
		, Radius(InRadius)
	{
		StackSize = Child->StackSize;
	}

	virtual float Eval(vec3 Point)
	{
		return Child->Eval(Point) - Radius;
	}

	virtual SDFNodeShared Clip(vec3 Point, float ClipRadius)
	{
		if (Eval(Point) <= ClipRadius)
		{
			SDFNodeShared NewChild = Child->Clip(Point, ClipRadius + Radius);
			return SDFNodeShared(new FlateNode(NewChild, Radius));
		}
		else
		{
			return nullptr;
		}
	}

	virtual SDFNodeShared Copy()
	{
		return SDFNodeShared(new FlateNode(Child->Copy(), Radius));
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

	virtual void Compile(ProgramBuffer& Program)
	{
		Child->Compile(Program);
		Program.Push(OpcodeT::Flate);
		Program.Push(Radius);
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

	virtual void ApplyMaterial(MaterialShared Material, bool Force)
	{
		Child->ApplyMaterial(Material, Force);
	}

	virtual void WalkMaterials(SDFNode::MaterialWalkCallback& Callback)
	{
		Child->WalkMaterials(Callback);
	}

	virtual MaterialShared GetMaterial(glm::vec3 Point)
	{
		return Child->GetMaterial(Point);
	}

	virtual bool HasPaint()
	{
		return Child->HasPaint();
	}

	virtual bool HasFiniteBounds()
	{
		return Child->HasFiniteBounds();
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
		Child.reset();
	}
};


AABB SymmetricalBounds(vec3 High)
{
	return { High * vec3(-1), High };
}


namespace SDF
{
	void Align(SDFNodeShared& Tree, vec3 Anchors)
	{
		const vec3 Alignment = Anchors * vec3(0.5) + vec3(0.5);
		const AABB Bounds = Tree->InnerBounds();
		const vec3 Offset = mix(Bounds.Min, Bounds.Max, Alignment) * vec3(-1.0);
		Tree->Move(Offset);
	}

	// The following functions perform rotations on SDFNode trees.
	void RotateX(SDFNodeShared& Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, S, 0, 0));
	}

	void RotateY(SDFNodeShared& Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, 0, S, 0));
	}

	void RotateZ(SDFNodeShared& Tree, float Degrees)
	{
		float R = radians(Degrees) * .5;
		float S = sin(R);
		float C = cos(R);
		Tree->Rotate(quat(C, 0, 0, S));
	}


	// The following functions construct Brush nodes.
	SDFNodeShared Sphere(float Radius)
	{
		std::array<float, 1> Params = { Radius };

		BrushMixin Eval = std::bind(SDFMath::Sphere, _1, Radius);

		AABB Bounds = SymmetricalBounds(vec3(Radius));
		return SDFNodeShared(new BrushNode(OpcodeT::Sphere, Params, Eval, Bounds));
	}

	SDFNodeShared Ellipsoid(float RadipodeX, float RadipodeY, float RadipodeZ)
	{
		std::array<float, 3> Params = { RadipodeX, RadipodeY, RadipodeZ };

		using EllipsoidBrushPtr = float(*)(vec3, vec3);
		BrushMixin Eval = std::bind((EllipsoidBrushPtr)SDFMath::Ellipsoid, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));

		AABB Bounds = SymmetricalBounds(vec3(RadipodeX, RadipodeY, RadipodeZ));
		return SDFNodeShared(new BrushNode(OpcodeT::Ellipsoid, Params, Eval, Bounds));
	}

	SDFNodeShared Box(float ExtentX, float ExtentY, float ExtentZ)
	{
		std::array<float, 3> Params = { ExtentX, ExtentY, ExtentZ };

		using BoxBrushPtr = float(*)(vec3, vec3);
		BrushMixin Eval = std::bind((BoxBrushPtr)SDFMath::Box, _1, vec3(ExtentX, ExtentY, ExtentZ));

		AABB Bounds = SymmetricalBounds(vec3(ExtentX, ExtentY, ExtentZ));
		return SDFNodeShared(new BrushNode(OpcodeT::Box, Params, Eval, Bounds));
	}

	SDFNodeShared Torus(float MajorRadius, float MinorRadius)
	{
		std::array<float, 2> Params = { MajorRadius, MinorRadius };

		BrushMixin Eval = std::bind(SDFMath::Torus, _1, MajorRadius, MinorRadius);

		float Radius = MajorRadius + MinorRadius;
		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, MinorRadius));

		return SDFNodeShared(new BrushNode(OpcodeT::Torus, Params, Eval, Bounds));
	}

	SDFNodeShared Cylinder(float Radius, float Extent)
	{
		std::array<float, 2> Params = { Radius, Extent };

		BrushMixin Eval = std::bind(SDFMath::Cylinder, _1, Radius, Extent);

		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Extent));
		return SDFNodeShared(new BrushNode(OpcodeT::Cylinder, Params, Eval, Bounds));
	}

	SDFNodeShared Plane(float NormalX, float NormalY, float NormalZ)
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
		return SDFNodeShared(new BrushNode(OpcodeT::Plane, Params, Eval, Unbound));
	}

	SDFNodeShared Cone(float Radius, float Height)
	{
		float Tangent = Radius / Height;
		std::array<float, 2> Params = { Tangent, Height };

		BrushMixin Eval = std::bind(SDFMath::Cone, _1, Tangent, Height);

		AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Height * .5));
		return SDFNodeShared(new BrushNode(OpcodeT::Cone, Params, Eval, Bounds));
	}

	SDFNodeShared Coninder(float RadiusL, float RadiusH, float Height)
	{
		float HalfHeight = Height * .5;
		std::array<float, 3> Params = { RadiusL, RadiusH, HalfHeight };

		BrushMixin Eval = std::bind(SDFMath::Coninder, _1, RadiusL, RadiusH, HalfHeight);

		float MaxRadius = max(RadiusL, RadiusH);
		AABB Bounds = SymmetricalBounds(vec3(MaxRadius, MaxRadius, HalfHeight));
		return SDFNodeShared(new BrushNode(OpcodeT::Coninder, Params, Eval, Bounds));
	}

	// The following functions construct CSG set operator nodes.
	SDFNodeShared Union(SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::Union, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Union, false>(Eval, LHS, RHS, 0.0));
	}

	SDFNodeShared Diff(SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::Diff, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Diff, false>(Eval, LHS, RHS, 0.0));
	}

	SDFNodeShared Inter(SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::Inter, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Inter, false>(Eval, LHS, RHS, 0.0));
	}

	SDFNodeShared BlendUnion(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::BlendUnion, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Union, true>(Eval, LHS, RHS, Threshold));
	}

	SDFNodeShared BlendDiff(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::BlendDiff, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Diff, true>(Eval, LHS, RHS, Threshold));
	}

	SDFNodeShared BlendInter(float Threshold, SDFNodeShared& LHS, SDFNodeShared& RHS)
	{
		SetMixin Eval = std::bind(SDFMath::BlendInter, _1, _2, _3);
		return SDFNodeShared(new SetNode<SetFamily::Inter, true>(Eval, LHS, RHS, Threshold));
	}

	SDFNodeShared Flate(SDFNodeShared& Node, float Radius)
	{
		return SDFNodeShared(new FlateNode(Node, Radius));
	}

	SDFNodeShared Stencil(SDFNodeShared& Node, SDFNodeShared& StencilMask, MaterialShared& Material, bool ApplyToNegative)
	{
		if (ApplyToNegative)
		{
			return SDFNodeShared(new StencilMaskNode<true>(Node, StencilMask, Material));
		}
		else
		{
			return SDFNodeShared(new StencilMaskNode<false>(Node, StencilMask, Material));
		}
	}
}


// SDFInterpreter function implementations
SDFInterpreter::SDFInterpreter(SDFNodeShared InEvaluator)
	: Root(InEvaluator)
{
	std::string PointVar = "";
	Root->Compile(Program);
	Program.Push(OpcodeT::Stop);
	StackSize = Root->StackSize;
}


float SDFInterpreter::Eval(glm::vec3 EvalPoint)
{
	std::vector<float> Stack;
	Stack.reserve(StackSize);

	size_t ProgramCounter = 0;
	vec3 Point = EvalPoint;

	constexpr bool ValidateStackEstimate = false;
	size_t HighWaterMark = 0;

	while (ProgramCounter < Program.Size())
	{
		if (ValidateStackEstimate)
		{
			HighWaterMark = max(HighWaterMark, Stack.size());
		}

		switch (Program.ReadOpcodeAt(ProgramCounter++))
		{
		case OpcodeT::Stop:
		{
			Assert(ProgramCounter == Program.Size());
			Assert(Stack.size() == 1);
			if (ValidateStackEstimate)
			{
				Assert(HighWaterMark == StackSize);
			}
			return Stack.back();
		}

		case OpcodeT::Sphere:
		{
			const float Radius = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::Sphere(Point, Radius));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Ellipsoid:
		{
			const vec3 Radipodes = Program.ReadVectorAt(ProgramCounter);
			ProgramCounter += 3;
			Stack.push_back(SDFMath::Ellipsoid(Point, Radipodes));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Box:
		{
			const vec3 Extent = Program.ReadVectorAt(ProgramCounter);
			ProgramCounter += 3;
			Stack.push_back(SDFMath::Box(Point, Extent));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Torus:
		{
			const float MajorRadius = Program.ReadScalarAt(ProgramCounter++);
			const float MinorRadius = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::Torus(Point, MajorRadius, MinorRadius));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Cylinder:
		{
			const float Radius = Program.ReadScalarAt(ProgramCounter++);
			const float Extent = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::Cylinder(Point, Radius, Extent));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Cone:
		{
			const float Tangent = Program.ReadScalarAt(ProgramCounter++);
			const float Height = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::Cone(Point, Tangent, Height));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Coninder:
		{
			const float RadiusL = Program.ReadScalarAt(ProgramCounter++);
			const float RadiusH = Program.ReadScalarAt(ProgramCounter++);
			const float Height = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::Coninder(Point, RadiusL, RadiusH, Height));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Plane:
		{
			const vec3 Normal = Program.ReadVectorAt(ProgramCounter);
			ProgramCounter += 3;
			Stack.push_back(SDFMath::Plane(Point, Normal));
			Point = EvalPoint;
			break;
		}

		case OpcodeT::Union:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();

			Stack.push_back(SDFMath::Union(LHS, RHS, 0.0f));
			break;
		}

		case OpcodeT::Inter:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();
#if 0
			// TODO : This is a throwback to an earlier version of the interpreter that supported materials.
			Stack.push_back(SDFMath::InterpretedInterOp(LHS, RHS));
#else
			Stack.push_back(SDFMath::Inter(LHS, RHS, 0.0f));
#endif
			break;
		}

		case OpcodeT::Diff:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();

			Stack.push_back(SDFMath::Diff(LHS, RHS, 0.0f));
			break;
		}

		case OpcodeT::BlendUnion:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();

			const float Threshold = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::BlendUnion(LHS, RHS, Threshold));
			break;
		}

		case OpcodeT::BlendInter:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();

			const float Threshold = Program.ReadScalarAt(ProgramCounter++);
#if 0
			// TODO : This is a throwback to an earlier version of the interpreter that supported materials.
			Stack.push_back(SDFMath::InterpretedSmoothInterOp(LHS, RHS, Threshold));
#else
			Stack.push_back(SDFMath::BlendInter(LHS, RHS, Threshold));
#endif
			break;
		}

		case OpcodeT::BlendDiff:
		{
			const float RHS = Stack.back();
			Stack.pop_back();
			const float LHS = Stack.back();
			Stack.pop_back();

			const float Threshold = Program.ReadScalarAt(ProgramCounter++);
			Stack.push_back(SDFMath::BlendDiff(LHS, RHS, Threshold));
			break;
		}

		case OpcodeT::Flate:
		{
			Stack.back() -= Program.ReadScalarAt(ProgramCounter++);
			break;
		}

		case OpcodeT::Offset:
		{
			Point = EvalPoint + Program.ReadVectorAt(ProgramCounter);
			ProgramCounter += 3;
			break;
		}

		case OpcodeT::Matrix:
		{
			Point = (Program.ReadMatrixAt(ProgramCounter) * vec4(EvalPoint, 1.0)).xyz();
			ProgramCounter += 16;
			break;
		}

		case OpcodeT::ScaleField:
		{
			Stack.back() *= Program.ReadScalarAt(ProgramCounter++);
			break;
		}

		default:
		{
			// Unknown opcode.  Halt and catch fire.
			Assert(false);
			return 0.0;
		}
		};
	}

	// Program terminated without encountering the stop instruction.
	Assert(false);
	return 0.0;
}


// SDFOctree function implementations
SDFOctreeShared SDFOctree::Create(SDFNodeShared& Evaluator, float TargetSize, bool Coalesce, int MaxDepth, float Margin)
{
	if (Evaluator->HasFiniteBounds())
	{
		ProfileScope Fnord("SDFOctree::Create");

		// Determine the octree's bounding cube from the evaluator's bounding box.
		AABB Bounds = Evaluator->Bounds().BoundingCube() + Margin;
		if (Bounds.Volume() == 0.0)
		{
			return nullptr;
		}

		SDFOctree* Tree = new SDFOctree(nullptr, Evaluator, TargetSize, Coalesce, Bounds, 1, MaxDepth);
		if (Tree->Evaluator)
		{
			return SDFOctreeShared(Tree);
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


SDFOctree::SDFOctree(SDFOctree* InParent, SDFNodeShared& InEvaluator, float InTargetSize, bool Coalesce, AABB InBounds, int Depth, int MaxDepth)
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
		EvaluatorLeaves = Evaluator->LeafCount();
	}
	else
	{
		EvaluatorLeaves = 0;
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
		Incomplete = true;
		if (Coalesce || MaxDepth == -1 || Depth < MaxDepth)
		{
			Populate(Coalesce, Depth, MaxDepth);
		}
		else
		{
			for (int i = 0; i < 8; ++i)
			{
				Children[i] = nullptr;
			}
		}
	}
	if (Evaluator)
	{
		Interpreter = SDFInterpreterShared(new SDFInterpreter(Evaluator));
	}

	if (!Parent)
	{
		for (SDFOctree* Child : Children)
		{
			if (Child)
			{
				Next = Child;
				break;
			}
		}
	}
}


void SDFOctree::Populate(bool Coalesce, int Depth, int MaxDepth)
{
	if (Incomplete)
	{
		Incomplete = false;
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
			Children[i] = new SDFOctree(this, Evaluator, TargetSize, Coalesce, ChildBounds, Depth + 1, MaxDepth);
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
			Evaluator.reset();
			Interpreter.reset();
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

			if (Coalesce && ((Penultimate && Uniform) || EvaluatorLeaves <= max(Depth, 3)))
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
		}
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
	Evaluator.reset();
	Interpreter.reset();
}


SDFOctree* SDFOctree::Descend(const vec3 Point, const bool Exact)
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
			SDFOctree* Found = Child->Descend(Point, Exact);
			if (Found || !Exact)
			{
				// If Found is `nullptr`, we return herer anyway if we don't need to empty regions.
				return Found;
			}
		}
		else if (!Exact)
		{
			// This cell is empty, and we don't need to evaluate empty regions, so return nullptr.
			return nullptr;
		}
	}
	return Evaluator ? this : nullptr;
};


SDFNodeShared SDFOctree::SelectEvaluator(const glm::vec3 Point, const bool Exact)
{
	SDFOctree* Match = Descend(Point, Exact);
	if (Match)
	{
		return Match->Evaluator;
	}
	else
	{
		return nullptr;
	}
}


SDFInterpreterShared SDFOctree::SelectInterpreter(const glm::vec3 Point, const bool Exact)
{
	SDFOctree* Match = Descend(Point, Exact);
	if (Match)
	{
		return Match->Interpreter;
	}
	else
	{
		return nullptr;
	}
}


SDFOctree* SDFOctree::LinkLeavesInner(SDFOctree* Cursor, int& OctreeLeafCounter)
{
	if (Terminus)
	{
		if (!Evaluator)
		{
			Next = Cursor;
			return Cursor;
		}
		else
		{
			DebugLeafIndex = OctreeLeafCounter++;
			Next = Cursor;
			return this;
		}
	}
	else
	{
		for (int i = 7; i >= 0; --i)
		{
			SDFOctree* Child = Children[i];
			if (Child)
			{
				Cursor = Child->LinkLeavesInner(Cursor, OctreeLeafCounter);
			}
		}
		Next = Cursor;
		return Cursor;
	}
}


void SDFOctree::LinkLeaves()
{
	Next = LinkLeavesInner(nullptr, OctreeLeafCount);
}


void SDFOctree::Walk(SDFOctree::CallbackType& Callback)
{
	if (Terminus || Incomplete)
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


float SDFOctree::Eval(glm::vec3 Point, const bool Exact)
{
	float Distance;
	{
#if 0
		SDFNodeShared Node = SelectEvaluator(Point, Exact);
#else
		SDFInterpreterShared Node = SelectInterpreter(Point, Exact);
#endif
		if (!Exact && !Node)
		{
			Distance = INFINITY;
		}
		else
		{
			Distance = Node->Eval(Point);
		}
	}

	return Distance;
}
