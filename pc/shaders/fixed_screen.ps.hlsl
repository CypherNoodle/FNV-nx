// Stage-zero texture * default white diffuse, alpha from texture.
sampler2D texture0 : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {return tex2D(texture0,uv);}
