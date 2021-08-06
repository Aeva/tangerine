
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
// See the License for the specific language governing permissionsand
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


AABB SphereBrushBounds(float Radius)
{
	return AABB(vec3(0.0), vec3(Radius));
}


float BoxBrush(vec3 Point, vec3 Extent)
{
	vec3 A = abs(Point) - Extent;
	return length(max(A, 0.0)) + min(max(max(A.x, A.y), A.z), 0.0);
}


AABB BoxBrushBounds(vec3 Extent)
{
	return AABB(vec3(0.0), Extent);
}


float UnionOp(float LHS, float RHS)
{
	return min(LHS, RHS);
}


AABB UnionOpBounds(AABB LHS, AABB RHS)
{
	vec3 BoundsMin = min(LHS.Center - LHS.Extent, RHS.Center - RHS.Extent);
	vec3 BoundsMax = max(LHS.Center + LHS.Extent, RHS.Center + RHS.Extent);
	vec3 Extent = (BoundsMax - BoundsMin) * 0.5;
	vec3 Center = BoundsMin + Extent;
	return AABB(Center, Extent);
}


float IntersectionOp(float LHS, float RHS)
{
	return max(LHS, RHS);
}


AABB IntersectionOpBounds(AABB LHS, AABB RHS)
{
	vec3 BoundsMin = max(LHS.Center - LHS.Extent, RHS.Center - RHS.Extent);
	vec3 BoundsMax = min(LHS.Center + LHS.Extent, RHS.Center + RHS.Extent);
	vec3 Extent = max(BoundsMax - BoundsMin, 0.0) * 0.5;
	vec3 Center = BoundsMin + Extent;
	return AABB(Center, Extent);
}


float CutOp(float LHS, float RHS)
{
	return max(LHS, -RHS);
}


AABB CutOpBounds(AABB LHS, AABB RHS)
{
	return LHS;
}


float SmoothUnionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return min(LHS, RHS) - H * H * 0.25 / Threshold;
}


AABB SmoothUnionOpBounds(AABB LHS, AABB RHS, float Threshold)
{
	float MaxH = Threshold * Threshold * 0.25 / Threshold;
	AABB Union = UnionOpBounds(LHS, RHS);
	if (any(greaterThan(Union.Extent, LHS.Extent + RHS.Extent + MaxH)))
	{
		return Union;
	}
	else
	{
		AABB Distortion = IntersectionOpBounds(
			AABB(LHS.Center, LHS.Extent + MaxH),
			AABB(RHS.Center, RHS.Extent + MaxH));
		return UnionOpBounds(Union, Distortion);
	}
}


float SmoothIntersectionOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS - RHS), 0.0);
	return max(LHS, RHS) + H * H * 0.25 / Threshold;
}


AABB SmoothIntersectionOpBounds(AABB LHS, AABB RHS, float Threshold)
{
	return IntersectionOpBounds(LHS, RHS);
}


float SmoothCutOp(float LHS, float RHS, float Threshold)
{
	float H = max(Threshold - abs(LHS + RHS), 0.0);
	return max(LHS, -RHS) + H * H * 0.25 / Threshold;
}


AABB SmoothCutOpBounds(AABB LHS, AABB RHS, float Threshold)
{
	return LHS;
}


AABB TranslateAABB(AABB Bounds, vec3 Offset)
{
	Bounds.Center += Offset;
	return Bounds;
}
