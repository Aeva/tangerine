--------------------------------------------------------------------------------

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


layout(std140, binding = 0)
uniform ViewInfoBlock
{
	mat4 WorldToView;
	mat4 ViewToWorld;
	mat4 ViewToClip;
	mat4 ClipToView;
	vec4 CameraOrigin;
	vec4 ScreenSize;
	vec4 ModelMin;
	vec4 ModelMax;
	float CurrentTime;
	bool Perspective;
};


layout(location = 0) out vec4 OutColor;


bool PlaneIntersect(vec3 RayStart, vec3 RayDir, vec3 PlaneNormal, float PlaneOffset, out vec3 Intersection, out float Dist)
{
	float NoD = dot(PlaneNormal, RayDir);
	if (NoD >= 0.0)
	{
		return false;
	}
	Dist = -((dot(PlaneNormal, RayStart) + abs(PlaneOffset)) / NoD);
	Intersection = RayDir * Dist + RayStart;
	return true;
}


void DrawPlane(vec3 RayStart, vec3 RayDir, vec3 PlaneNormal, float PlaneOffset, inout float BestDist, inout vec3 BestNormal, inout vec3 BestIntersect)
{
	vec3 Intersect;
	float Dist;
	bool Hit = PlaneIntersect(RayStart, RayDir, PlaneNormal, PlaneOffset, Intersect, Dist);
	if (Hit && Dist < BestDist)
	{
		BestDist = Dist;
		BestNormal = PlaneNormal;
		BestIntersect = Intersect;
	}
}


float SdPlus(vec2 Position)
{
	Position = abs(fract(Position) - 0.5);
	float Box1;
	{
		vec2 d = Position - vec2(0.03, 0.2);
		Box1 = length(max(d,0.0)) + min(max(d.x,d.y),0.0);
	}
	float Box2;
	{
		vec2 d = Position - vec2(0.2, 0.03);
		Box2 = length(max(d,0.0)) + min(max(d.x,d.y),0.0);
	}
	return min(Box1, Box2);
}


float SdMinus(vec2 Position)
{
	vec2 d = abs(fract(Position) - 0.5) - vec2(0.2, 0.03);
	return length(max(d,0.0)) + min(max(d.x,d.y),0.0);
}


