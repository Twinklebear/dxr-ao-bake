#include "util.hlsl"
#include "lcg_rng.hlsl"

struct VSInput {
    float4 position: POSITION0;
    float3 normal: NORMAL0;
    float2 uv: TEXCOORD0;
};

struct FSInput {
    float4 uv_position: SV_POSITION;
    float4 world_position: TEXCOORD0;
    float3 normal: NORMAL0;
};

RaytracingAccelerationStructure scene : register(t0);

cbuffer AtlasInfo : register(b0) {
    int2 dimensions; 
    int n_samples;
    float ao_length;
}

FSInput vsmain(VSInput input)
{
    FSInput result;
    result.uv_position = float4(input.uv.x * 2.f - 1.f, input.uv.y * 2.f - 1.f, 0.f, 1.f);
    result.world_position = input.position;
    result.normal = input.normal;

    return result;
}

float4 fsmain(FSInput input) : SV_TARGET0
{
    float3 v_z = normalize(input.normal);
    float3 v_x, v_y;
    ortho_basis(v_x, v_y, v_z);

    int pixel_id = (input.uv_position.x + input.uv_position.y * dimensions.y) * dimensions.x;
    LCGRand rng = get_rng(pixel_id, 0);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    RayDesc ray;
    ray.Origin = input.world_position;
    ray.TMin = 0.001f;
    ray.TMax = ao_length;

    float n_occluded = 0;
    for (int i = 0; i < n_samples; ++i) {
        const float theta = sqrt(lcg_randomf(rng));
        const float phi = 2.f * M_PI * lcg_randomf(rng);

        const float x = cos(phi) * theta;
        const float y = sin(phi) * theta;
        const float z = sqrt(1.f - theta * theta);

        ray.Direction = normalize(x * v_x + y * v_y + z * v_z);

        q.TraceRayInline(scene, 0, 0xff, ray);
        q.Proceed();

        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            n_occluded += 1.f;
        }
    }

    return 1.f - n_occluded / n_samples;
}

