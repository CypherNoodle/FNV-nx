// Two sequential RGBA MODULATE stages, both explicitly selecting TEXCOORD0.
sampler2D texture0 : register(s0);
sampler2D texture1 : register(s1);
float4 main(float4 color : COLOR0,float2 uv : TEXCOORD0) : COLOR0 {
    float4 current=saturate(tex2D(texture0,uv)*color);
    return saturate(tex2D(texture1,uv)*current);
}
