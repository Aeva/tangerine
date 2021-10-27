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


// NOTE: SSBO binding 0 is reserved for generated parameters.


layout(std140, binding = 2)
uniform InstanceDataBlock
{
	mat4 WorldToLocal;
	mat4 LocalToWorld;
	AABB Bounds;
};


in vec3 LocalPosition;
in flat vec3 LocalMin;
in flat vec3 LocalMax;
in flat vec3 LocalCamera;


layout(depth_less) out float gl_FragDepth;
layout(location = 0) out vec3 OutPosition;
#if VISUALIZE_TRACING_ERROR
layout(location = 1) out vec4 OutNormal;
#else
layout(location = 1) out vec3 OutNormal;
#endif
layout(location = 2) out uint OutSubtreeID;


vec3 Gradient(vec3 Position)
{
	float AlmostZero = 0.0001;
	float Dist = ClusterDist(Position);
	return vec3(
		ClusterDist(vec3(Position.x + AlmostZero, Position.y, Position.z)) - Dist,
		ClusterDist(vec3(Position.x, Position.y + AlmostZero, Position.z)) - Dist,
		ClusterDist(vec3(Position.x, Position.y, Position.z + AlmostZero)) - Dist);
}


struct Coverage
{
	float Low;
	float High;
	float Sign;
};


void main()
{
	vec3 EyeRay = normalize(LocalPosition - LocalCamera);
	vec3 RayStart = EyeRay * BoxBrush(LocalCamera - Bounds.Center, Bounds.Extent) + LocalCamera;

	bool CanHit = RayHitAABB(RayStart, EyeRay, Bounds, RayStart);

	bool Hit = false;
	float Travel = 0.0;
	vec3 Position;
	float Dist = 0.0;

	if (CanHit)
	{
		for (int i = 0; i < 50; ++i)
		{
			Position = EyeRay * Travel + RayStart;
			if (any(lessThan(Position, LocalMin)) || any(greaterThan(Position, LocalMax)))
			{
				Hit = false;
				break;
			}
			else
			{
				Dist = ClusterDist(Position);
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
	}

	if (Hit)
	{
		vec4 WorldPosition = LocalToWorld * vec4(Position, 1.0);
		WorldPosition /= WorldPosition.w;

		OutPosition = WorldPosition.xyz;
		OutNormal.xyz = normalize(mat3(LocalToWorld) * Gradient(Position));
#if VISUALIZE_TRACING_ERROR
		OutNormal.a = abs(clamp(Dist, -0.005, 0.005) / 0.005);
#endif
		OutSubtreeID = SubtreeIndex;

		vec4 ViewPosition = WorldToView * WorldPosition;
		ViewPosition /= ViewPosition.w;
		vec4 ClipPosition = ViewToClip * ViewPosition;
		gl_FragDepth = 1.0 - (ClipPosition.z / ClipPosition.w);
	}
	else
	{
		gl_FragDepth = 0.0;
	}
}
