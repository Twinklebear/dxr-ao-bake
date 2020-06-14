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

FSInput vsmain(VSInput input)
{
    FSInput result;
    result.uv_position = float4(input.uv.x * 2.f - 1.f, input.uv.y * 2.f - 1.f, 0.f, 1.f);
    result.world_position = input.position;
    result.normal = input.normal;

    return result;
}

float4 fsmain(FSInput) : SV_TARGET0
{
    return float4(1.f, 1.f, 1.f, 1.f);
}

