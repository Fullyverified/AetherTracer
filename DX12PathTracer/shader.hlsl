struct Payload // 32 bytes
{
    float3 color; // 12 bytes
    float3 throughput; // 12 bytes
    bool allowReflection; // 4 bytes
    bool missed; // bytes
};
struct Vertex
{
    float3 position;
    float3 normal;
    float2 texcoord;
    uint materialIndex;
};
// UAV and SRVs
RaytracingAccelerationStructure scene : register(t0, space0);
RWTexture2D<float4> uav : register(u0, space0);
StructuredBuffer<Vertex> VertexBuffers[] : register(t1, space1);
Buffer<uint> IndexBuffers[] : register(t2, space2);
// constants
static const float3 camera = float3(0, 0, -7);
static const float3 light = float3(0, 200, 0);
static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);
[shader("raygeneration")]
void RayGeneration()
{
    
    uint2 idx = DispatchRaysIndex().xy;
    float2 size = DispatchRaysDimensions().xy;
    
    float2 uv = idx / size;
    float3 target = float3((uv.x * 2 - 1) * 1.8 * (size.x / size.y), (1 - uv.y) * 4 - 2 + camera.y, 0);
    
    RayDesc ray;
    ray.Origin = camera;
    ray.Direction = target - camera;
    ray.TMin = 0.001;
    ray.TMax = 1000;
    
    Payload payload;
    payload.allowReflection = true;
    payload.missed = false;
    payload.throughput = float3(1, 1, 1);
    payload.color = float3(1, 1, 1);
    
    TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    uav[idx] = float4(payload.color, 1);
}
[shader("miss")]
void Miss(inout Payload payload)
{
    float slope = normalize(WorldRayDirection()).y;
    float t = saturate(slope * 5 + 0.5);
    payload.color = lerp(skyBottom, skyTop, t);
    payload.missed = true;
}
void Hit(inout Payload payload, float2 uv)
{
    uint instanceID = InstanceID();
    uint prim = PrimitiveIndex();
    uint geomIdx = GeometryIndex();
    
    // Fetch triangle indices
    uint i0 = IndexBuffers[instanceID][prim * 3 + 0];
    uint i1 = IndexBuffers[instanceID][prim * 3 + 1];
    uint i2 = IndexBuffers[instanceID][prim * 3 + 2];
    
    // Fetch Verticies
    Vertex v0 = VertexBuffers[instanceID][i0];
    Vertex v1 = VertexBuffers[instanceID][i1];
    Vertex v2 = VertexBuffers[instanceID][i2];
    
    // Interpolate normal from three vertices
    float uv0 = 1.0f - uv.x - uv.y;
    float3 normal = normalize(v0.normal * uv0 + v1.normal * uv.x + v2.normal * uv.y);
    float3 worldNormal = normalize(mul(normal, (float3x3) ObjectToWorld4x3()));
    
    float3 rayPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    float3 wi = WorldRayDirection() * -1;
    wi = normalize(wi);
    
    float3 throughput = dot(worldNormal, wi);
    
    payload.color *= throughput;
    
}
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    float2 uv = attribs.barycentrics;
    Hit(payload, uv);
    return;
        
    
}