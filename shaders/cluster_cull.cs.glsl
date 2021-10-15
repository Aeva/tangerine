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


layout(std140, binding = 1)
uniform InstanceInfoBlock
{
	uint InstanceOffset;
};


layout(std430, binding = 0) restrict writeonly buffer TileHeap
{
	TileHeapEntry Heap[];
};


layout(std140, binding = 1) buffer TileHeapInfo
{
	uint HeapSize;
	uint SegmentStart;
	uint StackPtr;
};


layout(std430, binding = 2) restrict readonly buffer SubtreeClipRects
{
	ClipRect ClipRects[];
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
		vec4 TileClip = vec4(ScreenMin, ScreenMax) * ScreenSize.zwzw * 2.0 - 1.0;
		ClipRect Rect = ClipRects[gl_GlobalInvocationID.z];
		if (ClipTest(TileClip, Rect))
		{
			uint Ptr = atomicAdd(StackPtr, 1);
			TileHeapEntry Tile;
			Tile.TileID = ((gl_GlobalInvocationID.y & 0xFFFF) << 16) | (gl_GlobalInvocationID.x & 0xFFFF);
			Tile.ClusterID = gl_GlobalInvocationID.z % ClusterCount;
			Tile.InstanceID = (gl_GlobalInvocationID.z / ClusterCount) + InstanceOffset;
			Heap[Ptr] = Tile;
		}
	}
}
