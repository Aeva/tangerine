

float4x4 WorldToView;
float4x4 ViewToClip;
float3 EyePosition;
float SplatScale;
float NearPlane;
float FarPlane;


struct VertexOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR0;
    float4 UV_Weight : TEXCOORD0;
};


float Exp5(float Val)
{
    float Exp2 = Val * Val;
    return Exp2 * Exp2 * Val;
}


float Exp8(float Val)
{
    float Exp2 = Val * Val;
    float Exp4 = Exp2 * Exp2;
    float Exp8 = Exp4 * Exp4;
    return Exp8;
}


float WeightZ(float ViewZ)
{
    float Alpha = saturate(max(NearPlane, ViewZ) / FarPlane) * 10.0;
    float Weight = 1.0f / Exp8(Alpha);
    return Weight;
}


VertexOutput VertexMain(
    float4 SplatPosition : SV_Position,
    float4 WorldOffset : TEXCOORD0,
    float4 WorldNormal : NORMAL0,
    float4 Color : COLOR0)
{
    VertexOutput Out;

    float3 EyeRay = normalize(EyePosition - WorldOffset.xyz);

    float Shrink = sqrt(max(dot(EyeRay, WorldNormal.xyz), 0.0f));

    float4 ViewPosition = mul(WorldOffset, WorldToView);
    ViewPosition /= ViewPosition.w;

    float SplatRadius = SplatScale * 0.5;

    float CenterWeight = WeightZ(abs(ViewPosition.z));
    float EdgeWeight = WeightZ(abs(ViewPosition.z) + FarPlane);

    ViewPosition.xyz += SplatPosition.xyz * SplatRadius * Shrink;

    Out.Position = mul(ViewPosition, ViewToClip);
    Out.Color = float4(Color.xyz, 1.0f);
    Out.UV_Weight = float4(SplatPosition.xy, CenterWeight, EdgeWeight);
    return Out;
}


struct PixelOutput
{
    float4 Color : SV_Target0;
};


PixelOutput PixelMain(VertexOutput In)
{
    PixelOutput Out;
    
    float2 UV = In.UV_Weight.xy;
    float CenterWeight = In.UV_Weight.z;
    float EdgeWeight = In.UV_Weight.w;
    float ParaboloidAlpha = 1.0f - dot(UV, UV);
    float Weight = lerp(EdgeWeight, CenterWeight, ParaboloidAlpha);

    Out.Color = In.Color * Weight;
    return Out;
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
