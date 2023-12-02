
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

#ifndef USE_VARIANT_INSTEAD_OF_UNION
#define USE_VARIANT_INSTEAD_OF_UNION 0
#endif

#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <mutex>

#if USE_VARIANT_INSTEAD_OF_UNION
#include <variant>
#endif

#include "glm_common.h"
#include "aabb.h"
#include "colors.h"
#include "errors.h"


enum class MaterialType
{
	Unknown,
	SolidColor,
	PBRBR,
	DebugNormals,
	DebugGradient,
	GradientLight,

	Count
};


struct MaterialInterface
{
	MaterialType Type = MaterialType::Unknown;

	MaterialInterface(MaterialType InType)
		: Type(InType)
	{
	}

	virtual ColorSampler GuessColor() = 0;

	virtual ~MaterialInterface()
	{
	}

	// Because materials are mutable, they are only equal if they share the same memory address.
	bool operator==(MaterialInterface& Other);
};


using MaterialShared = std::shared_ptr<MaterialInterface>;
using MaterialWeakRef = std::weak_ptr<MaterialInterface>;


struct RayHit
{
	bool Hit;
	float Travel;
	glm::vec3 Position;
};


enum class OpcodeT : std::uint32_t
{
	Stop = 0,

	Sphere,
	Ellipsoid,
	Box,
	Torus,
	Cylinder,
	Cone,
	Coninder,
	Plane,

	Union,
	Inter,
	Diff,
	BlendUnion,
	BlendInter,
	BlendDiff,
	Flate,

	Offset,
	Matrix,
	ScaleField,
};


struct ProgramBuffer
{
#if USE_VARIANT_INSTEAD_OF_UNION
	using Word = std::variant<OpcodeT, float>;

#else
	struct Word
	{
		union
		{
			OpcodeT Opcode;
			float Scalar;
		};

		Word(OpcodeT InOpcode)
			: Opcode(InOpcode)
		{
		}

		Word(float InScalar)
			: Scalar(InScalar)
		{
		}
	};
	static_assert(sizeof(Word) == sizeof(float));
#endif

	std::vector<Word> Words;

	size_t Size() const
	{
		return Words.size();
	}

	void Push(const OpcodeT InOpcode);
	void Push(const float InScalar);
	void Push(const glm::vec3& InVector);
	void Push(const glm::mat4& InMatrix);

	template<typename ContainerT>
	void Push(const ContainerT& Params)
	{
		for (const float& Param : Params)
		{
			Push(Param);
		}
	}

	OpcodeT ReadOpcodeAt(const size_t ProgramCounter) const;

	float ReadScalarAt(const size_t ProgramCounter) const;
	glm::vec3 ReadVectorAt(const size_t ProgramCounter) const;
	glm::mat4 ReadMatrixAt(const size_t ProgramCounter) const;
};


struct SDFNode;
using SDFNodeShared = std::shared_ptr<SDFNode>;
using SDFNodeWeakRef = std::weak_ptr<SDFNode>;


struct SDFNode
{
	size_t StackSize = 0;

	virtual float Eval(glm::vec3 Point) = 0;

	virtual SDFNodeShared Clip(glm::vec3 Point, float Radius) = 0;

	virtual SDFNodeShared Copy() = 0;

	virtual AABB Bounds() = 0;

	virtual AABB InnerBounds() = 0;

	virtual void Compile(ProgramBuffer& Program) = 0;

	glm::vec3 Gradient(glm::vec3 Point);

	virtual void Move(glm::vec3 Offset) = 0;

	virtual void Rotate(glm::quat Rotation) = 0;

	virtual void Scale(float Scale) = 0;

	virtual void ApplyMaterial(MaterialShared Material, bool Force) = 0;

	using MaterialWalkCallback = std::function<void(MaterialShared)>;
	virtual void WalkMaterials(MaterialWalkCallback& Callback) = 0;

	virtual MaterialShared GetMaterial(glm::vec3 Point) = 0;

	virtual bool HasPaint() = 0;

	virtual bool HasFiniteBounds() = 0;

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

	SDFNodeShared Stencil(SDFNodeShared& Node, SDFNodeShared& StencilMask, MaterialShared& Material, bool ApplyToNegative);
}


struct SDFInterpreter
{
	SDFNodeShared Root;
	ProgramBuffer Program;
	size_t StackSize;

	SDFInterpreter(SDFNodeShared InEvaluator);

	float Eval(glm::vec3 EvalPoint);
};


using SDFInterpreterShared = std::shared_ptr<SDFInterpreter>;


struct SDFOctree;
using SDFOctreeShared = std::shared_ptr<SDFOctree>;
using SDFOctreeWeakRef = std::weak_ptr<SDFOctree>;


struct SDFOctree
{
	// These are to simplify metaprogramming in ParallelMeshingTask.
	using value_type = SDFOctree;
	using iterator = void*;

	AABB Bounds;
	glm::vec3 Pivot;
	float TargetSize;
	bool Terminus;
	int EvaluatorLeaves;
	int OctreeLeafCount = 0;
	int DebugLeafIndex = -1;
	SDFNodeShared Evaluator;
	SDFOctree* Children[8];
	SDFOctree* Parent;
	SDFOctree* Next = nullptr;

	SDFInterpreterShared Interpreter;
	bool Incomplete = false;

	static SDFOctreeShared Create(SDFNodeShared& Evaluator, float TargetSize = 0.25, bool Coalesce = true, int MaxDepth = -1, float Margin = 0.0);
	void Populate(bool Coalesce, int Depth, int MaxDepth = -1);
	~SDFOctree();
	SDFOctree* Descend(const glm::vec3 Point, const bool Exact = true);
	SDFNodeShared SelectEvaluator(const glm::vec3 Point, const bool Exact = true);
	SDFInterpreterShared SelectInterpreter(const glm::vec3 Point, const bool Exact = true);

	SDFOctree* LinkLeavesInner(SDFOctree* Previous, int& LeafCounter);
	void LinkLeaves();

	using CallbackType = std::function<void(SDFOctree&)>;
	void Walk(CallbackType& Callback);

	float Eval(glm::vec3 Point, const bool Exact = true);
	glm::vec3 Gradient(glm::vec3 Point)
	{
		SDFNodeShared Node = SelectEvaluator(Point);
		return Node->Gradient(Point);
	}
	MaterialShared GetMaterial(glm::vec3 Point)
	{
		SDFNodeShared Node = SelectEvaluator(Point);
		return Node->GetMaterial(Point);
	}

private:
	SDFOctree(SDFOctree* InParent, SDFNodeShared& InEvaluator, float InTargetSize, bool Coalesce, AABB InBounds, int Depth, int MaxDepth);
};
