struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float3 color : COLOR0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    float2 pixelPosition = input.pos.xy;
    return float4(input.pos.xyz/1080, 1.0f);
}
