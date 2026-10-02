// Tangent-space normal maps use +Y along the bitangent. The model transform's
// orientation has already been folded into world_tangent.w by the vertex shader.
float3 mapped_normal(float3 normal, float4 world_tangent, float3 sampled, float scale)
{
    float3 tangent = world_tangent.xyz - normal * dot(normal, world_tangent.xyz);
    float tangent_squared = dot(tangent, tangent);
    // Interpolated frames can degenerate even if every source vertex is valid.
    if (tangent_squared <= 1e-12)
        return normal;
    tangent *= rsqrt(tangent_squared);
    float3 bitangent = cross(normal, tangent) * (world_tangent.w < 0.0 ? -1.0 : 1.0);
    float3 local = sampled * 2.0 - 1.0;
    local.xy *= scale;
    // Keep normalization finite even for a large (but finite) normal scale.
    local /= max(1.0, max(max(abs(local.x), abs(local.y)), abs(local.z)));
    float3 result = tangent * local.x + bitangent * local.y + normal * local.z;
    float squared = dot(result, result);
    return squared > 1e-12 ? result * rsqrt(squared) : normal;
}
