prepend: shaders/tile_heap.glsl
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


struct TileHeapEntry
{
	uint TileID;
};


layout(std430, binding = 0) restrict writeonly buffer TileHeap
{
	TileHeapEntry Heap[];
};


layout(std140, binding = 1) buffer TileHeapInfo
{
	uint HeapSize;
	uint StackPtr;
};


layout(local_size_x = TILE_SIZE_X, local_size_y = TILE_SIZE_Y, local_size_z = 1) in;
void main()
{
	// Each lane in this dispatch corresponds to a screen space tile.
	// The work group size of this dispatch is not significant.
	vec2 TileSize = vec2(float(TILE_SIZE_X), float(TILE_SIZE_Y));
	vec2 ScreenMin = gl_GlobalInvocationID.xy * TileSize;
	if (all(lessThanEqual(ScreenMin, ScreenSize.xy)))
	{
		vec2 ScreenMax = min(ScreenMin + TileSize, ScreenSize.xy);
		vec2 TileClipMin = ScreenMin * ScreenSize.zw * 2.0 - 1.0;
		vec2 TileClipMax = ScreenMax * ScreenSize.zw * 2.0 - 1.0;

		vec2 TestClipMin;
		vec2 TestClipMax;
		{
			AABB Bounds = SceneBounds();
			vec3 WorldMin = Bounds.Center - Bounds.Extent;
			vec3 WorldMax = Bounds.Center + Bounds.Extent;
			vec4 TestClipA = ViewToClip * WorldToView * vec4(WorldMin, 1.0);
			vec4 TestClipB = ViewToClip * WorldToView * vec4(WorldMax, 1.0);
			if (TestClipA.z < 0.0 && TestClipB.z < 0.0)
			{
				return;
			}
			TestClipMin = min(TestClipA.xy, TestClipB.xy);
			TestClipMax = max(TestClipA.xy, TestClipB.xy);
		}
		if (all(lessThanEqual(TileClipMin, TestClipMax)) &&\
			all(lessThanEqual(TestClipMin, TileClipMax)))
		{
			uint Ptr = atomicAdd(StackPtr, 1);
			TileHeapEntry Tile;
			Tile.TileID = ((gl_GlobalInvocationID.y & 0xFFFF) << 16) | (gl_GlobalInvocationID.x & 0xFFFF);
			Heap[Ptr] = Tile;
		}
	}
}
