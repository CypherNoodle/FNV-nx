sampler2D texture0 : register(s0);
float4 main(float4 color : COLOR0,float2 uv : TEXCOORD0) : COLOR0 {
    float4 sample=tex2D(texture0,uv);
    return float4(sample.rgb*color.rgb,sample.a);
}
