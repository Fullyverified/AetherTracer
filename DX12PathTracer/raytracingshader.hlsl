struct [raypayload] Payload // 72 bytes
{
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 emission : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 pos : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 dir : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    uint numBounces : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    bool missed : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    uint2 pixelIndex : read(caller, closesthit, miss) : write(caller);
    uint2 dims : read(caller, closesthit, miss) : write(caller);
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
RWBuffer<uint> randPattern : register(u1, space0);

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
    uint frame;
}


// constants
static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);
static const float PI = 3.141592653589793; // why not

[shader("raygeneration")]
void RayGeneration()
{

    uint2 pixelIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
            
    float2 uv = (pixelIndex + 0.5f) / float2(dims);
    
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
    payload.emission = float3(0.0f, 0.0f, 0.0f);
    payload.missed = false;
    payload.pixelIndex = pixelIndex;
    payload.dims = dims;
    payload.numBounces = 0;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    
    for (uint numBounces = 0; numBounces <= 8; numBounces++)
    {
        TraceRay(scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        ray.Origin = payload.pos;
        ray.Direction = payload.dir;
       
        finalColor += payload.throughput * payload.emission;
     
        if (payload.missed || payload.emission.x > 0.0f || payload.emission.y > 0.0f || payload.emission.z > 0.0f)
        {
            break;
        }

    }
    
    accumulationTexture[pixelIndex] += float4(payload.throughput, 1.0f);
}

float random(uint2 pixelIndex, uint2 dims)
{
    uint state = randPattern[pixelIndex.x + pixelIndex.y * dims.x];

    state = state * 747796405u + 2891336453u;
    
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word ^= word >> 22;
    uint rot = state >> 28;
    word = (word >> rot) | (word << (32u - rot));
   
    randPattern[pixelIndex.x + dims.x * pixelIndex.y] = state; // update state
    
    return word * (1.0f / 4294967296.0f);
}

float wang_hash(uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    uint rand = seed;
    seed += 10;
    return rand;
}

float randomnew(inout uint seed)
{
    float rand = wang_hash(seed) * (1.0f / 4294967296.0f);
    seed += 0x27d4eb2d;
    return rand;
}



float3 SampleHemisphere(float a, float b)
{
    float r = sqrt(a);
    float theta = 2.0f * PI * b;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0f - a);
    
    return normalize(float3(x, y, z));
}

float3x3 BuildONB(float3 n)
{
    float3 arbitrary = (abs(n.x) > 0.9f) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(n, arbitrary));
    float3 bitangent = cross(n, tangent);
    return float3x3(tangent, bitangent, n);
}

float3 localToWorld(float3 local, float3x3 onb)
{
    return local.x * onb[0] + local.y * onb[1] + local.z * onb[2];
}

float3 DirectionSampler(inout Payload payload, Material mat, float3 worldNormal, float2 uv)
{
    //uint state = randPattern[payload.pixelIndex.x + payload.pixelIndex.y * payload.dims.x];
    uint state = (payload.pixelIndex.x + payload.pixelIndex.y * payload.dims.x) * frame * 0x27d4eb2d;
    
    float rand1 = random(payload.pixelIndex, payload.dims);
    float rand2 = random(payload.pixelIndex, payload.dims);

    rand1 = randomnew(state);
    rand2 = randomnew(state);
    
    float3 localDir = SampleHemisphere(rand1, rand2);
    
    float3 worldDir = localToWorld(localDir, BuildONB(worldNormal));
    
    randPattern[payload.pixelIndex.x + payload.dims.x * payload.pixelIndex.y] = state; // update state
    return worldDir;
}

float3 throughputUpdate(inout Payload payload, Material mat, float3 worldNormal, float2 uv)
{
    //uint state = randPattern[payload.pixelIndex.x + payload.pixelIndex.y * payload.dims.x];

    
    float3 wi = WorldRayDirection() * -1;
    wi = normalize(wi);
    
    float3 wo = payload.dir; // dir already updated
    
    float NoV = saturate(dot(worldNormal, wi)); // cos_theta_i
    float NoL = saturate(dot(worldNormal, wo)); //cos_theta_o
    
        
    //randPattern[payload.pixelIndex.x + payload.dims.x * payload.pixelIndex.y] = state; // update state
    return mat.color;
}

void Shade(inout Payload payload, float2 uv)
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
    
    payload.pos = rayPos;
    
    //float3 wi = WorldRayDirection() * -1.0f;
    
    payload.emission = mat.color * mat.emission;
    
    payload.dir = DirectionSampler(payload, mat, worldNormal, uv);
    payload.throughput *= throughputUpdate(payload, mat, worldNormal, uv);
    //payload.throughput = mat.color;
    
    return;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    float2 uv = attribs.barycentrics;
    payload.numBounces++;
    Shade(payload, uv);

    return;
    
}



[shader("miss")]
void Miss(inout Payload payload)
{
    float slope = normalize(WorldRayDirection()).y;
    float t = saturate(slope * 5 + 0.5);
    payload.throughput *= lerp(skyBottom, skyTop, t);

    payload.missed = true;
    return;
}