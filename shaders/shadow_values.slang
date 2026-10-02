struct TemporalShadowData {
    float4x4 previous_view_projection;
    uint4 frame_info; // valid, sequence, max history frames, light count
};
[[vk::binding(6, 0)]]
ConstantBuffer<TemporalShadowData> temporal;

// xyz contain ten 8-bit light values. z's high 16 bits contain surface identity;
// w contains receiver motion (raw pass) or history length (temporal resolve).
float unpack_visibility(uint4 values, uint index)
{
    return float((values[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0;
}
void pack_visibility(inout uint4 values, uint index, float value)
{
    uint shift = (index % 4u) * 8u;
    uint encoded = uint(round(saturate(value) * 255.0));
    values[index / 4u] = (values[index / 4u] & ~(255u << shift)) | (encoded << shift);
}

float2 sign_not_zero(float2 value)
{
    return float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}
float pack_normal(float3 normal)
{
    normal /= max(dot(abs(normal), float3(1)), 1e-8);
    float2 oct = normal.xy;
    if (normal.z < 0.0)
        oct = (1.0 - abs(oct.yx)) * sign_not_zero(oct);
    uint2 encoded = uint2(round(saturate(oct * 0.5 + 0.5) * 4095.0));
    // 24-bit integer converts exactly to float, without NaN bit encodings.
    return float(encoded.x | (encoded.y << 12));
}
float3 unpack_normal(float value)
{
    uint encoded = uint(value);
    float2 oct = float2(encoded & 4095u, encoded >> 12) / 4095.0 * 2.0 - 1.0;
    float3 normal = float3(oct, 1.0 - abs(oct.x) - abs(oct.y));
    if (normal.z < 0.0)
        normal.xy = (1.0 - abs(normal.yx)) * sign_not_zero(normal.xy);
    return normalize(normal);
}

struct ShadowOutput {
    uint4 visibility : SV_Target0;
    float4 surface : SV_Target1;
};
