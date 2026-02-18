cbuffer Params : register(b0)
{
    uint stage;
    float exposure;
    uint numIterations;
}

Texture2D<float4> accumulationTexture : register(t0);
RWTexture2D<float4> Output : register(u0);

RWBuffer<uint> maxLumBuffer : register(u1);

groupshared float g_maxLum[256];

void maxLuminance(uint3 dispatchID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    uint2 dim;
    
    accumulationTexture.GetDimensions(dim.x, dim.y);
    if (dispatchID.x >= dim.x || dispatchID.y >= dim.y)
    {
        g_maxLum[groupIndex] = 0.0f;
    }
    
    
    float3 accum = accumulationTexture.Load(int3(dispatchID.xy, 0)).rgb;
    float luminance = 0.2126f * accum.r + 0.7152f * accum.g + 0.0722f * accum.b;
    
    g_maxLum[groupIndex] = luminance;
    GroupMemoryBarrierWithGroupSync();
    
    // parallel reduction in groupshared memory
    for (uint i = 128; i > 0; i >>= 1)
    {
        if (groupIndex < i)
        {
            g_maxLum[groupIndex] = max(g_maxLum[groupIndex], g_maxLum[groupIndex + i]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    // one thread per group does global atomic max
    if (groupIndex == 0)
    {
        uint lumBits = asuint(g_maxLum[0]);
        InterlockedMax(maxLumBuffer[0], lumBits);

    }
    
}

void toneMap(uint3 dispatchID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    float maxLuminance = maxLumBuffer[0];
    
    uint2 dim;
    accumulationTexture.GetDimensions(dim.x, dim.y);
    if (dispatchID.x >= dim.x || dispatchID.y >= dim.y)
        return;
    
    float3 accum = accumulationTexture.Load(int3(dispatchID.xy, 0)).rgb;
    
    accum /= (float) numIterations;
    
    float luminance = 0.2126f * accum.r + 0.7152f * accum.g + 0.0722f * accum.b;
    
    if (luminance > 0)
    {
        float mappedLuminance = (luminance * (1.0f + (luminance / (maxLuminance * maxLuminance)))) / (1.0f + luminance);
        
        accum = accum / luminance * mappedLuminance;
        
        float gamma = 2.2f;
        float invGamma = 1.0f / gamma;
        
        accum = pow(accum, invGamma);
       
    }

    Output[dispatchID.xy] = float4(accum, 1.0f);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    if (stage == 0)
    {
        maxLuminance(dispatchID, groupThreadID, groupIndex);
    }
    else if (stage == 1)
    {
        toneMap(dispatchID, groupThreadID, groupIndex);
    }

}