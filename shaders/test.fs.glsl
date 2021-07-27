--------------------------------------------------------------------------------


layout(std140, binding = 0)
uniform ViewInfoBlock
{
	vec4 ScreenSize;
	float CurrentTime;
};


void main()
{
	float Alpha = sin(CurrentTime / 500.0) * 0.5 + 0.5;
	vec3 Color1 = vec3(gl_FragCoord.xy * ScreenSize.zw, 0.0);
	vec3 Color2 = vec3(1.0 - gl_FragCoord.xy * ScreenSize.zw, 0.0).xzy;
	gl_FragColor = vec4(mix(Color1, Color2, Alpha), 1.0);
}
