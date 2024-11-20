cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
    matrix model;
    matrix view;
    matrix projection;
    float4 mkBufferInfo; // This can still be used if needed
};

StructuredBuffer<uint2> computeBuffer : register(t0); // Use t0 for SRV

struct VertexShaderInput
{
    float3 pos : POSITION;
    float3 color : COLOR0;
};

struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float3 color : COLOR0;
    //float3 computeColor : COLOR1;
};

PixelShaderInput main(VertexShaderInput input)
{
    PixelShaderInput output;
    float4 pos = float4(input.pos, 1.0f);

    // Transform the vertex position into projected space.
    pos = mul(pos, model);
    pos = mul(pos, view);
    pos = mul(pos, projection);
    output.pos = pos;

    output.color = input.color;
    
   // uint index = (uint) (input.pos.x + 1) * (uint) mkBufferInfo.x + (uint) (input.pos.y + 1);
   // uint2 computeData = computeBuffer[index % (uint) (mkBufferInfo.x * mkBufferInfo.y)];
    
    // Create a color from compute data
    //output.computeColor = float3(
    //    computeBuffer[index % (uint) (mkBufferInfo.x * mkBufferInfo.y)].x,
    //    computeBuffer[index % (uint) (mkBufferInfo.x * mkBufferInfo.y)].y,
    //    1.0f
    //);
    
    return output;
}