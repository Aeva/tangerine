prepend: shaders/defines.h
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


layout(binding = 3) uniform sampler2D DepthBuffer;
layout(binding = 4, r32f) uniform writeonly image2D DepthRange;


shared float[64] TileCache;


layout(local_size_x = TILE_SIZE_X, local_size_y = TILE_SIZE_Y, local_size_z = 1) in;
void main()
{
	// Lanes correspond to pixels in the depth buffer, in an 2x2 tile.
	// The thread group corresponds to a pixel in a lower mip level.

	uint Lane = TILE_SIZE_X * gl_LocalInvocationID.y + gl_LocalInvocationID.x;
	TileCache[Lane] = 0.0;
	ivec2 TexelCoord = min(ivec2(gl_GlobalInvocationID.xy), ivec2(ScreenSize.xy) - 1);
	TileCache[Lane] = texelFetch(DepthBuffer, TexelCoord, 0).r;

	barrier();
	groupMemoryBarrier();

	if (Lane == 0)
	{
		float DepthMin = TileCache[0];
		for (int i = 1; i < 64; ++i)
		{
			DepthMin = min(DepthMin, TileCache[i]);
		}
		imageStore(DepthRange, ivec2(gl_WorkGroupID.xy), vec4(DepthMin));
	}
}
