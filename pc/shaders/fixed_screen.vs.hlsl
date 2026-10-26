// Narrow D3D9 XYZRHW/TEX1 path; viewport is scale.xy and bias.zw.
float4 viewport : register(c0);
struct Input {float4 position : POSITION0; float2 uv : TEXCOORD0;};
struct Output {float4 position : POSITION0; float2 uv : TEXCOORD0;};
Output main(Input input) {
    Output output;
    float w = 1.0 / input.position.w;
    output.position = float4((input.position.xy * viewport.xy + viewport.zw) * w,
                             input.position.z * w, w);
    output.uv = input.uv;
    return output;
}
