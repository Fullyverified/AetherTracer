
Texture2D<float4> accumulationTexture : register(t0);
RWBuffer<float> groupMaxes : register(u0);

cbuffer Params : register(b0)
{
    float maxLum; // unused
    float exposure; // unused
    uint frameCount;
}

groupshared float sharedLums[256];

[numthreads(1, 1, 1)]
void main()
{
    
    
}

void maxReduction(uint3 dispatchID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupThreadID : SV_GroupIndex)
{
   
    
}