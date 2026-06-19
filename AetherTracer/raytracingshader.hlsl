struct [raypayload] Payload // 64 bytes
{
uint64_t state : read(caller, closesthit, miss) : write(caller, closesthit, miss);
float3 throughput : read(caller, closesthit, miss) : write(caller, closesthit, miss);
float3 emission : read(caller, closesthit, miss) : write(caller, closesthit, miss);
float3 pos : read(caller, closesthit, miss) : write(caller, closesthit, miss);
float3 dir : read(caller, closesthit, miss) : write(caller, closesthit, miss);
bool missed : read(caller, closesthit, miss) : write(caller, closesthit, miss);
bool internal : read(caller, closesthit, miss) : write(caller, closesthit, miss);
};

struct [raypayload] Shadow_Payload // 4 bytes
{
bool occluded : read(caller, closesthit, miss) : write(caller, closesthit, miss);
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
    uint textureMap; // index to map buffer
    float3 albedo; // raw value
    uint roughnessMap;
    float roughness;
    uint metallicMap;
    float metallic;
    float ior;
    float transmission;
    uint emissionMap;
    float emission;
    uint normalMap;
    uint displacementMap;
};
struct SampledMaterial
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

RaytracingAccelerationStructure scene : register(t1, space1);

StructuredBuffer<Vertex> VertexBuffers[] : register(t2, space2);
Buffer<uint> IndexBuffers[] : register(t3, space3);

StructuredBuffer<Material> Materials : register(t4, space4);
Buffer<uint> materialIndexBuffer : register(t5, space5);

Buffer<uint> emissiveEntitiesIndiceBuffer : register(t6, space6);

Texture2D<float4> textures[] : register(t7, space7);


cbuffer pt_params : register(b0)
{
    float3 camPos;
    float pad0;
    row_major float4x4 InvVieProj;
    uint numFrames;
    bool sky;
    float skyBrightness;
    uint minBounces;
    uint maxBounces;
    bool jitter;
    bool next_event_estimation;
    uint NEE_samples;
}

SamplerState g_sampler : register(s0);

// constants
static const float3 skyTop = float3(0.24f, 0.44f, 1.0f);
static const float3 skyBottom = float3(0.75f, 0.86f, 1.0f);
static const float PI = 3.141592653589793; // why not
const float EPSILON = 1e-5;

float randomPCGFloat(inout uint64_t state)
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
    randomPCGFloat(state); // initialize
    
    float2 jitterAmount = jitter == true ? float2(randomPCGFloat(state), randomPCGFloat(state)) - 0.5f : float2(0.0f, 0.0f);
    
    float2 uv = (pixelIndex + 0.5f + jitterAmount) / float2(dims);
    
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
    payload.state = state;
    payload.throughput = float3(1.0f, 1.0f, 1.0f);
    payload.emission = float3(0.0f, 0.0f, 0.0f);
    payload.missed = false;
    payload.internal = false;
    
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);
    
    for (uint i = 0; i <= maxBounces; i++)
    {
        RAY_FLAG ray_flags = payload.internal ? RAY_FLAG_NONE : RAY_FLAG_NONE; // for refraction
        TraceRay(scene, ray_flags, 0xFF, 0, 2, 0, ray, payload);
       
        ray.Origin = payload.pos;
        ray.Direction = payload.dir;
        
        // terminate ray if at end of path
        if (payload.missed || payload.emission.x > 0.0f || payload.emission.y > 0.0f || payload.emission.z > 0.0f)
        {
            break;
        }
        
        // Russian roullete
        if (i > minBounces)
        {
            float maxComponent = max(payload.throughput.x, max(payload.throughput.y, payload.throughput.z));
            float rand = randomPCGFloat(payload.state);
            float p = clamp(maxComponent, 0.05, 0.95);
            if (rand > p)
            {
                break;
            }
            payload.throughput *= 1.0f / p;


        }
    }
    
    finalColor += payload.emission * payload.throughput;
    //finalColor += payload.throughput;
    randPattern[pixelIndex.x + pixelIndex.y * dims.x] = payload.state; // write back updated state
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

// Fresnel schlick
float3 fresnelSchlickMetallic(float cos_theta, float3 f0)
{
    return saturate(f0 + (float3(1.0f, 1.0f, 1.0f) - f0) * pow(1.0f - cos_theta, 5.0f));
}

