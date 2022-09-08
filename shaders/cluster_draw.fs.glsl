prepend: shaders/interpreter.glsl
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
	mat4 WorldToLastView;
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


layout(std140, binding = 1)
uniform InstanceDataBlock
{
	mat4 LocalToWorld;
	mat4 WorldToLocal;
};


layout(std140, binding = 3)
uniform DebugOptionsBlock
{
	uint OctreeID;
	uint Wireframe;
};


in vec3 LocalPosition;
in vec3 Barycenter;
in flat uint DrawID;
in flat AABB Bounds;
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
layout(location = 3) out vec3 OutMaterial;


vec3 Gradient(vec3 Position)
{
	float AlmostZero = 0.0001;
	float Dist = ClusterDist(Position).Dist;
	return vec3(
		ClusterDist(vec3(Position.x + AlmostZero, Position.y, Position.z)).Dist - Dist,
		ClusterDist(vec3(Position.x, Position.y + AlmostZero, Position.z)).Dist - Dist,
		ClusterDist(vec3(Position.x, Position.y, Position.z + AlmostZero)).Dist - Dist);
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
	MaterialDist Dist = MaterialDist(vec3(1.0), 0.0);

	if (Wireframe != 0 && any(lessThan(Barycenter, vec3(0.1))))
	{
		Dist.Color = vec3(1.0) - Barycenter;
		Position = RayStart;
		Hit = true;
		CanHit = false;
	}

	if (CanHit)
	{
		for (int i = 0; i < MAX_ITERATIONS; ++i)
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
#if VISUALIZE_INTERIOR_ISOLINES
				Dist.Dist = DiffOp(Dist.Dist, Plane(Position, 0.0, 0.0, -1.0));
#endif
				if (Dist.Dist <= 0.001)
				{
					Hit = true;
					break;
				}
				else
				{
						Travel += Dist.Dist;
				}
			}
		}

#if VISUALIZE_INTERIOR_ISOLINES
		if (Position.z >= -0.001)
		{
			float Scale = 8.0;
			float Alpha = fract(abs(ClusterDist(Position).Dist) * Scale);
			Dist.Color = mix(vec3(0.0), vec3(1.0), Alpha);
		}
#endif
	}

	if (Hit)
	{
		vec4 WorldPosition = LocalToWorld * vec4(Position, 1.0);
		WorldPosition /= WorldPosition.w;

		OutPosition = WorldPosition.xyz;
		OutNormal.xyz = normalize(mat3(LocalToWorld) * Gradient(Position));
#if VISUALIZE_INTERIOR_ISOLINES
		if (Position.z >= -0.001)
		{
			OutNormal.xyz = vec3(0.0, 0.0, 1.0);
		}
#endif
#if VISUALIZE_TRACING_ERROR
		OutNormal.a = abs(clamp(Dist.Dist, -0.005, 0.005) / 0.005);
#endif
		OutSubtreeID = SubtreeIndex;
		OutMaterial = Dist.Color;

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
