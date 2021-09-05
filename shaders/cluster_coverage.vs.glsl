prepend: shaders/defines.h
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


layout(std430, binding = 0) restrict readonly buffer TileHeap
{
	TileHeapEntry Heap[];
};


layout(std140, binding = 1) restrict readonly buffer TileHeapInfo
{
	uint HeapSize;
	uint StackPtr;
};


out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[];
};


vec2 Verts[4] = \
{
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
};


ivec3 Indices[2] = \
{
	ivec3(0, 1, 2),
	ivec3(0, 2, 3)
};


out flat vec2 TileCoords;
out flat uint Variant;


void main()
{
	if (gl_InstanceID <= StackPtr)
	{
		vec2 MinNDC;
		vec2 MaxNDC;
		{
			TileHeapEntry Tile = Heap[gl_InstanceID];
			TileCoords = vec2(float(Tile.TileID & 0xFFFF), float(Tile.TileID >> 16));
			Variant = Tile.Variant;
			vec2 TileSize = vec2(float(TILE_SIZE_X), float(TILE_SIZE_Y));
			vec2 ScreenMin = TileCoords * TileSize;
			vec2 ScreenMax = min(ScreenMin + TileSize, ScreenSize.xy);
			MinNDC = ScreenMin * ScreenSize.zw * 2.0 - 1.0;
			MaxNDC = ScreenMax * ScreenSize.zw * 2.0 - 1.0;
		}

		const int Tri = gl_VertexID / 3;
		const int Vert = gl_VertexID % 3;
		int Index = Indices[Tri][Vert];
		vec2 NDC = mix(MinNDC, MaxNDC, Verts[Index]);
		gl_Position = vec4(NDC, 1.0, 1.0);
	}
	else
	{
		gl_Position = vec4(0.0);
	}
}