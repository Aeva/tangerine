
float4x4 LocalToWorld;
//float3x3 LocalToWorldRotateOnly;
float4x4 WorldToView;
float4x4 ViewToClip;
//float3 EyePosition;
float SplatRadius;
float SplatDepth;
float AspectRatio;


struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};


VertexOutput VertexMain(
    float4 SplatVertex : SV_Position,
    float4 LocalOffset : TEXCOORD0,
    //float4 LocalNormal : NORMAL0,
    float4 Color : COLOR0)
{
    VertexOutput Out;

    float4 WorldOffset = mul(float4(LocalOffset.xyz, 1.0f), LocalToWorld);
    //float3 WorldNormal = mul(LocalNormal.xyz, LocalToWorldRotateOnly);

    //float3 EyeRay = normalize(EyePosition - WorldOffset.xyz);
    //float TangentScale = max(sqrt(max(dot(EyeRay, WorldNormal.xyz), 0.0f)), 0.99);

    float4 ViewPosition = mul(WorldOffset, WorldToView);
    ViewPosition /= ViewPosition.w;
    ViewPosition.z += SplatVertex.z * SplatDepth;

    //ViewPosition.xyz += SplatVertex.xyz * SplatRadius;
    float4 ClipPosition = mul(ViewPosition, ViewToClip);
    ClipPosition.xyz /= ClipPosition.w;
    ClipPosition.xy += SplatVertex.xy * float2(SplatRadius * AspectRatio, SplatRadius) * 2.0f;
    ClipPosition.xyz *= ClipPosition.w;
    Out.Position = ClipPosition;

    Out.Color = Color;
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