float fresnelSchlickIOR(float cos_theta, float ior)
{
    float r0 = (1.0003f - ior) / (1.0003f + ior);
    r0 = r0 * r0;
    return saturate(r0 + (1.0f - r0) * pow(1.0f - cos_theta, 5.0f));
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

// Visible normal distribution function (VNDF)
float3 SampleGGX_VNDF(float3 omega_i, float alpha, float3 xi)
{

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

float PdfGGX_VNDF(float3 omega_i, float3 omega_o, float alpha)
{
    
    float NoI = omega_i.z;
    float NoO = omega_o.z;
    
    if (NoI <= 0.0f || NoO <= 0.0f)
    {
        return 0.0f;
    }
    
    float3 omega_m = normalize(omega_i + omega_o);
    
    float NoM = omega_m.z;
    float IoM = abs(dot(omega_i, omega_m));
    float OoM = abs(dot(omega_o, omega_m));
    
    if (NoM <= 0.0f || IoM <= 0.0f || OoM <= 0.0f)
    {
        return 0.0f;
    }
    
    
    float D = D_GGX(NoM, alpha);
    float G1 = G1_Smith(NoI, alpha);
    
    float numer = D * G1 * IoM / abs(NoI);
    float denom = 4.0f * OoM;
    
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

float3 specularDirection(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv)
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
   
    float3 omega_m = -viewDirLocal;
    while (dot(viewDirLocal, omega_m) <= 0.0f)
    {
        float2 xi = float2(randomPCGFloat(payload.state), randomPCGFloat(payload.state));

        // sample local outgoing direction
        omega_m = SampleGGX_VNDF(viewDirLocal, alpha, float3(xi.x, xi.y, 0.0f));
    }
    
    float3 lightDirLocal = reflect(-viewDirLocal, omega_m);
    
    // transform to world space
    float3 wo = localToWorld(lightDirLocal, onb);
    
    return wo;
}

float3 specularThroughput(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv)
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
    float3 viewDirLocal = worldToLocal(wo, onb);
    float3 lightDirLocal = worldToLocal(wi, onb);
    
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

float3 diffuseDirection(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv)
{
    float rand1 = randomPCGFloat(payload.state);
    float rand2 = randomPCGFloat(payload.state);
    
    float3 localDir = SampleHemisphere(rand1, rand2);
    float3 worldDir = localToWorld(localDir, BuildONB(worldNormal));
    
    return worldDir;
}

float3 diffuseThroughput(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv)
{
    return mat.color;
}

float3 refractionDirection(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv, inout bool TIR)
{
    float3 wi = normalize(WorldRayDirection());

    float n1 = payload.internal ? mat.ior : 1.0003f;
    float n2 = payload.internal ? 1.0003f : mat.ior;
    
    float cosTheta_I = -dot(worldNormal, wi);
    
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
    
    refraction = normalize(refraction);
    payload.internal = payload.internal ? false : true;
    
    return refraction;
}

float3 refractionThroughput(inout Payload payload, SampledMaterial mat, float3 worldNormal, float2 uv, bool TIR)
{
    if (TIR)
    {
        return float3(1.0f, 1.0f, 1.0f);
        //return specularThroughput(payload, mat, worldNormal, uv);
    }
    
    float3 wi = normalize(WorldRayDirection());
    
    // payload.internal is flipped BEFORE the throughput is calculated
    float n1 = 1.0003f;
    float n2 = mat.ior;
    if (!payload.internal)
    {
        n1 = mat.ior;
        n2 = 1.0003f;
    }
    
    worldNormal = payload.internal ? worldNormal * -1.0f : worldNormal;
    
    float cosTheta_wi = abs(dot(wi, worldNormal));
        
    float F = fresnelSchlickIOR(cosTheta_wi, mat.ior);
    
    float3 throughput = (1.0f - F) * mat.color * (n2 / n1) * (n2 / n1);
    return throughput;
}

float3 NextEventEstimation(inout Payload payload, SampledMaterial sampled_material, RayDesc shadow_ray)
{
    // select random emissive entity
    uint num_emissive_entities;
    emissiveEntitiesIndiceBuffer.GetDimensions(num_emissive_entities);
    
    SampledMaterial best_material;
    best_material.emission = 0;
    best_material.color = float3(0.0f, 0.0f, 0.0f);
    float best_distance = 0;
    float3 best_tri_pos;
    float3 best_direction;
    
    for (uint i = 0; i < NEE_samples; i++)
    {
        float rand = randomPCGFloat(payload.state);
        uint rand_emissive_entity = emissiveEntitiesIndiceBuffer[uint(rand * num_emissive_entities)];
        
        uint index_buffer_size;
        IndexBuffers[rand_emissive_entity].GetDimensions(index_buffer_size);
        uint rand_triangle = uint(rand * index_buffer_size);
        rand_triangle /= 3;
        
        // Fetch random emissive triangle
        uint i0 = IndexBuffers[rand_emissive_entity][rand_triangle * 3 + 0];
        uint i1 = IndexBuffers[rand_emissive_entity][rand_triangle * 3 + 1];
        uint i2 = IndexBuffers[rand_emissive_entity][rand_triangle * 3 + 2];

        // Fetch Vertices
        Vertex v0 = VertexBuffers[rand_emissive_entity][i0];
        Vertex v1 = VertexBuffers[rand_emissive_entity][i1];
        Vertex v2 = VertexBuffers[rand_emissive_entity][i2];
        
        // Interpolate normal from three vertices
        float2 uv = float2(randomPCGFloat(payload.state), randomPCGFloat(payload.state));
        float uv0 = 1.0f - uv.x - uv.y;
        float3 normal = normalize(v0.normal * uv0 + v1.normal * uv.x + v2.normal * uv.y);
        float3 worldNormal = normalize(mul(normal, (float3x3) ObjectToWorld4x3()));
        
        // Interpoloate random position on triangle from three vertices
        float3 trianglePos = v0.position * uv0 + v1.position * uv.x + v2.position * uv.y;
        float3 trianglePosWorld = mul(trianglePos, (float3x3) ObjectToWorld4x3());
        
        float3 origin = shadow_ray.Origin;
        
        float3 direction = normalize(trianglePosWorld - origin);
        float distance = length(direction);
        
        // discard if ray is behind object
        if (dot(worldNormal, direction) >= 0.0f)
        {
            continue;
        }

        // check that part of the triangle is emissive
        uint matID = materialIndexBuffer[rand_emissive_entity];
        Material material = Materials[matID];
    
        float2 tex_coord = v0.texcoord * uv0 + v1.texcoord * uv.x + v2.texcoord * uv.y;
        SampledMaterial sampled_material;
        sampled_material.color = material.textureMap == 0 ? material.albedo : textures[NonUniformResourceIndex(material.textureMap)].SampleLevel(g_sampler, tex_coord, 0).xyz;
        sampled_material.emission = material.emissionMap == 0 ? material.emission : textures[NonUniformResourceIndex(material.emissionMap)].SampleLevel(g_sampler, tex_coord, 0).x * material.emission;

        // skip if that point on the triangle is not emissive
        if (sampled_material.emission == 0)
        {
            continue;
        }
        
        
        // no suitable triangle yet found, take this one OR new sample is better
        if (best_material.emission == 0.0f || sampled_material.emission / sqrt(distance) > best_material.emission / sqrt(best_distance))
        {
            best_material.color = sampled_material.color;
            best_material.emission = sampled_material.emission;
            best_distance = distance;
            best_tri_pos = trianglePosWorld;
            best_direction = direction;
            continue;
        }

    }
        
    if (best_material.emission == 0)
    {
        // no light found
        return float3(0.0f, 0.0f, 0.0f);
    }
    
        
    Shadow_Payload shadow_ray_payload;
    shadow_ray_payload.occluded = true;
    shadow_ray.TMax = best_distance - 1e-04;
    shadow_ray.Direction = best_direction;
    
    shadow_ray.Origin += shadow_ray.Direction * 0.0001; // nudge ray slighty
    
    TraceRay(scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, 1, 2, 1, shadow_ray, shadow_ray_payload);
   
    if (shadow_ray_payload.occluded == true)
    {
        // light occluded
        return float3(0.0f, 0.0f, 0.0f);
    }

    // place holder for now    
    return best_material.color * best_material.emission;
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
    
    // Flip normal if ray inside
    worldNormal = dot(WorldRayDirection(), worldNormal) < 0 ? worldNormal : worldNormal * -1.0f;

    // Fetch material
    uint matID = materialIndexBuffer[instanceIndex];
    Material material = Materials[matID];
    
    SampledMaterial sampled_material;
    
    float2 tex_coord = v0.texcoord * uv0 + v1.texcoord * uv.x + v2.texcoord * uv.y;
    
    sampled_material.color = material.textureMap == 0 ? material.albedo : textures[NonUniformResourceIndex(material.textureMap)].SampleLevel(g_sampler, tex_coord, 0).xyz;
    sampled_material.roughness = material.roughnessMap == 0 ? material.roughness : textures[NonUniformResourceIndex(material.roughnessMap)].SampleLevel(g_sampler, tex_coord, 0).x;
    sampled_material.metallic = material.metallicMap == 0 ? material.metallic : textures[NonUniformResourceIndex(material.metallicMap)].SampleLevel(g_sampler, tex_coord, 0).x;
    sampled_material.ior = material.ior;
    sampled_material.transmission = material.transmission;
    sampled_material.emission = material.emissionMap == 0 ? material.emission : textures[NonUniformResourceIndex(material.emissionMap)].SampleLevel(g_sampler, tex_coord, 0).x * material.emission;
    
    //float3 tri_normal = textures[material.normalMap].SampleLevel(g_sampler, uv, 0);
    //float3 mat_displacement = textures[mat.metallicMap].SampleLevel(g_sampler, uv, 0);
  
    // Update ray
    float3 rayPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
    payload.pos = rayPos;
    payload.emission = sampled_material.color * sampled_material.emission;
    
    float cosTheta_i = abs(dot(WorldRayDirection(), worldNormal));
    
    // Sample lobe
    float randomSample = randomPCGFloat(payload.state);
    float F = fresnelSchlickIOR(cosTheta_i, sampled_material.ior);
    float p_specular = lerp(F, 1.0f, sampled_material.metallic);
    float p_transmission = sampled_material.transmission * (1.0f - p_specular);
    float p_diffuse = 1.0f - (p_specular + p_transmission);
 
    
    // Lobe selection
    bool TIR = false;
  
    // Specular lobe
    if (randomSample <= p_specular)
    {
        payload.dir = specularDirection(payload, sampled_material, worldNormal, uv);
        payload.throughput *= specularThroughput(payload, sampled_material, worldNormal, uv) / p_specular;
    }
    // Transmission lobe
    else if (randomSample <= p_specular + p_transmission)
    {
      payload.dir = refractionDirection(payload, sampled_material, worldNormal, uv, TIR);
      payload.throughput *= refractionThroughput(payload, sampled_material, worldNormal, uv, TIR) / p_transmission;
    }
    // Diffuse lobe
    else if (randomSample <= p_specular + p_transmission + p_diffuse)
    {
        payload.dir = diffuseDirection(payload, sampled_material, worldNormal, uv);
        payload.throughput *= diffuseThroughput(payload, sampled_material, worldNormal, uv) / p_diffuse;
    }
    
    if (next_event_estimation)
    {
        RayDesc shadow_ray;
        shadow_ray.Origin = rayPos;
        shadow_ray.TMin = 0.001;
        float3 nee_result = NextEventEstimation(payload, sampled_material, shadow_ray);
        payload.throughput += nee_result;
    }
    
    
    return;
}

[shader("closesthit")]
void ClosestHitRadiance(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    
    float2 uv = attribs.barycentrics;
    Shade(payload, uv);
    
    return;
}

[shader("miss")]
void MissRadiance(inout Payload payload)
{
    payload.missed = true;
    if (!sky)
    {
        payload.throughput *= float3(0.0f, 0.0f, 0.0f);
        return;
    }
    
     
    float slope = normalize(WorldRayDirection()).y;
    float t = saturate(slope * 2 + 0.5);
    float3 skyColor = lerp(skyBottom, skyTop, t);
    payload.emission = float3(skyBrightness, skyBrightness, skyBrightness) * skyColor;
    return;
}

[shader("closesthit")]
void ClosestHitShadow(inout Shadow_Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.occluded = true;
    return;
}

[shader("miss")]
void MissShadow(inout Shadow_Payload payload)
{
    payload.occluded = false;
    return;
}