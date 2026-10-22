// Bounded fixed vertex prototype: XYZ + TEXCOORD0, no lights or blending.
float4 column0 : register(c0);
float4 column1 : register(c1);
float4 column2 : register(c2);
float4 column3 : register(c3);
struct Vertex {float3 position : POSITION; float2 uv : TEXCOORD0;};
struct Output {float4 position : POSITION; float2 uv : TEXCOORD0;};
Output main(Vertex input) {
    Output output;
    float4 p = float4(input.position, 1.0);
    output.position = float4(dot(p,column0),dot(p,column1),dot(p,column2),dot(p,column3));
    output.uv = input.uv;
    return output;
}
