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
	bool Perspective;
};


layout(location = 0) out vec4 OutColor;


float rand(vec2 Coord, uint Scale)
{
	uvec2 q = uvec2(Coord) * Scale * uvec2(1597334677U, 3812015801U);
	uint Seed = (q.x ^ q.y) * 1597334677U;
	uint Mantissa = 0x007FFFFFu;
	uint Exponent = 0x3F800000u;
	return uintBitsToFloat((Seed & Mantissa) | Exponent) - 1.0;
}


void main()
{
	float Noise = 0.0;
	vec2 Coord = gl_FragCoord.xy * 0.25;
	uint Time = uint(CurrentTime * 0.05);
	Noise += rand(Coord, Time);
	Noise += rand(Coord, Time + 2);
	Noise += rand(Coord, Time + 3);
	Noise /= 3.0;
	OutColor = vec4(vec3(Noise), 1.0);
}
