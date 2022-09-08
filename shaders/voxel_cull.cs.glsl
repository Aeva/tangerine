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


layout(binding = 1) uniform sampler2D DepthPyramid;


layout(std140, binding = 1)
uniform InstanceDataBlock
{
	mat4 LocalToWorld;
	mat4 WorldToLocal;
};


layout(std430, binding = 2)
restrict readonly buffer VoxelDataBlock
{
	AABB BoundsArray[];
};


struct DrawArraysIndirectCommand
{
	uint Count;
	uint InstanceCount;
	uint First;
	uint BaseInstance;
};


layout(std430, binding = 3)
restrict writeonly buffer IndirectDataBlock
{
	DrawArraysIndirectCommand IndirectArgs[];
};


vec3 Verts[8] = \
{
	vec3(-1.0, -1.0, -1.0),
	vec3(-1.0, -1.0, 1.0),
	vec3(-1.0, 1.0, -1.0),
	vec3(-1.0, 1.0, 1.0),
	vec3(1.0, -1.0, -1.0),
	vec3(1.0, -1.0, 1.0),
	vec3(1.0, 1.0, -1.0),
	vec3(1.0, 1.0, 1.0)
};


shared uint Vote;
shared vec4 NDCBounds;
shared float MaxDepth;
shared vec4 Screen[8];


layout(local_size_x = 1, local_size_y = 8, local_size_z = 1) in;
void main()
{
	AABB Bounds = BoundsArray[gl_GlobalInvocationID.x];
	uint Vert = gl_GlobalInvocationID.y;

	vec3 Local = Verts[Vert] * Bounds.Extent + Bounds.Center;
	vec4 Clip = ViewToClip * WorldToLastView * LocalToWorld * vec4(Local, 1.0);
	Screen[Vert].xyz = Clip.xyz / Clip.w;
	Screen[Vert].w = 1.0 - Screen[Vert].z;

	memoryBarrierShared();

	if (Vert == 0)
	{
		Vote = 0;
		NDCBounds = vec2(1.0, -1.0).xxyy / vec4(0.0);
		MaxDepth = 0.0;
		for (int i = 0; i < 8; ++i)
		{
			NDCBounds.xy = min(NDCBounds.xy, Screen[i].xy);
			NDCBounds.zw = max(NDCBounds.zw, Screen[i].xy);
			MaxDepth = max(MaxDepth, Screen[i].w);
		}
		NDCBounds = NDCBounds * 0.5 + 0.5;
	}
	memoryBarrierShared();

	if (Vert < 4)
	{
		vec2 Span = (NDCBounds.zw - NDCBounds.xy) * ScreenSize.xy;
		float MaxSpan = max(max(Span.x, Span.y), 1.0);

		float Mip = ceil(log2(MaxSpan));

		vec2 CullDepthRange = vec2(1.0 / 0.0, 0.0);

		vec2 UV = mix(NDCBounds.xy, NDCBounds.zw, vec2(Vert / 2, Vert % 2));
		if (all(greaterThan(UV, vec2(0.0))) && all(lessThan(UV, vec2(1.0))))
		{
			float CullDepth = textureLod(DepthPyramid, UV, Mip).r;
			CullDepthRange.x = min(CullDepthRange.x, CullDepth);
			CullDepthRange.y = max(CullDepthRange.y, CullDepth);
			if (CullDepth <= MaxDepth)
			{
				atomicOr(Vote, 1);
			}
		}
	}
	memoryBarrierShared();

	if (Vert == 0)
	{
		IndirectArgs[gl_GlobalInvocationID.x].InstanceCount = Vote;
	}
}
