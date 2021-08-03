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


out vec4 FragColor;


vec3 Gradient(vec3 Position)
{
	float AlmostZero = 0.0001;
	float Dist = SceneDist(Position);
	return vec3(
		SceneDist(vec3(Position.x + AlmostZero, Position.y, Position.z)) - Dist,
		SceneDist(vec3(Position.x, Position.y + AlmostZero, Position.z)) - Dist,
		SceneDist(vec3(Position.x, Position.y, Position.z + AlmostZero)) - Dist);
}


void main()
{
	vec2 NDC = gl_FragCoord.xy * ScreenSize.zw * 2.0 - 1.0;
	vec4 Clip = vec4(NDC, -1.0, 1.0);
	vec4 View = ClipToView * Clip;
	View /= View.w;
	vec4 World = ViewToWorld * View;
	World /= World.w;
	vec3 EyeRay = normalize(World.xyz - CameraOrigin.xyz);

	bool Hit = false;
	float Travel = 0;
	vec3 Position;
	for (int i = 0; i < 100; ++i)
	{
		Position = EyeRay * Travel + CameraOrigin.xyz;
		float Dist = SceneDist(Position);
		if (Dist <= 0.0001)
		{
			Hit = true;
			break;
		}
		else
		{
			Travel += Dist;
		}
	}

	if (Hit)
	{
		vec3 Normal = normalize(Gradient(Position));
		vec3 LightRay = normalize(vec3(-1.0, 1.0, -1.0));
		float Diffuse = max(-dot(Normal, LightRay), 0.2);
		//FragColor = vec4(vec3(Diffuse), 1.0);
		FragColor = vec4((Normal * 0.5 + 0.5) * Diffuse, 1.0);
	}
	else
	{
		FragColor = vec4(0.0, 0.0, 0.0, 1.0);
	}
}
