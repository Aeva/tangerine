

float4x4 WorldToView;
float4x4 ViewToClip;


struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};


VertexOutput VertexMain(
    float4 SplatPosition : SV_Position,
    float4 WorldOffset : TEXCOORD0,
    float4 Color : COLOR0)
{
    VertexOutput Out;
    float4 ViewPosition = mul(WorldOffset, WorldToView);
    ViewPosition /= ViewPosition.w;
    ViewPosition.xy += SplatPosition.xy;
    ViewPosition.z += SplatPosition.z * 0.1f;
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
