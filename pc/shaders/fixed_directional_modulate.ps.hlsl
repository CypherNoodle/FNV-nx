// Explicit single-texture modulation; not automatic texture-stage emulation.
sampler2D texture0 : register(s0);
float4 main(float4 color : COLOR0,float2 uv : TEXCOORD0) : COLOR0 {
    return tex2D(texture0,uv)*color;
}
