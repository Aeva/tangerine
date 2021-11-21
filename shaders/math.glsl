
// Copyright 2021 Aeva Palecek
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


struct AABB
{
	vec3 Center;
	vec3 Extent;
};


float SphereBrush(vec3 Point, float Radius)
{
	return length(Point) - Radius;
}


float EllipsoidBrush(vec3 Point, vec3 Radipodes)
{
	float K0 = length(Point / Radipodes);
	float K1 = length(Point / (Radipodes * Radipodes));
	return K0 * (K0 - 1.0) / K1;
}


float BoxBrush(vec3 Point, vec3 Extent)
{
	vec3 A = abs(Point) - Extent;
	return length(max(A, 0.0)) + min(max(max(A.x, A.y), A.z), 0.0);
}


float TorusBrush(vec3 Point, float MajorRadius, float MinorRadius)
{
	return length(vec2(length(Point.xy) - MajorRadius, Point.z)) - MinorRadius;
}


float CylinderBrush(vec3 Point, float Radius, float Extent)
{
	vec2 D = abs(vec2(length(Point.xy), Point.z)) - vec2(Radius, Extent);
	return min(max(D.x, D.y), 0.0) + length(max(D, 0.0));
}


struct MaterialDist
{
	uint Material;
	float Dist;
};


float UnionOp(float LHS, float RHS)
{
	return min(LHS, RHS);
}


float IntersectionOp(float LHS, float RHS)
{
	return max(LHS, RHS);
}


float CutOp(float LHS, float RHS)
{
	return max(LHS, -RHS);
}


#define BINARY_OP_ROUTES(Function) \
MaterialDist Function##(float LHS, MaterialDist RHS) \
{ \
	MaterialDist NewLHS = MaterialDist(0, LHS); \
	return Function##(NewLHS, RHS); \
} \
\
\
MaterialDist Function##(MaterialDist LHS, float RHS) \
{ \
	MaterialDist NewRHS = MaterialDist(0, RHS); \
	return Function##(LHS, NewRHS); \
}


#define BINARY_OP_VARIANTS(Function) \
MaterialDist Function##(MaterialDist LHS, MaterialDist RHS) \
{ \
	float Dist = Function##(LHS.Dist, RHS.Dist); \
	uint Material = (Dist == LHS.Dist) ? LHS.Material : RHS.Material; \
	return MaterialDist(Material, Dist); \
} \
BINARY_OP_ROUTES(Function)


BINARY_OP_VARIANTS(UnionOp)
BINARY_OP_VARIANTS(IntersectionOp)


MaterialDist CutOp(MaterialDist LHS, MaterialDist RHS)
{
	return MaterialDist(LHS.Material, max(LHS.Dist, -RHS.Dist));
}
BINARY_OP_ROUTES(CutOp)


float SmoothUnionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return min(LHS, RHS) - H * H * 0.25 / Threshold;
}


float SmoothIntersectionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return max(LHS, RHS) + H * H * 0.25 / Threshold;
}


float SmoothCutOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS + RHS), 0.0);
	return max(LHS, -RHS) + H * H * 0.25 / Threshold;
}


#define BLEND_OP_ROUTES(Function) \
MaterialDist Function##(float LHS, MaterialDist RHS, float Threshold) \
{ \
	MaterialDist NewLHS = MaterialDist(0, LHS); \
	return Function##(NewLHS, RHS, Threshold); \
} \
\
\
MaterialDist Function##(MaterialDist LHS, float RHS, float Threshold) \
{ \
	MaterialDist NewRHS = MaterialDist(0, RHS); \
	return Function##(LHS, NewRHS, Threshold); \
}


#define BLEND_OP_VARIANTS(Function) \
MaterialDist Function##(MaterialDist LHS, MaterialDist RHS, float Threshold) \
{ \
	float Dist = Function(LHS.Dist, RHS.Dist, Threshold); \
	uint Material = (abs(LHS.Dist - Dist) <= abs(RHS.Dist - Dist)) ? LHS.Material : RHS.Material; \
	return MaterialDist(Material, Dist); \
} \
BLEND_OP_ROUTES(Function)


