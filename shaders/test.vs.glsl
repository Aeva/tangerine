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


out gl_PerVertex
{
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[];
};


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


out vec3 WorldSpace;

void main()
{
	AABB Bounds = SceneBounds();
	const int Tri = gl_VertexID / 3;
	const int Vert = gl_VertexID % 3;
	int Index = Indices[Tri][Vert];
	WorldSpace = Verts[Index] * Bounds.Extent + Bounds.Center;
	gl_Position = ViewToClip * WorldToView * vec4(WorldSpace, 1.0);
}
