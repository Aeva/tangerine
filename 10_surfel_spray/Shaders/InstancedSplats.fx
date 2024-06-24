
float4x4 LocalToWorld;
float3x3 LocalToWorldRotateOnly;
float4x4 WorldToView;
float4x4 ViewToClip;
float3 EyePosition;
float SplatRadius;


struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};


VertexOutput VertexMain(
    float4 SplatVertex : SV_Position,
    float4 LocalOffset : TEXCOORD0,
    float4 LocalNormal : NORMAL0,
    float4 Color : COLOR0)
{
    VertexOutput Out;

    float4 WorldOffset = mul(float4(LocalOffset.xyz, 1.0f), LocalToWorld);
    float3 WorldNormal = mul(LocalNormal.xyz, LocalToWorldRotateOnly);

    float3 EyeRay = normalize(EyePosition - WorldOffset.xyz);
    float TangentScale = sqrt(max(dot(EyeRay, WorldNormal.xyz), 0.0f));

    float4 ViewPosition = mul(WorldOffset, WorldToView);
    ViewPosition /= ViewPosition.w;

    ViewPosition.xyz += SplatVertex.xyz * TangentScale * SplatRadius;
    Out.Position = mul(ViewPosition, ViewToClip);

#if 1
    Out.Color = Color;
#else
    float Alpha = 1.0f + SplatVertex.z;
    Out.Color = lerp(float4(1.0f, 1.0f, 1.0f, 1.0f), Color, Alpha);
#endif
    return Out;
}


float4 PixelMain(VertexOutput In) : SV_Target0
{
    return In.Color;
}


technique SplatInstancing
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
