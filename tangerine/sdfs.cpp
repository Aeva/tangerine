
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
#include <sstream>
#include <functional>
#include <cmath>

#ifdef MINIMAL_DLL
typedef void* ptr;
#define Snil nullptr
#define Sinteger(X) nullptr
#define Sflonum(X) nullptr
#define Sstring_to_symbol(X) nullptr
#define Scons(...) nullptr

#else
#include <chezscheme.h>
#include <racketcs.h>
#endif

#include "sdfs.h"
#include "profiling.h"


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


using SymbolMixin = std::function<ptr(ptr)>;
using BrushMixin = std::function<float(vec3)>;
using TransformMixin = std::function<vec3(vec3)>;
using PointMixin = std::function<std::string(const int, const std::string&)>;
using SetMixin = std::function<float(float, float)>;


// The following functions are to make constructing scheme lists a little cleaner.
ptr SchemeThing(ptr List)
{
	return List;
}

ptr SchemeThing(int Number)
{
	return Sinteger(Number);
}

ptr SchemeThing(double Number)
{
	return Sflonum(Number);
}

ptr SchemeThing(const char* Symbol)
{
	return Sstring_to_symbol(Symbol);
}

ptr SchemeThing(vec3 Vector);

template<typename Head>
ptr SchemeList(Head Item)
{
	return Scons(SchemeThing(Item), Snil);
}

template<typename Head, typename... Tail>
ptr SchemeList(Head Item, Tail... Next)
{
	return Scons(SchemeThing(Item), SchemeList(Next...));
}

ptr SchemeThing(vec3 Vector)
{
	return SchemeList(Vector.x, Vector.y, Vector.z);
}


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
	std::ostringstream Stream;
	Stream << "PARAMS[" << Offset++ << "]";
	for (int i = 1; i < Count; ++i)
	{
		Stream << ", PARAMS[" << Offset++ << "]";
	}
	return Stream.str();
}


// The following structs are used to implement executable signed distance
// functions, to be constructed indirectly from Racket.


template<typename ParamsT>
struct TransformNode : public SDFNode
{
	PointMixin PointFn;
	ParamsT NodeParams;
	SymbolMixin SymbolFn;
	TransformMixin TransformFn;
	TransformMixin InverseFn;
	SDFNode* Child;

	TransformNode(PointMixin& InPointFn, ParamsT& InNodeParams, SymbolMixin& InSymbolFn, TransformMixin& InTransformFn, TransformMixin& InInverseFn, SDFNode* InChild)
		: PointFn(InPointFn)
		, NodeParams(InNodeParams)
		, SymbolFn(InSymbolFn)
		, TransformFn(InTransformFn)
		, InverseFn(InInverseFn)
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
			return new TransformNode(PointFn, NodeParams, SymbolFn, TransformFn, InverseFn, NewChild);
		}
		else
		{
			return nullptr;
		}
	}

	virtual AABB Bounds()
	{
		const AABB ChildBounds = Child->Bounds();
		const vec3 A = ChildBounds.Min;
		const vec3 B = ChildBounds.Max;

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
		Bounds.Min = InverseFn(A);
		Bounds.Max = Bounds.Min;

		for (const vec3& Point : Points)
		{
			const vec3 Tmp = InverseFn(Point);
			Bounds.Min = min(Bounds.Min, Tmp);
			Bounds.Max = max(Bounds.Max, Tmp);
		}

		return Bounds;
	}

	virtual ptr Quote()
	{
		return SymbolFn(Child->Quote());
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		const int Offset = StoreParams(TreeParams, NodeParams);
		std::string NewPoint = PointFn(Offset, Point);
		return Child->Compile(TreeParams, NewPoint);
	}

	virtual ~TransformNode()
	{
		delete Child;
	}
};


template<typename ParamsT>
struct BrushNode : public SDFNode
{
	std::string BrushFnName;
	ParamsT NodeParams;
	SymbolMixin SymbolFn;
	BrushMixin BrushFn;
	AABB BrushAABB;

