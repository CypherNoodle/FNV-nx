// Bounded material lighting: at most one directional light, no specular; unchanged TEXCOORD0.
float4 column0 : register(c0);
float4 column1 : register(c1);
float4 column2 : register(c2);
float4 column3 : register(c3);
float4 normal0 : register(c4);
float4 normal1 : register(c5);
float4 normal2 : register(c6);
float4 direction : register(c7);
float4 diffuse : register(c8);
float4 ambient : register(c9);
struct Vertex {float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0;};
struct Output {float4 position : POSITION; float4 color : COLOR0; float2 uv : TEXCOORD0;};
Output main(Vertex input){
    Output output;float4 p=float4(input.position,1.0);
    output.position=float4(dot(p,column0),dot(p,column1),dot(p,column2),dot(p,column3));
    float3 n=float3(dot(input.normal,normal0.xyz),dot(input.normal,normal1.xyz),dot(input.normal,normal2.xyz));
    if(direction.w!=0.0)n=normalize(n);
    output.color=saturate(float4(ambient.rgb+diffuse.rgb*max(dot(n,-direction.xyz),0.0),diffuse.a));
    output.uv=input.uv;
    return output;
}
