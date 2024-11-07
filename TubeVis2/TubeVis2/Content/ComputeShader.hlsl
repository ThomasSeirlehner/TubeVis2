#define WORKGROUP_SIZE 16

// represents 2 times uint_32
RWStructuredBuffer<uint2> kBuffer : register(u0);

cbuffer MatricesAndUserInput : register(b0)
{
    float4 kBufferInfo;
};

[numthreads(WORKGROUP_SIZE, WORKGROUP_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 coord = int2(DTid.xy);
    int3 imgSize = int3(1, 2, 3); //int3(kBufferInfo.xyz);

    if (coord.x >= imgSize.x || coord.y >= imgSize.y)
        return;

    for (int i = 0; i < imgSize.z; ++i)
    {
        uint index = coord.x + coord.y * imgSize.x + i * (imgSize.x * imgSize.y);
        kBuffer[index] = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
    }
}
