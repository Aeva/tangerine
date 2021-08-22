--------------------------------------------------------------------------------

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

layout(binding = 1) uniform sampler2D DepthBuffer;
layout(binding = 2) uniform sampler2D PositionBuffer;
layout(binding = 3) uniform sampler2D NormalBuffer;

layout(location = 0) out vec4 OutColor;


void SampleAt(vec2 Coord, out bool DepthMask, out vec3 Position, out vec3 Normal)
{
	const vec2 UV = clamp(Coord * ScreenSize.zw, 0.0, 1.0);
	DepthMask = texture(DepthBuffer, UV).r == 0.0 ? false : true;
	if (DepthMask)
	{
		Position = texture(PositionBuffer, UV).rgb;
		Normal = texture(NormalBuffer, UV).rgb;
	}
}


void main()
{
	bool DepthMask;
	vec3 CenterPosition;
	vec3 CenterNormal;
	SampleAt(gl_FragCoord.xy, DepthMask, CenterPosition, CenterNormal);
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
			OutColor = vec4(vec3(0.5), 1.0);
			if (Angle > -0.707)
			{
				float Delta = distance(AverageNeighbor, CameraOrigin.xyz) - distance(CameraOrigin.xyz, CenterPosition);
				if (Delta < 0.0)
				{
					OutColor.xyz = vec3(0.0);
				}
				else
				{
					OutColor.xyz = vec3(1.0);
				}
			}
		}
	}
	else
	{
		OutColor = vec4(vec3(0.25), 1.0);
	}
}
