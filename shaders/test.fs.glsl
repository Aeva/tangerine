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


float SphereDist(vec3 Point, float Radius)
{
	return length(Point) - Radius;
}


float SceneDist(vec3 Point)
{
	return SphereDist(Point, 1.8);
}


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
		float Dist = SphereDist(Position, 1.5);
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
		gl_FragColor = vec4(vec3(Diffuse), 1.0);
	}
	else
	{
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
	}
}
