struct ShadowData {
    float4x4 view_projection;
    uint4 light_index;
};
[[vk::binding(2, 0)]]
ConstantBuffer<ShadowData> shadow;
[[vk::binding(3, 0)]]
Sampler2D shadow_map;

float shadow_visibility(float3 position, uint light_index)
{
    if (light_index != shadow.light_index.x)
        return 1.0;
    float4 clip = mul(shadow.view_projection, float4(position, 1));
    float3 projected = clip.xyz / clip.w;
    // The depth pass uses a negative-height viewport, hence the UV Y flip.
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;
    uint width, height;
    shadow_map.GetDimensions(width, height);
    float2 texel = 1.0 / float2(width, height);
    float visibility = 0.0;
    // Nearest depth samples + manual comparison avoid requiring linear depth filtering.
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += projected.z - 0.0001 <= shadow_map.SampleLevel(uv + float2(x, y) * texel, 0).r ? 1.0 : 0.0;
    // Fade the finite coverage edge instead of exposing a hard moving square.
    float edge = max(abs(projected.x), abs(projected.y));
    return lerp(visibility / 9.0, 1.0, smoothstep(0.9, 1.0, edge));
}
