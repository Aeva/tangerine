--------------------------------------------------------------------------------


layout(std140, binding = 0)
uniform ViewInfoBlock
{
	vec4 ScreenSize;
};


void main()
{
	gl_FragColor = vec4(gl_FragCoord.xy * ScreenSize.zw, 0.0, 1.0);
}
