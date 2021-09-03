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
// See the License for the specific language governing permissionsand
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
	float CurrentTime;
};


in flat AABB Bounds;
in flat vec3 WorldMin;
in flat vec3 WorldMax;


layout(depth_less) out float gl_FragDepth;
layout(location = 0) out vec3 OutPosition;
#if VISUALIZE_TRACING_ERROR
layout(location = 1) out vec4 OutNormal;
#else
layout(location = 1) out vec3 OutNormal;
#endif


vec3 Gradient(vec3 Position)
{
	float AlmostZero = 0.0001;
	float Dist = SceneDist(Position);
	return vec3(
		SceneDist(vec3(Position.x + AlmostZero, Position.y, Position.z)) - Dist,
		SceneDist(vec3(Position.x, Position.y + AlmostZero, Position.z)) - Dist,
		SceneDist(vec3(Position.x, Position.y, Position.z + AlmostZero)) - Dist);
}


struct Coverage
{
	float Low;
	float High;
	float Sign;
};


void main()
{
	vec2 NDC = gl_FragCoord.xy * ScreenSize.zw * 2.0 - 1.0;
	vec4 Clip = vec4(NDC, -1.0, 1.0);
	vec4 View = ClipToView * Clip;
	View /= View.w;
	vec4 World = ViewToWorld * View;
	World /= World.w;
	vec3 EyeRay = normalize(World.xyz - CameraOrigin.xyz);
	vec3 RayStart = EyeRay * BoxBrush(CameraOrigin.xyz - Bounds.Center, Bounds.Extent) + CameraOrigin.xyz;
	vec3 SearchMin = min(RayStart, WorldMin);
	vec3 SearchMax = max(RayStart, WorldMax);

	bool Hit = false;
	float Travel = 0.0;
	vec3 Position;
	float Dist = 0.0;
#if USE_COVERAGE_SEARCH
	{
		const float AlmostZero = 0.001;
		float MaxTravel = distance(WorldMin, WorldMax); // Overestimate, bad for perf;
		float PivotTravel = mix(Travel, MaxTravel, 0.5);
		float PivotRadius = SceneDist(EyeRay * PivotTravel + RayStart);

		const int MaxStack = 14;
		Coverage Stack[MaxStack];
		Stack[0] = Coverage(MaxTravel, MaxTravel, 0.0);
		Stack[1] = Coverage(PivotTravel - abs(PivotRadius), PivotTravel + abs(PivotRadius), sign(PivotRadius));
		int Top = 1;
		Coverage Cursor = Coverage(Travel, Travel, 0.0);

		for (int i = 0; i < 200; ++i)
		{
			if (Stack[Top].Low - AlmostZero <= Cursor.High)
			{
				Cursor = Stack[Top];
				--Top;
				if (Top == -1 || Cursor.Sign < 0)
				{
					break;
				}
			}
			else
			{
				PivotTravel = (Stack[Top].Low + Cursor.High) * 0.5;
				PivotRadius = SceneDist(EyeRay * PivotTravel + RayStart);
				Coverage Next = Coverage(PivotTravel - abs(PivotRadius), PivotTravel + abs(PivotRadius), sign(PivotRadius));
				if (abs(Stack[Top].Sign + Next.Sign) > 0 && Stack[Top].Low - AlmostZero <= Next.High)
				{
					Stack[Top].Low = Next.Low;
				}
				else if (Top < MaxStack - 1)
				{
					++Top;
					Stack[Top] = Next;
				}
				else
				{
					// Ran out of stack!
					break;
				}
			}
		}
		if (Cursor.Sign == -1)
		{
			Travel = max(0.0, Cursor.Low);
			Position = EyeRay * Travel + RayStart;
			Dist = SceneDist(Position);
			Hit = true;
		}
	}
#else
	for (int i = 0; i < 100; ++i)
	{
		Position = EyeRay * Travel + RayStart;
		if (any(lessThan(Position, SearchMin)) || any(greaterThan(Position, SearchMax)))
		{
			Hit = false;
			break;
		}
		else
		{
			Dist = SceneDist(Position);
			if (Dist <= 0.001)
			{
				Hit = true;
				break;
			}
			else
			{
				Travel += Dist;
			}
		}
	}
#endif

	if (Hit)
	{
		OutPosition = Position;
		OutNormal.xyz = normalize(Gradient(Position));
#if VISUALIZE_TRACING_ERROR
		OutNormal.a = abs(clamp(Dist, -0.005, 0.005) / 0.005);
#endif

		vec4 ViewPosition = WorldToView * vec4(Position, 1.0);
		ViewPosition /= ViewPosition.w;
		vec4 ClipPosition = ViewToClip * ViewPosition;
		gl_FragDepth = 1.0 - (ClipPosition.z / ClipPosition.w);
	}
	else
	{
		gl_FragDepth = 0.0;
	}
}
