prepend: shaders/defines.h
prepend: shaders/math.glsl
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


#if ENABLE_OCCLUSION_CULLING
layout(binding = 1) uniform sampler2D DepthPyramid;
#endif


layout(std140, binding = 1)
uniform InstanceDataBlock
{
	mat4 LocalToWorld;
	mat4 WorldToLocal;
};


layout(std140, binding = 2)
uniform VoxelDataBlock
{
	AABB Bounds;
};


out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[];
};


out vec3 LocalPosition;
out vec3 Barycenter;
out flat vec3 LocalMin;
out flat vec3 LocalMax;
out flat vec3 LocalCamera;

#if DEBUG_OCCLUSION_CULLING
out vec4 OcclusionDebug;
#endif

vec3 Verts[24] = \
{
	vec3(-1.0, -1.0, 1.0),
	vec3(1.0, -1.0, 1.0),
	vec3(1.0, 1.0, 1.0),
	vec3(-1.0, 1.0, 1.0),
	vec3(-1.0, -1.0, -1.0),
	vec3(-1.0, 1.0, -1.0),
	vec3(1.0, 1.0, -1.0),
	vec3(1.0, -1.0, -1.0),
	vec3(-1.0, 1.0, -1.0),
	vec3(-1.0, 1.0, 1.0),
	vec3(1.0, 1.0, 1.0),
	vec3(1.0, 1.0, -1.0),
	vec3(-1.0, -1.0, -1.0),
	vec3(1.0, -1.0, -1.0),
	vec3(1.0, -1.0, 1.0),
	vec3(-1.0, -1.0, 1.0),
	vec3(1.0, -1.0, -1.0),
	vec3(1.0, 1.0, -1.0),
	vec3(1.0, 1.0, 1.0),
	vec3(1.0, -1.0, 1.0),
	vec3(-1.0, -1.0, -1.0),
	vec3(-1.0, -1.0, 1.0),
	vec3(-1.0, 1.0, 1.0),
	vec3(-1.0, 1.0, -1.0)
};


ivec3 Indices[12] = \
{
	ivec3(0, 1, 2),
	ivec3(0, 2, 3),
	ivec3(4, 5, 6),
	ivec3(4, 6, 7),
	ivec3(8, 9, 10),
	ivec3(8, 10, 11),
	ivec3(12, 13, 14),
	ivec3(12, 14, 15),
	ivec3(16, 17, 18),
	ivec3(16, 18, 19),
	ivec3(20, 21, 22),
	ivec3(20, 22, 23)
};


void main()
{
	LocalMin = Bounds.Center - Bounds.Extent;
	LocalMax = Bounds.Center + Bounds.Extent;
	{
		vec4 Tmp = WorldToLocal * vec4(CameraOrigin.xyz, 1.0);
		Tmp /= Tmp.w;
		LocalCamera = Tmp.xyz;
	}
	{
		const int Tri = gl_VertexID / 3;
		const int Vert = gl_VertexID % 3;
		int Index = Indices[Tri][Vert];
		LocalPosition = Verts[Index] * Bounds.Extent + Bounds.Center;
		Barycenter = vec3(0.0);
		Barycenter[Vert] = 1.0;
		gl_Position = ViewToClip * WorldToView * LocalToWorld * vec4(LocalPosition, 1.0);
	}
#if ENABLE_OCCLUSION_CULLING
	// Ideally this would go in a mesh shader or a compute shader.

	vec4 NDCBounds = vec2(1.0, -1.0).xxyy / vec4(0.0);
	float MaxDepth = 0.0;
	for (int i = 0; i < 24; ++i)
	{
		vec3 Local = Verts[i] * Bounds.Extent + Bounds.Center;
		vec4 Clip = ViewToClip * WorldToLastView * LocalToWorld * vec4(Local, 1.0);
		vec3 NDC = Clip.xyz / Clip.w;

		float Depth = 1.0 - NDC.z;
		MaxDepth = max(MaxDepth, Depth);

		NDCBounds.xy = min(NDCBounds.xy, NDC.xy);
		NDCBounds.zw = max(NDCBounds.zw, NDC.xy);
	}
	NDCBounds = NDCBounds * 0.5 + 0.5;
	vec2 Span = (NDCBounds.zw - NDCBounds.xy) * ScreenSize.xy;
	float MaxSpan = max(max(Span.x, Span.y), 1.0);

	float Mip = ceil(log2(MaxSpan));

	bool AnyPass = false;
	vec2 CullDepthRange = vec2(1.0 / 0.0, 0.0);
	for (float y = 0.0; y <= 1.0; ++y)
	{
		if (AnyPass)
		{
			break;
		}
		for (float x = 0.0; x <= 1.0; ++x)
		{
			vec2 UV = mix(NDCBounds.xy, NDCBounds.zw, vec2(x, y));
			float CullDepth = textureLod(DepthPyramid, UV, Mip).r;
			CullDepthRange.x = min(CullDepthRange.x, CullDepth);
			CullDepthRange.y = max(CullDepthRange.y, CullDepth);
			if (CullDepth <= MaxDepth)
			{
				AnyPass = true;
				break;
			}
		}
	}

#if DEBUG_OCCLUSION_CULLING
	OcclusionDebug = vec4(MaxDepth, CullDepthRange.x, Mip, AnyPass ? 0.0 : 1.0);
#else
	if (!AnyPass)
	{
		gl_Position /= 0.0;
	}
#endif
#endif
}