	BrushNode(const std::string& InBrushFnName, const ParamsT& InNodeParams, SymbolMixin& InSymbolFn, BrushMixin& InBrushFn, AABB& InBrushAABB)
		: BrushFnName(InBrushFnName)
		, NodeParams(InNodeParams)
		, SymbolFn(InSymbolFn)
		, BrushFn(InBrushFn)
		, BrushAABB(InBrushAABB)
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
			return new BrushNode(BrushFnName, NodeParams, SymbolFn, BrushFn, BrushAABB);
		}
		else
		{
			return nullptr;
		}
	}

	virtual AABB Bounds()
	{
		return BrushAABB;
	}

	virtual ptr Quote()
	{
		return SymbolFn(Snil);
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		const int Offset = StoreParams(TreeParams, NodeParams);
		std::ostringstream Stream;
		Stream << BrushFnName << "(" << Point << ", " << MakeParamList(Offset, (int)NodeParams.size()) << ")";
		return Stream.str();
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
	SymbolMixin SymbolFn;
	SetMixin SetFn;
	SDFNode* LHS;
	SDFNode* RHS;
	float Threshold;

	SetNode(SymbolMixin& InSymbolFn, SetMixin& InSetFn, SDFNode* InLHS, SDFNode* InRHS, float InThreshold)
		: SymbolFn(InSymbolFn)
		, SetFn(InSetFn)
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
				return new SetNode<Family, BlendMode>(SymbolFn, SetFn, NewLHS, NewRHS, Threshold);
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
			return new SetNode<Family, BlendMode>(SymbolFn, SetFn, NewLHS, NewRHS, Threshold);
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

	virtual ptr Quote()
	{
		return SymbolFn(SchemeList(LHS->Quote(), RHS->Quote()));
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		const std::string CompiledLHS = LHS->Compile(TreeParams, Point);
		const std::string CompiledRHS = RHS->Compile(TreeParams, Point);

		if (BlendMode)
		{
			const int Offset = (int)TreeParams.size();
			TreeParams.push_back(Threshold);

			auto Formatter = [&](const char* OpName)
			{
				std::ostringstream Stream;
				Stream << OpName << "(" << CompiledLHS << ", " << CompiledRHS << ", PARAMS[" << Offset << "])";
				return Stream.str();
			};

			if (Family == SetFamily::Union)
			{
				return Formatter("SmoothUnionOp");
			}
			else if (Family == SetFamily::Diff)
			{
				return Formatter("SmoothCutOp");
			}
			else if (Family == SetFamily::Inter)
			{
				return Formatter("SmoothIntersectionOp");
			}
		}
		else
		{
			auto Formatter = [&](const char* OpName)
			{
				std::ostringstream Stream;
				Stream << OpName << "(" << CompiledLHS << ", " << CompiledRHS << ")";
				return Stream.str();
			};

			if (Family == SetFamily::Union)
			{
				return Formatter("UnionOp");
			}
			else if (Family == SetFamily::Diff)
			{
				return Formatter("CutOp");
			}
			else if (Family == SetFamily::Inter)
			{
				return Formatter("IntersectionOp");
			}
		}
	}

	virtual ~SetNode()
	{
		delete LHS;
		delete RHS;
	}
};


struct PaintNode : public SDFNode
{
	int Material;
	SDFNode* Child;

	PaintNode(int InMaterial, SDFNode* InChild)
		: Material(InMaterial)
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
			return new PaintNode(Material, NewChild);
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

	virtual ptr Quote()
	{
		return SchemeList("paint", Material, Child->Quote());
	}

	virtual std::string Compile(std::vector<float>& TreeParams, std::string& Point)
	{
		std::ostringstream Stream;
		Stream << "MaterialDist(" << Material << ", " << Child->Compile(TreeParams, Point) << ")";
		return Stream.str();
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

// Extract the bounds of a SDF tree.
extern "C" TANGERINE_API ptr TreeBounds(void* Handle)
{
	ProfileScope("TreeBounds");
	AABB Tmp = ((SDFNode*)Handle)->Bounds();

	return SchemeList(
		SchemeList(Tmp.Min.x, Tmp.Min.y, Tmp.Min.z),
		SchemeList(Tmp.Max.x, Tmp.Max.y, Tmp.Max.z));
}

// Return the csgst representation of a SDF tree.
extern "C" TANGERINE_API ptr QuoteTree(void* Handle)
{
	ProfileScope("QuoteTree");
	return ((SDFNode*)Handle)->Quote();
}

// Delete a CSG operator tree that was constructed with the functions below.
extern "C" TANGERINE_API void DiscardTree(void* Handle)
{
	ProfileScope("DiscardTree");
	delete (SDFNode*)Handle;
}


// The following functions construct transform nodes.
extern "C" TANGERINE_API void* MakeTranslation(float X, float Y, float Z, void* Child)
{
	vec3 Offset(X, Y, Z);

	std::array<float, 3> Params = { X, Y, Z };

	PointMixin PointFn = PointMixin(
		[](const int Offset, const std::string& Point) -> std::string
		{
			std::ostringstream Stream;
			Stream << "(" << Point << " - vec3(" << MakeParamList(Offset, 3) << "))";
			return Stream.str();
		});

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Child) -> ptr
		{
			return SchemeList("move", X, Y, Z, Child);
		});

	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point - Offset;
		});

	TransformMixin Inverse = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			return Point + Offset;
		});

	return new TransformNode(PointFn, Params, Quote, Eval, Inverse, (SDFNode*)Child);
}


