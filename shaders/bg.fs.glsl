--------------------------------------------------------------------------------

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


void main()
{
	vec2 NDC = gl_FragCoord.xy * ScreenSize.zw * 2.0 - 1.0;
	vec4 Clip = vec4(NDC, -1.0, 1.0);
	vec4 View = ClipToView * Clip;
	View /= View.w;
	vec4 World = ViewToWorld * View;
	World /= World.w;
	vec3 EyeRay = normalize(World.xyz - CameraOrigin.xyz);

	float BestDist = 1.0 / 0.0;
	vec3 BestNormal = vec3(0.0, 0.0, 0.0);
	vec3 BestIntersect = vec3(0.0, 0.0, 0.0);

	vec3 FocalPoint = vec3(0.0, 0.0, 0.0);
	vec3 Stare = normalize(CameraOrigin.xyz - FocalPoint);
	float TowardsX = abs(dot(Stare, vec3(1.0, 0.0, 0.0)));
	float TowardsY = abs(dot(Stare, vec3(0.0, 1.0, 0.0)));
	float TowardsZ = abs(dot(Stare, vec3(0.0, 0.0, 1.0)));
	float ClipAngle = 0.25;

	if (CameraOrigin.x < ModelMax.x && TowardsX > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(-1.0, 0.0, 0.0), ModelMax.x, BestDist, BestNormal, BestIntersect);
	}
	if (CameraOrigin.y < ModelMax.y && TowardsY > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(0.0, -1.0, 0.0), ModelMax.y, BestDist, BestNormal, BestIntersect);
	}
	if (CameraOrigin.z < ModelMax.z && TowardsZ > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(0.0, 0.0, -1.0), ModelMax.z, BestDist, BestNormal, BestIntersect);
	}

	if (CameraOrigin.x > ModelMin.x && TowardsX > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(1.0, 0.0, 0.0), ModelMin.x, BestDist, BestNormal, BestIntersect);
	}
	if (CameraOrigin.y > ModelMin.y && TowardsY > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(0.0, 1.0, 0.0), ModelMin.y, BestDist, BestNormal, BestIntersect);
	}
	if (CameraOrigin.z > ModelMin.z && TowardsZ > ClipAngle)
	{
		DrawPlane(CameraOrigin.xyz, EyeRay, vec3(0.0, 0.0, 1.0), ModelMin.z, BestDist, BestNormal, BestIntersect);
	}

	OutColor = vec4(0.5, 0.5, 0.5, 1.0);
	if (BestDist != 1.0 / 0.0)
	{
		float Highlight = dot(BestNormal, normalize(CameraOrigin.xyz - BestIntersect)) * 0.5 + 0.5;
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

		if (BestNormal.x != 0.0)
		{
			if ((BoundsLineY && WithinBoundsZ) || BoundsLineZ && WithinBoundsY)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (LineDistY < LineHalf && LineDistY < LineDistZ)
			{
				OutColor.rgb = vec3(0.25, 1.0, 0.25) * Highlight;
			}
			else if (LineDistZ < LineHalf && LineDistZ < LineDistY)
			{
				OutColor.rgb = vec3(0.25, 0.25, 1.0) * Highlight;
			}
		}
		else if (BestNormal.y != 0.0)
		{
			if ((BoundsLineX && WithinBoundsZ) || BoundsLineZ && WithinBoundsX)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (LineDistX < LineHalf && LineDistX < LineDistZ)
			{
				OutColor.rgb = vec3(1.0, 0.25, 0.25) * Highlight;
			}
			else if (LineDistZ < LineHalf && LineDistZ < LineDistX)
			{
				OutColor.rgb = vec3(0.25, 0.25, 1.0) * Highlight;
			}
		}
		else
		{
			if ((BoundsLineX && WithinBoundsY) || BoundsLineY && WithinBoundsX)
			{
				OutColor.rgb = vec3(0.0);
			}
			else if (LineDistX < LineHalf && LineDistX < LineDistY)
			{
				OutColor.rgb = vec3(1.0, 0.25, 0.25) * Highlight;
			}
			else if (LineDistY < LineHalf && LineDistY < LineDistX)
			{
				OutColor.rgb = vec3(0.25, 1.0, 0.25) * Highlight;
			}
		}
	}
}
