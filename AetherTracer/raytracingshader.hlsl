struct [raypayload] Payload // 76 bytes
{
    float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 emission : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 pos : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 dir : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    uint numBounces : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    bool missed : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    bool internal : read(caller, closesthit, miss) : write(caller, closesthit, miss);
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
RWBuffer<uint64_t> randPattern : register(u1, space0);

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
    uint numFrames;
    bool sky;
}


// constants
static const float3 skyTop = float3(0.24, 0.44, 0.72);
static const float3 skyBottom = float3(0.75, 0.86, 0.93);
static const float PI = 3.141592653589793; // why not

float randomPCG(inout uint64_t state)
{
    uint64_t oldstate = state;
    state = oldstate * 6364136223846793005ULL + 2891336453ULL;

    uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = uint32_t(oldstate >> 59u);
    
    uint32_t result = (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    return float(result) * (1.0f / 4294967296.0f);
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixelIndex = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    
    uint64_t state = randPattern[pixelIndex.x + pixelIndex.y * dims.x];
    randomPCG(state); // initialize
    float2 jitter = float2(randomPCG(state), randomPCG(state)) - 0.5f;
    
    float2 uv = (pixelIndex + 0.5f + jitter) / float2(dims);
    
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
    payload.internal = false;
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    
    for (uint numBounces = 0; numBounces <= 8; numBounces++)
    {
        RAY_FLAG ray_flags = payload.internal ? RAY_FLAG_NONE : RAY_FLAG_CULL_BACK_FACING_TRIANGLES; // for refraction
        TraceRay(scene, ray_flags, 0xFF, 0, 0, 0, ray, payload);
        
        
        ray.Origin = payload.pos;
        ray.Direction = payload.dir;
       
        finalColor += payload.throughput * payload.emission;
     
        if (payload.missed || payload.emission.x > 0.0f || payload.emission.y > 0.0f || payload.emission.z > 0.0f)
        {
            break;
        }

    }
    
    accumulationTexture[pixelIndex] += float4(finalColor, 1.0f);
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

float3 worldToLocal(float3 world, float3x3 onb)
{
    return float3(dot(world, onb[0]), dot(world, onb[1]), dot(world, onb[2]));
}

// GGX Normal Distribution Function
float D_GGX(float omega_m_dot_n, float alpha)
{
    float a2 = alpha * alpha;
    float d = (omega_m_dot_n * omega_m_dot_n * (a2 - 1.0f) + 1.0f);
    return a2 / (PI * d * d);
}

// monodirectional shadowing
float G1_Smith(float omega_dot_n, float alpha)
{
    float a2 = alpha * alpha;
    float cos2 = omega_dot_n * omega_dot_n;
    float tan2 = (1.0f - cos2) / cos2;
    return 2.0f / (1.0f + sqrt(1.0f + a2 * tan2));
}

// Fresnel schlick
float3 fresnelSchlickMetallic(float cos_theta, float3 f0)
{
    return f0 + (float3(1.0f, 1.0f, 1.0f) - f0) * pow(1.0f - cos_theta, 5.0f);
}

float fresnelSchlickIOR(inout Payload payload, float cos_theta, float ior)
{
    float r0 = payload.internal ? (1.0003f - ior) / (1.0003f + ior) : (ior - 1.0003f) / (ior + 1.0003f);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * pow(1.0f - cos_theta, 5.0f);
}

// Visible normal distribution function (VNDF)
float3 SampleGGX_VNDF(float3 omega_i, float alpha, float3 xi) {

    float U1 = xi.x;
    float U2 = xi.y;
    
    // stretch view
    float3 Vh = normalize(float3(alpha * omega_i.xy, omega_i.z));

    // Orthonomral basis
    float len_sq = Vh.x * Vh.x + Vh.y * Vh.y;
    
    float3 T1, T2;
    if (len_sq > 0.0f)
    {
        float perp = 1.0f / sqrt(len_sq);
        T1 = float3(Vh.y * perp, -Vh.x * perp, 0.0f);
        T2 = cross(Vh, T1);
    }
    else
    {
        T1 = float3(1.0f, 0.0f, 0.0f);
        T2 = float3(0.0f, 1.0f, 0.0f);
    }
    
    
    // Sample projected half-disks with correct proportion
    float a = 1.0f / (1.0f + Vh.z);
    float r = sqrt(xi.x);
    float phi;
    if (xi.y < a)
    {
        phi = xi.y / a * PI;
    }
    else
    {
        phi = PI + (xi.y - a) / (1.0f - a) * PI;
    }
    
    float P1 = r * cos(phi);
    float P2 = r * sin(phi) * (xi.y < a ? 1.0f : Vh.z);
    
    // Project to sphere along Vh
    float h = sqrt(max(0.0f, 1.0f - P1 * P1 - P2 * P2));
    float3 Nh = P1 * T1 + P2 * T2 + h * Vh;
    
    // Umstretch back
    float3 omega_m = normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0f, Nh.z)));
    return omega_m;

}

