

float4x4 LocalToWorld;
float4x4 WorldToView;
float4x4 ViewToClip;


struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};


VertexOutput VertexMain(
    float4 Position : SV_Position,
    float4 ViewOffset : TEXCOORD0,
    float4 Color : COLOR0)
{
    VertexOutput Out;
    float4 ViewPosition = mul(Position, WorldToView);
    ViewPosition /= ViewPosition.w;
    ViewPosition.xyz += ViewOffset.xyz;
    Out.Position = mul(ViewPosition, ViewToClip);
    Out.Color = Color;
    return Out;
}


float4 PixelMain(VertexOutput In) : SV_Target0
{
    return In.Color;
}


technique BasicDraw
{
    pass
    {
#if OPENGL
        VertexShader = compile vs_3_0 VertexMain();
        PixelShader = compile ps_3_0 PixelMain();
#else
        VertexShader = compile vs_4_0 VertexMain();
        PixelShader = compile ps_4_0 PixelMain();
#endif
    }
}
