struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float3 color : COLOR0;
};

StructuredBuffer<uint2> inputBuffer : register(t0);

float4 main(PixelShaderInput input) : SV_TARGET
{
    float2 pixelPosition = input.pos.xy;
    
    float2 buffer = inputBuffer[0];
    
    return float4(buffer.x, buffer.y, 0.0f, 1.0f);

}