void main()
{
	vec2 NDC = gl_FragCoord.xy * ScreenSize.zw * 2.0 - 1.0;

	vec4 View;
	vec3 Origin;
	if (Perspective)
	{
		vec4 Clip = vec4(NDC, -1.0, 1.0);
		View = ClipToView * Clip;
		View /= View.w;
		Origin = CameraOrigin.xyz;
	}
	else
	{
		vec4 Clip = vec4(NDC, 1.0, 1.0);
		View = ClipToView * Clip;
		View /= View.w;
		vec4 Tmp = ViewToWorld * vec4(View.xy, 0.0, View.w);
		Origin = Tmp.xyz / Tmp.w;
	}

	vec4 World = ViewToWorld * View;
	World /= World.w;
	vec3 EyeRay = normalize(World.xyz - Origin);

	float BestDist = 1.0 / 0.0;
	vec3 BestNormal = vec3(0.0, 0.0, 0.0);
	vec3 BestIntersect = vec3(0.0, 0.0, 0.0);

	float ClipAngle = 0.25;

	bvec3 DrawMin;
	bvec3 DrawMax;

	if (Perspective)
	{
		vec3 FocalPoint = vec3(0.0, 0.0, 0.0);
		vec3 Stare = normalize(Origin - FocalPoint);
		float TowardsX = abs(dot(Stare, vec3(1.0, 0.0, 0.0)));
		float TowardsY = abs(dot(Stare, vec3(0.0, 1.0, 0.0)));
		float TowardsZ = abs(dot(Stare, vec3(0.0, 0.0, 1.0)));

		DrawMin.x = Origin.x < ModelMax.x && TowardsX > ClipAngle;
		DrawMin.y = Origin.y < ModelMax.y && TowardsY > ClipAngle;
		DrawMin.z = Origin.z < ModelMax.z && TowardsZ > ClipAngle;
		DrawMax.x = Origin.x > ModelMin.x && TowardsX > ClipAngle;
		DrawMax.y = Origin.y > ModelMin.y && TowardsY > ClipAngle;
		DrawMax.z = Origin.z > ModelMin.z && TowardsZ > ClipAngle;
	}
	else
	{
		DrawMin.x = dot(EyeRay, vec3(1.0, 0.0, 0.0)) > ClipAngle;
		DrawMin.y = dot(EyeRay, vec3(0.0, 1.0, 0.0)) > ClipAngle;
		DrawMin.z = dot(EyeRay, vec3(0.0, 0.0, 1.0)) > ClipAngle;
		DrawMax.x = dot(EyeRay, vec3(-1.0, 0.0, 0.0)) > ClipAngle;
		DrawMax.y = dot(EyeRay, vec3(0.0, -1.0, 0.0)) > ClipAngle;
		DrawMax.z = dot(EyeRay, vec3(0.0, 0.0, -1.0)) > ClipAngle;
	}

	if (DrawMin.x)
	{
		DrawPlane(Origin, EyeRay, vec3(-1.0, 0.0, 0.0), ModelMax.x, BestDist, BestNormal, BestIntersect);
	}
	if (DrawMin.y)
	{
		DrawPlane(Origin, EyeRay, vec3(0.0, -1.0, 0.0), ModelMax.y, BestDist, BestNormal, BestIntersect);
	}
	if (DrawMin.z)
	{
		DrawPlane(Origin, EyeRay, vec3(0.0, 0.0, -1.0), ModelMax.z, BestDist, BestNormal, BestIntersect);
	}

	if (DrawMax.x)
	{
		DrawPlane(Origin, EyeRay, vec3(1.0, 0.0, 0.0), ModelMin.x, BestDist, BestNormal, BestIntersect);
	}
	if (DrawMax.y)
	{
		DrawPlane(Origin, EyeRay, vec3(0.0, 1.0, 0.0), ModelMin.y, BestDist, BestNormal, BestIntersect);
	}
	if (DrawMax.z)
	{
		DrawPlane(Origin, EyeRay, vec3(0.0, 0.0, 1.0), ModelMin.z, BestDist, BestNormal, BestIntersect);
	}

	OutColor = vec4(0.5, 0.5, 0.5, 1.0);
	if (BestDist != 1.0 / 0.0)
	{
		float Highlight = dot(BestNormal, -EyeRay) * 0.5 + 0.5;
		OutColor.rgb = vec3(Highlight);
		float LineDistX = min(fract(BestIntersect.x), 1.0 - fract(BestIntersect.x));
		float LineDistY = min(fract(BestIntersect.y), 1.0 - fract(BestIntersect.y));
		float LineDistZ = min(fract(BestIntersect.z), 1.0 - fract(BestIntersect.z));
		float LineHalf = 0.05;
		bool BoundsLineX = min(distance(BestIntersect.x, ModelMin.x), distance(BestIntersect.x, ModelMax.x)) < LineHalf;
		bool BoundsLineY = min(distance(BestIntersect.y, ModelMin.y), distance(BestIntersect.y, ModelMax.y)) < LineHalf;
		bool BoundsLineZ = min(distance(BestIntersect.z, ModelMin.z), distance(BestIntersect.z, ModelMax.z)) < LineHalf;
		bool WithinBoundsX = ModelMin.x <= BestIntersect.x && BestIntersect.x <= ModelMax.x;
		bool WithinBoundsY = ModelMin.y <= BestIntersect.y && BestIntersect.y <= ModelMax.y;
		bool WithinBoundsZ = ModelMin.z <= BestIntersect.z && BestIntersect.z <= ModelMax.z;
		bool TileX = int(ceil(BestIntersect.x)) % 2 == 0;
		bool TileY = int(ceil(BestIntersect.y)) % 2 == 0;
		bool TileZ = int(ceil(BestIntersect.z)) % 2 == 0;
		vec3 Gray = vec3(0.4) * Highlight;
		vec3 Red = vec3(1.0, 0.25, 0.25) * Highlight;
		vec3 Green = vec3(0.25, 1.0, 0.25) * Highlight;
		vec3 Blue = vec3(0.25, 0.25, 1.0) * Highlight;

		if (BestNormal.x != 0.0)
		{
			if ((BoundsLineY && WithinBoundsZ) || BoundsLineZ && WithinBoundsY)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (abs(BestIntersect.y) < LineHalf || abs(BestIntersect.z) < LineHalf)
			{
				OutColor.rgb = Gray;
			}
			else if (LineDistY < LineHalf && LineDistY < LineDistZ)
			{
				OutColor.rgb = Green;
			}
			else if (LineDistZ < LineHalf && LineDistZ < LineDistY)
			{
				OutColor.rgb = Blue;
			}
			else if (TileY && !TileZ && ((BestIntersect.y > 0 && SdPlus(BestIntersect.yz) <= 0.0) || SdMinus(BestIntersect.yz) <= 0.0))
			{
				OutColor.rgb = Green;
			}
			else if (TileZ && !TileY && ((BestIntersect.z > 0 && SdPlus(BestIntersect.yz) <= 0.0) || SdMinus(BestIntersect.yz) <= 0.0))
			{
				OutColor.rgb = Blue;
			}
		}
		else if (BestNormal.y != 0.0)
		{
			if ((BoundsLineX && WithinBoundsZ) || BoundsLineZ && WithinBoundsX)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (abs(BestIntersect.x) < LineHalf || abs(BestIntersect.z) < LineHalf)
			{
				OutColor.rgb = Gray;
			}
			else if (LineDistX < LineHalf && LineDistX < LineDistZ)
			{
				OutColor.rgb = Red;
			}
			else if (LineDistZ < LineHalf && LineDistZ < LineDistX)
			{
				OutColor.rgb = Blue;
			}
			else if (TileX && !TileZ && ((BestIntersect.x > 0 && SdPlus(BestIntersect.xz) <= 0.0) || SdMinus(BestIntersect.xz) <= 0.0))
			{
				OutColor.rgb = Red;
			}
			else if (TileZ && !TileX && ((BestIntersect.z > 0 && SdPlus(BestIntersect.xz) <= 0.0) || SdMinus(BestIntersect.xz) <= 0.0))
			{
				OutColor.rgb = Blue;
			}
		}
		else
		{
			if ((BoundsLineX && WithinBoundsY) || BoundsLineY && WithinBoundsX)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (abs(BestIntersect.x) < LineHalf || abs(BestIntersect.y) < LineHalf)
			{
				OutColor.rgb = Gray;
			}
			else if (LineDistX < LineHalf && LineDistX < LineDistY)
			{
				OutColor.rgb = Red;
			}
			else if (LineDistY < LineHalf && LineDistY < LineDistX)
			{
				OutColor.rgb = Green;
			}
			else if (TileX && !TileY && ((BestIntersect.x > 0 && SdPlus(BestIntersect.xy) <= 0.0) || SdMinus(BestIntersect.xy) <= 0.0))
			{
				OutColor.rgb = Red;
			}
			else if (TileY && !TileX && ((BestIntersect.y > 0 && SdPlus(BestIntersect.xy) <= 0.0) || SdMinus(BestIntersect.xy) <= 0.0))
			{
				OutColor.rgb = Green;
			}
		}
	}
}
