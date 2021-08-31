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


void ClipBounds(out vec3 ClipMin, out vec3 ClipMax)
{
	AABB Bounds = SceneBounds();
	vec3 A = Bounds.Center - Bounds.Extent;
	vec3 B = Bounds.Center + Bounds.Extent;

	vec4 Tmp;
#define TRANSFORM(World) \
	Tmp = (ViewToClip * WorldToView * vec4(World, 1.0));\
	Tmp /= Tmp.w;

	TRANSFORM(A);
	ClipMin = Tmp.xyz;
	ClipMax = ClipMin;

#define CRANK(var) \
	TRANSFORM(var); \
	ClipMin = min(ClipMin, Tmp.xyz); \
	ClipMax = max(ClipMax, Tmp.xyz);

	CRANK(B);
	CRANK(vec3(B.x, A.yz));
	CRANK(vec3(A.x, B.y, A.z));
	CRANK(vec3(A.xy, B.z));
	CRANK(vec3(A.x, B.yz));
	CRANK(vec3(B.x, A.y, B.z));
	CRANK(vec3(B.xy, A.z));

#undef CRANK
#undef TO_CLIP
}


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

		vec3 TestClipMin;
		vec3 TestClipMax;
		ClipBounds(TestClipMin, TestClipMax);

		if (TestClipMin.z >= 0.0 && \
			all(lessThanEqual(TileClipMin, TestClipMax.xy)) && \
			all(lessThanEqual(TestClipMin.xy, TileClipMax)))
		{
			uint Ptr = atomicAdd(StackPtr, 1);
			TileHeapEntry Tile;
			Tile.TileID = ((gl_GlobalInvocationID.y & 0xFFFF) << 16) | (gl_GlobalInvocationID.x & 0xFFFF);
			Heap[Ptr] = Tile;
		}
	}
}