float3 SampleBRDF_GGX(float3 omega_i, float alpha, float3 xi)
{
    float3 omega_m = SampleGGX_VNDF(omega_i, alpha, xi);
    // invalid sample, resample
    
    if (dot(omega_i, omega_m) <= 0.0f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    
    float3 omega_o = reflect(-omega_i, omega_m);
    return omega_o;
}

float PdfGGX_VNDF(float3 omega_i, float3 omega_o, float alpha)
{
    if (dot(omega_o, float3(0.0f, 0.0f, 1.0f)) <= 0.0f)
    {
        return 0.0f;
    }
    
    float3 omega_m = normalize(omega_i + omega_o);
    float D = D_GGX(dot(omega_m, float3(0.0f, 0.0f, 1.0f)), alpha);
    float G1 = G1_Smith(dot(omega_i, float3(0.0f, 0.0f, 1.0f)), alpha);
    
    float numer = D * dot(omega_m, float3(0.0f, 0.0f, 1.0f));
    float denom = (4.0f * abs(dot(omega_i, float3(0.0f, 0.0f, 1.0f))));
    
    return numer / denom;
}

float3 EvalBRDF_GGX(float3 omega_i, float3 omega_o, float alpha, float3 f0, float3 N)
{
    if (dot(omega_i, N) <= 0.0f || dot(omega_o, N) <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    
    float3 omega_m = normalize(omega_i + omega_o);
    float d = D_GGX(dot(omega_m, N), alpha);
    float g = G1_Smith(dot(omega_i, N), alpha) * G1_Smith(dot(omega_o, N), alpha);
    float3 f = fresnelSchlickMetallic(dot(omega_i, omega_m), f0);
    float denom = 4.0f * abs(dot(omega_i, N)) * abs(dot(omega_o, N));
    return (d * g * f) / denom;
}

float3 specularDirection(inout Payload payload, Material mat, float3 worldNormal, float2 uv, inout uint64_t state)
{
    float3 wi = WorldRayDirection() * -1;
    wi = normalize(wi);
    
    float roughness = mat.roughness;
    float alpha = roughness * roughness;
    alpha = max(alpha, 0.001f);
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), mat.color, mat.metallic);
    
    // local frame
    float3x3 onb = BuildONB(worldNormal);
   
    // transform view dir to local space
    float3 viewDirLocal = worldToLocal(wi, onb);
    
    float2 xi = float2(randomPCG(state), randomPCG(state));
    
    // sample local outgoing direction
    float3 lightDirLocal = SampleBRDF_GGX(viewDirLocal, alpha, float3(xi.x, xi.y, 0.0f));
    
    // transform to world space
    float3 wo = localToWorld(lightDirLocal, onb);
    
    return wo;
}

float3 specularThroughput(inout Payload payload, Material mat, float3 worldNormal, float2 uv, inout uint64_t state)
{
    float3 wo = WorldRayDirection() * -1.0f;
    float3 wi = payload.dir;
    
    float roughness = mat.roughness;
    float alpha = roughness * roughness;
    alpha = max(alpha, 0.001f);
   
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), mat.color, mat.metallic);
    
    // build local frame
    float3x3 onb = BuildONB(worldNormal);
    
    // transform to local
    float3 viewDirLocal = worldToLocal(wi, onb);
    float3 lightDirLocal = worldToLocal(wo, onb);
    
    // compute pdf
    float pdf = PdfGGX_VNDF(viewDirLocal, lightDirLocal, alpha);
    if (pdf <= 0.0001f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    
    // elvaluate brdf
    float3 brdf = EvalBRDF_GGX(viewDirLocal, lightDirLocal, alpha, f0, float3(0.0f, 0.0f, 1.0f));
    
    // cosine term
    float cosTheta = abs(lightDirLocal.z);
    
    // throughput
    float3 throughput = brdf * cosTheta / pdf;
    
    return throughput;
}

float3 diffuseDirection(inout Payload payload, Material mat, float3 worldNormal, float2 uv, inout uint64_t state)
{
    float rand1 = randomPCG(state);
    float rand2 = randomPCG(state);
    
    float3 localDir = SampleHemisphere(rand1, rand2);
    float3 worldDir = localToWorld(localDir, BuildONB(worldNormal));
    
    return worldDir;
}

float3 diffuseThroughput(inout Payload payload, Material mat, float3 worldNormal, float2 uv, inout uint64_t state)
{
    return mat.color;
}

