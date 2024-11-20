struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float3 color : COLOR0;
    //float3 computeColor : COLOR1;
};

StructuredBuffer<uint2> computeBuffer : register(t0);

float4 main(PixelShaderInput input) : SV_TARGET
{
    uint2 pixelPosition = uint2(input.pos.xy);
    
    // Calculate index based on pixel position
    //uint index = pixelPosition.y * 1920 + pixelPosition.x; // Adjust width as needed
    
    // Read data from compute buffer
    //uint2 computeData = uint2(input.computeColor.x, input.computeColor.y);
    
    // Normalize and create color from compute data (example logic)
    float3 computeColor = float3(
        float(computeBuffer[1].x),
        float(computeBuffer[1].y),
        0.0 // Or some other value based on your logic
    );
    
    // Blend with vertex color
    float3 finalColor =  computeColor;
    
    return float4(finalColor, 1.0); // Output final color
}