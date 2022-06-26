
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


#include "extern.h"
#include "profiling.h"
#include "sdfs.h"

using namespace glm;


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
		Clipped->Release();
		return nullptr;
	}
	else
	{
		return Clipped;
	}
}

// Performs a ray hit query against the SDF evaluator.
extern "C" TANGERINE_API RayHit RayMarchTree(
	void* Handle,
	float RayStartX, float RayStartY, float RayStartZ,
	float RayDirX, float RayDirY, float RayDirZ,
	int MaxIterations, float Epsilon)
{
	ProfileScope("RayMarchTree");
	vec3 RayStart = vec3(RayStartX, RayStartY, RayStartZ);
	vec3 RayDir = vec3(RayDirX, RayDirY, RayDirZ);
	return ((SDFNode*)Handle)->RayMarch(RayStart, RayDir, MaxIterations, Epsilon);
}


// Delete a CSG operator tree that was constructed with the functions below.
extern "C" TANGERINE_API void DiscardTree(void* Handle)
{
	ProfileScope("DiscardTree");
	delete ((SDFNode*)Handle);
}

// Returns true if the evaluator has a finite boundary.
extern "C" TANGERINE_API bool TreeHasFiniteBounds(void* Handle)
{
	return ((SDFNode*)Handle)->HasFiniteBounds();
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
	const vec3 Anchors = vec3(X, Y, Z);
	SDF::Align(Tree, Anchors);
}


// Material annotation functions
extern "C" TANGERINE_API void PaintTree(float Red, float Green, float Blue, void* Handle)
{
	SDFNode* Tree = ((SDFNode*)Handle);
	Tree->ApplyMaterial(vec3(Red, Green, Blue), false);
}


// The following functions construct Brush nodes.
extern "C" TANGERINE_API void* MakeSphereBrush(float Radius)
{
	return SDF::Sphere(Radius);
}

extern "C" TANGERINE_API void* MakeEllipsoidBrush(float RadipodeX, float RadipodeY, float RadipodeZ)
{
	return SDF::Ellipsoid(RadipodeX, RadipodeY, RadipodeZ);
}

extern "C" TANGERINE_API void* MakeBoxBrush(float ExtentX, float ExtentY, float ExtentZ)
{
	return SDF::Box(ExtentX, ExtentY, ExtentZ);
}

extern "C" TANGERINE_API void* MakeTorusBrush(float MajorRadius, float MinorRadius)
{
	return SDF::Torus(MajorRadius, MinorRadius);
}

extern "C" TANGERINE_API void* MakeCylinderBrush(float Radius, float Extent)
{
	return SDF::Cylinder(Radius, Extent);
}

extern "C" TANGERINE_API void* MakePlaneOperand(float NormalX, float NormalY, float NormalZ)
{
	return SDF::Plane(NormalX, NormalY, NormalZ);
}


// The following functions construct CSG set operator nodes.
extern "C" TANGERINE_API void* MakeUnionOp(void* LHS, void* RHS)
{
	return SDF::Union((SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeDiffOp(void* LHS, void* RHS)
{
	return SDF::Diff((SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeInterOp(void* LHS, void* RHS)
{
	return SDF::Inter((SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendUnionOp(float Threshold, void* LHS, void* RHS)
{
	return SDF::BlendUnion(Threshold, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendDiffOp(float Threshold, void* LHS, void* RHS)
{
	return SDF::BlendDiff(Threshold, (SDFNode*)LHS, (SDFNode*)RHS);
}

extern "C" TANGERINE_API void* MakeBlendInterOp(float Threshold, void* LHS, void* RHS)
{
	return SDF::BlendInter(Threshold, (SDFNode*)LHS, (SDFNode*)RHS);
}
