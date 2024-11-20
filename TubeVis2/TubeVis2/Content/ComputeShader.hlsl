#define WORKGROUP_SIZE 1

RWStructuredBuffer<uint2> kBuffer : register(u0);

cbuffer MatricesAndUserInput : register(b0)
{
    float4 kBufferInfo; // x: width, y: height, z: depth, w: unused
};

[numthreads(1, WORKGROUP_SIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    
    uint2 coord = DTid.xy;
    uint3 imgSize = uint3(kBufferInfo.xyz);

    if (coord.x >= imgSize.x || coord.y >= imgSize.y)
        return;

    uint baseIndex = coord.y * imgSize.x + coord.x;

    for (uint i = 0; i < imgSize.z; ++i)
    {
        uint index = baseIndex + i * (imgSize.x * imgSize.y);
        
        // Example computation: set each element to its index and depth
        kBuffer[index] = uint2(index, i);
    }

}