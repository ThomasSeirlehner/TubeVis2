// Pixel Shader

cbuffer PixelShaderConstants : register(b0)
{
    float4 mkBufferInfo; // x: width, y: height, z: depth, w: unused
}

struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float3 color : COLOR0;
};

StructuredBuffer<uint2> computeBuffer : register(t0);

float4 main(PixelShaderInput input) : SV_TARGET
{
    uint2 pixelPosition = uint2(input.pos.xy);
    uint index = uint(input.pos.y) * uint(mkBufferInfo.x) + uint(input.pos.x);
    
    float3 computeColor = float3(
        float(computeBuffer[index].x),
        float(computeBuffer[index].y),
        0.0
    );
    
    // Blend with vertex color or use compute color directly
    float3 finalColor = computeColor;
    
    return float4(finalColor, 1.0);
}