BLEND_OP_VARIANTS(SmoothUnionOp)
BLEND_OP_VARIANTS(SmoothIntersectionOp)


MaterialDist SmoothCutOp(MaterialDist LHS, MaterialDist RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS.Dist + RHS.Dist), 0.0);
	float Dist = max(LHS.Dist, -RHS.Dist) + H * H * 0.25 / Threshold;
	return MaterialDist(LHS.Material, Dist);
}
BLEND_OP_ROUTES(SmoothCutOp)


MaterialDist TreeRoot(MaterialDist Dist)
{
	return Dist;
}


MaterialDist TreeRoot(float Dist)
{
	return MaterialDist(0, Dist);
}


vec3 MatrixTransform(vec3 Point, mat4 Transform)
{
	return (Transform * vec4(Point, 1.0)).xyz;
}


vec3 QuaternionTransform(vec3 Point, vec4 Quat)
{
	vec2 Sign = vec2(1.0, -1.0);
	vec4 Tmp = vec4(
		dot(Sign.xyx * Quat.wzy, Point),
		dot(Sign.xxy * Quat.zwx, Point),
		dot(Sign.yxx * Quat.yxw, Point),
		dot(Sign.yyy * Quat.xyz, Point));
	return vec3(
		dot(Sign.xyxy * Quat.wzyx, Tmp),
		dot(Sign.xxyy * Quat.zwxy, Tmp),
		dot(Sign.yxxy * Quat.yxwz, Tmp));
}


void BoundingRect(mat4 ViewToClip, mat4 LocalToView, AABB Bounds, out vec3 ClipMin, out vec3 ClipMax)
{
	vec3 A = Bounds.Center - Bounds.Extent;
	vec3 B = Bounds.Center + Bounds.Extent;

	vec4 Tmp;
#define TRANSFORM(Point) \
	Tmp = (LocalToView * vec4(Point, 1.0));\
	Tmp /= Tmp.w; \
	Tmp = (ViewToClip * Tmp); \
	Tmp.xyz /= Tmp.w; \
	Tmp.z = 1.0 - Tmp.z;

	TRANSFORM(A);
	ClipMin = Tmp.xyz;
	ClipMax = ClipMin;

#define CRANK(Point) \
	TRANSFORM(Point); \
	ClipMin = min(ClipMin, Tmp.xyz); \
	ClipMax = max(ClipMax, Tmp.xyz);

	CRANK(B);
	CRANK(vec3(B.x, A.yz));
	CRANK(vec3(A.x, B.y, A.z));
	CRANK(vec3(A.xy, B.z));
	CRANK(vec3(A.x, B.yz));
	CRANK(vec3(B.x, A.y, B.z));
	CRANK(vec3(B.xy, A.z));

#undef CRANK
#undef TRANSFORM
}


struct ClipRect
{
	vec3 ClipMin;
	vec3 ClipMax;
};


bool ClipTest(vec4 TileClip, ClipRect Rect)
{
	return Rect.ClipMin.z <= 1.0 && \
		all(lessThanEqual(TileClip.xy, Rect.ClipMax.xy)) && \
		all(lessThanEqual(Rect.ClipMin.xy, TileClip.zw));
}


float MaxComponent(vec3 Vector)
{
	return max(max(Vector.x, Vector.y), Vector.z);
}


float MinComponent(vec3 Vector)
{
	return min(min(Vector.x, Vector.y), Vector.z);
}


bool RayHitAABB(vec3 RayStart, vec3 RayDir, AABB Bounds, out vec3 Enter)
{
	vec3 InvRayDir = vec3(1.0 / RayDir);
	vec3 A = InvRayDir * (RayStart - Bounds.Center);
	vec3 B = abs(InvRayDir) * Bounds.Extent;
	vec3 PointNear = -A - B;
	vec3 PointFar = -A + B;
	float TravelNear = MaxComponent(PointNear);
	float TravelFar = MinComponent(PointFar);
	Enter = clamp(RayDir * TravelNear + RayStart, Bounds.Center - Bounds.Extent, Bounds.Center + Bounds.Extent);
	return TravelNear < TravelFar && TravelFar > 0.0;
}
