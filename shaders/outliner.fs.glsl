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
uniform OutlinerOptionsBlock
{
	uint OutlinerFlags;
};

layout(std430, binding = 2) restrict readonly buffer DepthTimeBuffer
{
	float SubtreeHeatmap[];
};

layout(binding = 1) uniform sampler2D DepthBuffer;
layout(binding = 2) uniform sampler2D PositionBuffer;
layout(binding = 3) uniform sampler2D NormalBuffer;
layout(binding = 4) uniform usampler2D SubtreeBuffer;

layout(location = 0) out vec4 OutColor;


void SampleAt(vec2 Coord, out bool DepthMask, out vec3 Position, out vec3 Normal, out uint SubtreeIndex)
{
	const vec2 UV = clamp(Coord * ScreenSize.zw, 0.0, 1.0);
	DepthMask = texture(DepthBuffer, UV).r == 0.0 ? false : true;
	if (DepthMask)
	{
		Position = texture(PositionBuffer, UV).rgb;
		Normal = texture(NormalBuffer, UV).rgb;
		SubtreeIndex = texture(SubtreeBuffer, UV).r;
	}
}


void SampleAt(vec2 Coord, out bool DepthMask, out vec3 Position, out vec3 Normal)
{
	uint Ignore;
	SampleAt(Coord, DepthMask, Position, Normal, Ignore);
}


vec3 HSV(float Hue, float Sat, float Value)
{
	vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
	return Value * mix(K.xxx, min(max(abs(fract(Hue + K.xyz) * 6.0 - K.w) - K.x, 0.0), 1.0), Sat);
}


void main()
{
	bool DepthMask;
	vec3 CenterPosition;
	vec3 CenterNormal;
	uint SubtreeIndex;
	SampleAt(gl_FragCoord.xy, DepthMask, CenterPosition, CenterNormal, SubtreeIndex);
	if (DepthMask)
	{
		vec3 Positions[8];
		vec3 Normals[8];
		vec2 Offsets[8] = \
		{
			vec2(-1.0, -1.0), // 0 : B
			vec2( 0.0, -1.0), // 1 : C
			vec2( 1.0, -1.0), // 2 : D

			vec2(-1.0,  0.0), // 3 : A
			vec2( 1.0,  0.0), // 4 : A

			vec2(-1.0,  1.0), // 5 : D
			vec2( 0.0,  1.0), // 6 : C
			vec2( 1.0,  1.0)  // 7 : B
		};
		bool IsOutline = false;
		for (int i = 0; i < 8; ++i)
		{
			vec2 Coord = gl_FragCoord.xy + Offsets[i];
			vec3 AdjacentPosition;
			SampleAt(Coord, DepthMask, Positions[i], Normals[i]);
			if (!DepthMask)
			{
				IsOutline = true;
				break;
			}
		}
		if (IsOutline)
		{
			OutColor = vec4(vec3(0.0), 1.0);
		}
		else
		{
			float Angle = -1.0;
			vec3 AverageNeighbor = CenterPosition;
			for (int i = 0; i < 4; ++i)
			{
				int j = 7 - i;
				vec3 Ray1 = normalize(Positions[i] - CenterPosition);
				vec3 Ray2 = normalize(Positions[j] - CenterPosition);
				float AngleAcross = dot(Ray1, Ray2);
				if (AngleAcross > Angle)
				{
					Angle = AngleAcross;
					AverageNeighbor = (Positions[i] + Positions[j]) * 0.5;
				}
			}
			{
				vec3 BaseColor = vec3(1.0);
				if ((OutlinerFlags & 1) == 1)
				{
					float PHI = 1.618033988749895;
					float Hue = fract((float(SubtreeIndex) * PHI * 130.0) / 360.0);
					BaseColor = HSV(Hue, 1.0, 1.0);
				}
				else if ((OutlinerFlags & 2) == 2)
				{
					float Alpha = SubtreeHeatmap[SubtreeIndex];
					vec3 Cold = vec3(1.0);
					vec3 Hot = vec3(1.0, 0.0, 0.0);
					BaseColor = mix(Cold, Hot, Alpha * Alpha);
				}
				float Highlight = dot(CenterNormal, normalize(CameraOrigin.xyz - CenterPosition));
				OutColor = vec4(BaseColor * vec3(Highlight), 1.0);
			}
			if (Angle > -0.707)
			{
				float Delta = distance(AverageNeighbor, CameraOrigin.xyz) - distance(CameraOrigin.xyz, CenterPosition);
				if (Delta < 0.0)
				{
					OutColor.xyz -= 0.75;
				}
				else
				{
					OutColor.xyz += 0.75;
				}
			}
		}
	}
	else
	{
		discard;
	}
}