extern "C" TANGERINE_API void* MakeMatrixTransform(
	float X1, float Y1, float Z1, float W1,
	float X2, float Y2, float Z2, float W2,
	float X3, float Y3, float Z3, float W3,
	float X4, float Y4, float Z4, float W4,
	void* Child)
{
	std::array<float, 16> Params = \
	{
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4
	};

	mat4 Matrix(
		X1, Y1, Z1, W1,
		X2, Y2, Z2, W2,
		X3, Y3, Z3, W3,
		X4, Y4, Z4, W4);

	mat4 InvMatrix = inverse(Matrix);

	PointMixin PointFn = PointMixin(
		[](const int Offset, const std::string& Point) -> std::string
		{
			std::ostringstream Stream;
			Stream << "MatrixTransform(" << Point << ", mat4(" << MakeParamList(Offset, 16) << "))";
			return Stream.str();
		});

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Child) -> ptr
		{
			return SchemeList(
				"mat4",
				SchemeList(
					SchemeList(X1, Y1, Z1, W1),
					SchemeList(X2, Y2, Z2, W2),
					SchemeList(X3, Y3, Z3, W3),
					SchemeList(X4, Y4, Z4, W4)),
				Child);
		});

	TransformMixin Eval = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = Matrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});

	TransformMixin Inverse = TransformMixin(
		[=](vec3 Point) -> vec3
		{
			vec4 Tmp = InvMatrix * vec4(Point, 1.0);
			return Tmp.xyz / Tmp.www;
		});

	return new TransformNode(PointFn, Params, Quote, Eval, Inverse, (SDFNode*)Child);
}


AABB SymmetricalBounds(vec3 High)
{
	return { High * vec3(-1), High };
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(float Radius)
{
	std::array<float, 1> Params = { Radius };

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Unused) -> ptr
		{
			return SchemeList("sphere", Radius);
		});

	BrushMixin Eval = std::bind(SDF::SphereBrush, _1, Radius);

	AABB Bounds = SymmetricalBounds(vec3(Radius));
	return new BrushNode("SphereBrush", Params, Quote, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(float RadipodeX, float RadipodeY, float RadipodeZ)
{
	std::array<float, 3> Params = { RadipodeX, RadipodeY, RadipodeZ };

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Unused) -> ptr
		{
			return SchemeList("ellipsoid", RadipodeX, RadipodeY, RadipodeZ);
		});

	BrushMixin Eval = std::bind(SDF::EllipsoidBrush, _1, vec3(RadipodeX, RadipodeY, RadipodeZ));

	AABB Bounds = SymmetricalBounds(vec3(RadipodeX, RadipodeY, RadipodeZ));
	return new BrushNode("UnwrappedEllipsoidBrush", Params, Quote, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeBoxBrush(float ExtentX, float ExtentY, float ExtentZ)
{
	std::array<float, 3> Params = { ExtentX, ExtentY, ExtentZ };

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Unused) -> ptr
		{
			return SchemeList("box", ExtentX, ExtentY, ExtentZ);
		});

	BrushMixin Eval = std::bind(SDF::BoxBrush, _1, vec3(ExtentX, ExtentY, ExtentZ));

	AABB Bounds = SymmetricalBounds(vec3(ExtentX, ExtentY, ExtentZ));
	return new BrushNode("UnwrappedBoxBrush", Params, Quote, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeTorusBrush(float MajorRadius, float MinorRadius)
{
	std::array<float, 2> Params = { MajorRadius, MinorRadius };

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Unused) -> ptr
		{
			return SchemeList("torus", MajorRadius, MinorRadius);
		});

	BrushMixin Eval = std::bind(SDF::TorusBrush, _1, MajorRadius, MinorRadius);

	float Radius = MajorRadius + MinorRadius;
	AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, MinorRadius));

	return new BrushNode("TorusBrush", Params, Quote, Eval, Bounds);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(float Radius, float Extent)
{
	std::array<float, 2> Params = { Radius, Extent };

	SymbolMixin Quote = SymbolMixin(
		[=](ptr Unused) -> ptr
		{
			return SchemeList("cylinder", Radius, Extent);
		});

	BrushMixin Eval = std::bind(SDF::CylinderBrush, _1, Radius, Extent);

	AABB Bounds = SymmetricalBounds(vec3(Radius, Radius, Extent));
	return new BrushNode("CylinderBrush", Params, Quote, Eval, Bounds);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("union"), Operands);
		});

	SetMixin Eval = std::bind(SDF::UnionOp, _1, _2);
	return new SetNode<SetFamily::Union, false>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("diff"), Operands);
		});

	SetMixin Eval = std::bind(SDF::CutOp, _1, _2);
	return new SetNode<SetFamily::Diff, false>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("inter"), Operands);
		});

	SetMixin Eval = std::bind(SDF::IntersectionOp, _1, _2);
	return new SetNode<SetFamily::Inter, false>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, 0.0);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("blend-union"), Scons(Sflonum(Threshold), Operands));
		});

	SetMixin Eval = std::bind(SDF::SmoothUnionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Union, true>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("blend-diff"), Scons(Sflonum(Threshold), Operands));
		});

	SetMixin Eval = std::bind(SDF::SmoothCutOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Diff, true>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	SymbolMixin Quote = SymbolMixin(
		[=](ptr Operands) -> ptr
		{
			return Scons(Sstring_to_symbol("blend-inter"), Scons(Sflonum(Threshold), Operands));
		});

	SetMixin Eval = std::bind(SDF::SmoothIntersectionOp, _1, _2, Threshold);
	return new SetNode<SetFamily::Inter, true>(Quote, Eval, (SDFNode*)LHS, (SDFNode*)RHS, Threshold);
}


// Misc nodes
extern "C" TANGERINE_API void* MakePaint(int Material, void* Child)
{
	return new PaintNode(Material, (SDFNode*)Child);
}
