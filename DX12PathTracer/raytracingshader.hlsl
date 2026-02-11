struct [raypayload] Payload // 56 bytes
{
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 color : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 newPos : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 newDir : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    uint numBounces : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    bool missed : read(caller, closesthit, miss) : write(caller, closesthit, miss);
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 texcoord;
    uint materialIndex;
};
struct Material
{
    float3 color;
    float roughness;
    float metallic;
    float ior;
    float transmission;
    float emission;
};

// UAV, SRVs and CBVs
RWTexture2D<float4> accumulationTexture : register(u0, space0);

RaytracingAccelerationStructure scene : register(t0, space0);

StructuredBuffer<Vertex> VertexBuffers[] : register(t1, space1);
Buffer<uint> IndexBuffers[] : register(t2, space2);

StructuredBuffer<Material> Materials : register(t3, space3);
Buffer<uint> materialIndexBuffer : register(t3, space4);

cbuffer Camerab : register(b0)
{
    float3 camPos;
    float pad0;
    row_major float4x4 InvVieProj;
    float pad1;
}


// constants
static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);

[shader("raygeneration")]
void RayGeneration()
{
    
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims = DispatchRaysDimensions().xy;
    
    
    float2 uv = (launchIndex + 0.5f) / float2(launchDims);
    
    // NDC [-1 , 1]
    
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // flip y
    
    float4 target = mul(float4(ndc.x, ndc.y, 0.0f, 1.0f), InvVieProj);
    float3 worldPos = target.xyz / target.w;
   
    RayDesc ray;
    ray.Origin = camPos;
    ray.Direction = normalize(worldPos - camPos);
    ray.TMin = 0.001;
    ray.TMax = 1e20f;
    
    Payload payload;
    payload.throughput = float3(1.0f, 1.0f, 1.0f);
    payload.color = float3(0.0f, 0.0f, 0.0f);
    payload.missed = false;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    
    for (uint numBounces = 0; numBounces < 1; numBounces++)
    {
        TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        ray.Origin = payload.newPos;
        ray.Direction = payload.newDir;
        
        finalColor += payload.color * payload.throughput;
            
        if (payload.missed)
        {
            break;
        }

    }
    
    accumulationTexture[launchIndex] += float4(finalColor, 1.0f);

}

[shader("miss")]
void Miss(inout Payload payload)
{
    float slope = normalize(WorldRayDirection()).y;
    float t = saturate(slope * 5 + 0.5);
    payload.color += payload.throughput * lerp(skyBottom, skyTop, t);
    
    payload.missed = true;
}

float3 DirectionSampler(inout Payload payload, Material mat, float3 worldNormal, float2 uv)
{
    
    float3 newDir = reflect(payload.newDir, worldNormal);
    
    
    return newDir;
}

void Hit(inout Payload payload, float2 uv)
{
    uint instanceIndex = InstanceIndex(); // auto generated
    uint instanceID = InstanceID(); // for vertice/index buffers
    uint prim = PrimitiveIndex();

    // Fetch triangle indices
    uint i0 = IndexBuffers[instanceID][prim * 3 + 0];
    uint i1 = IndexBuffers[instanceID][prim * 3 + 1];
    uint i2 = IndexBuffers[instanceID][prim * 3 + 2];

    // Fetch Vertices
    Vertex v0 = VertexBuffers[instanceID][i0];
    Vertex v1 = VertexBuffers[instanceID][i1];
    Vertex v2 = VertexBuffers[instanceID][i2];

    // Interpolate normal from three vertices
    float uv0 = 1.0f - uv.x - uv.y;
    float3 normal = normalize(v0.normal * uv0 + v1.normal * uv.x + v2.normal * uv.y);
    float3 worldNormal = normalize(mul(normal, (float3x3) ObjectToWorld4x3()));

    // Fetch material
    uint matID = materialIndexBuffer[instanceIndex];
    Material mat = Materials[matID];
    
    // Update ray
    float3 rayPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.newPos = rayPos;
    
    payload.newDir = DirectionSampler(payload, mat, worldNormal, uv);
    
    
    // shading
    float3 wi = WorldRayDirection() * -1;
    wi = normalize(wi);

    float3 throughput = dot(worldNormal, wi);
    payload.throughput = throughput;
    payload.color =  mat.color;

}


        
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    float2 uv = attribs.barycentrics;
    payload.numBounces++;
    Hit(payload, uv);
    return;
    
}