float3 refractionDirection(inout Payload payload, Material mat, float3 worldNormal, float2 uv, inout bool TIR, inout uint64_t state)
{
    float3 wi = normalize(WorldRayDirection());

    float n1 = payload.internal ? mat.ior : 1.0003f;
    float n2 = payload.internal ? 1.0003f : mat.ior;
    //worldNormal = payload.internal ? worldNormal * -1.0f : worldNormal; // already flipped
    
    float cosTheta_I = dot(worldNormal, wi) * 1.0f;
    
    float sinTheta1 = sqrt(max(0.0f, 1.0f - cosTheta_I * cosTheta_I));
    float sinTheta2 = (n1 / n2) * sinTheta1;
    
    // total internal reflection, bounce off / bounce back inside
    if (sinTheta2 >= 1.0f)
    {
        float3 reflection = reflect(wi, worldNormal);
        TIR = true;
        return reflection;
    }
    
    // valid refracton into next medium
    float cosTheta2 = sqrt(max(0.0f, 1.0f - sinTheta2 * sinTheta2));
    float3 refraction = (wi * (n1 / n2)) + (worldNormal * ((n1 / n2) * cosTheta_I - cosTheta2));
    
    normalize(refraction);
    payload.internal = payload.internal ? false : true;
    
    return refraction;
}

float3 refractionThroughput(inout Payload payload, Material mat, float3 worldNormal, float2 uv, bool TIR, inout uint64_t state)
{     
    if (TIR)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }
    
    float3 wi = normalize(WorldRayDirection());
    
    float n1 = payload.internal ? mat.ior : 1.0003f;
    float n2 = payload.internal ? 1.0003f : mat.ior;
    worldNormal = payload.internal ? worldNormal * -1.0f : worldNormal;
    
    float cosTheta_wi = abs(dot(wi, worldNormal));
    
    float eta = n1 / n2;
    
    float F = fresnelSchlickIOR(payload, cosTheta_wi, mat.ior);
    
    float3 throughput = (1.0f - F) * mat.color;
    return throughput;
}

void Shade(inout Payload payload, float2 uv, inout uint64_t state)
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
    worldNormal = payload.internal == true ? worldNormal * -1.0f : worldNormal;
    
    // Fetch material
    uint matID = materialIndexBuffer[instanceIndex];
    Material mat = Materials[matID];
    
    // Update ray
    float3 rayPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    payload.pos = rayPos;    
    payload.emission = mat.color * mat.emission;
    
    float cosTheta_i = abs(dot(WorldRayDirection(), worldNormal));
    
    // Sample lobe
    float randomSample = randomPCG(state);
    float randomSample2 = randomPCG(state);
    float p_specular = mat.metallic;
    float p_transmission = mat.transmission * (1.0f - mat.metallic);
    float p_diffuse = 1.0f - (p_specular + p_transmission);
    float F = fresnelSchlickIOR(payload, cosTheta_i, mat.ior);
    
    // Lobe selection
    bool TIR = false;
  
    // Specular lobe
    if (randomSample <= p_specular)
    {
        payload.dir = specularDirection(payload, mat, worldNormal, uv, state);
        payload.throughput *= specularThroughput(payload, mat, worldNormal, uv, state);
    }
    // Transmission lobe
    else if (randomSample <= p_specular + p_transmission)
    {
        // Specular (Glass)
        if (randomSample2 < F)
        {
            payload.dir = specularDirection(payload, mat, worldNormal, uv, state);
            payload.throughput *= specularThroughput(payload, mat, worldNormal, uv, state);
        }
        // Refraction
        else
        {
            payload.dir = refractionDirection(payload, mat, worldNormal, uv, TIR, state);
            payload.throughput *= refractionThroughput(payload, mat, worldNormal, uv, TIR, state);
        }
    }
        // Diffuse lobe
    else if (randomSample <= p_specular + p_transmission + p_diffuse)
    {
        payload.dir = diffuseDirection(payload, mat, worldNormal, uv, state);
        payload.throughput *= diffuseThroughput(payload, mat, worldNormal, uv, state);
     }
    
    
    return;
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    uint64_t state = randPattern[payload.pixelIndex.x + payload.pixelIndex.y * payload.dims.x];
    randomPCG(state); // initialize
    
    float2 uv = attribs.barycentrics;
    payload.numBounces++;
    Shade(payload, uv, state);
    
    randPattern[payload.pixelIndex.x + payload.pixelIndex.y * payload.dims.x] = state; // write back updated state
    return;
}

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.missed = true;
    if (!sky)
    {
        payload.throughput *= float3(0.0f, 0.0f, 0.0f);
        return;
    }
    float slope = normalize(WorldRayDirection()).y;
    float t = saturate(slope * 5 + 0.5);
    payload.throughput *= lerp(skyBottom, skyTop, t);
    return;
}