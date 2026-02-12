cbuffer Params : register(b0)
{
    float maxLum;
    float exposure;
    uint frameCount;  
}

Texture2D<float4> accumulationTexture : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID )
{
    float temp = 1.0f;
    
    uint2 dim;
    accumulationTexture.GetDimensions(dim.x, dim.y);
    if (id.x >= dim.x || id.y >= dim.y) return;
    
    float3 accum = accumulationTexture.Load(int3(id.xy, 0)).rgb;
    
    accum /= (float) frameCount;
    
    float luminance = 0.2126f * accum.r + 0.7152f * accum.g + 0.0722f * accum.b;
    
    if (luminance > 0)
    {
        float mappedLuminance = (luminance * (1.0f + (luminance / (temp * temp)))) / (1.0f + luminance);
        
        accum = accum / luminance * mappedLuminance;
        
        float gamma = 2.2f;
        float invGamma = 1.0f / gamma;
        
        accum = pow(accum, invGamma);
       

    }

    Output[id.xy] = float4(accum, 1.0f);

}