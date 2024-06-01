

sampler AccumulatorSampler : register(s0);


struct VertexOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};


VertexOutput VertexMain(
    float4 Position : SV_Position)
{
    VertexOutput Out;
    Out.Position = Position;
    Out.UV = Out.Position.xy;
    return Out;
}


float4 PixelMain(VertexOutput In) : SV_Target0
{
    float4 Acc = tex2D(AccumulatorSampler, In.UV.xy * float2(0.5f, -0.5f) + 0.5f);
    return float4(Acc.rgb / Acc.a, 1.0f);
    //return float4(, 1.0f, 1.0f);
}


technique AlphaDivide
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
