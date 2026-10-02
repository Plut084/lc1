import lights;

[[vk::binding(4, 0)]]
RaytracingAccelerationStructure shadow_scene;

// Integer hash seeded by pixel, light and frame: temporal accumulation needs new samples.
uint shadow_hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float shadow_random(inout uint state)
{
    state = shadow_hash(state + 0x9e3779b9u);
    return float(state >> 8) * (1.0 / 16777216.0);
}

float2 sample_shadow_disk(inout uint state, uint index, uint count)
{
    // Stratify area (r^2), with a random angular rotation of a golden-angle sequence.
    // Each point is uniform over disk area; sqrt avoids biasing towards the center.
    float radius = sqrt((float(index) + shadow_random(state)) / float(count));
    float angle = 6.28318530718 * frac(float(index) * 0.61803398875 + shadow_random(state));
    return radius * float2(cos(angle), sin(angle));
}

float trace_shadow_visibility(float3 origin, float3 direction, float distance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0;
    ray.TMax = distance;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(shadow_scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_NOTHING ? 1.0 : 0.0;
}

float shadow_visibility(float3 position, float3 normal, Light light, float3 direction,
                        uint2 pixel, uint light_index, uint frame_index)
{
    float bias = max(0.002, max(max(abs(position.x), abs(position.y)), abs(position.z)) * 2e-6);
    float3 origin = position + normal * bias;
    if (light.type == 0)
        return trace_shadow_visibility(origin, direction, 1e7);

    float radius = light.type == 3 ? max(light.radius, 0.0) : 0.0;
    uint count = radius > 0.0 ? clamp(light.shadow_sample_count, 1u, 32u) : 1u;
    float3 axis = abs(direction.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(axis, direction));
    float3 bitangent = cross(direction, tangent);
    uint state = shadow_hash(pixel.x ^ shadow_hash(pixel.y) ^ shadow_hash(light_index + 1u) ^ shadow_hash(frame_index + 17u));
    float visibility = 0.0;
    for (uint i = 0; i < count; ++i) {
        float2 disk = radius > 0.0 ? sample_shadow_disk(state, i, count) * radius : float2(0);
        float3 sample_position = light.position + tangent * disk.x + bitangent * disk.y;
        float3 offset = sample_position - origin;
        float distance = length(offset);
        // Recompute direction from the biased origin, and stop before the sampled emitter.
        visibility += distance <= bias ? 1.0 :
            trace_shadow_visibility(origin, offset / distance, distance - bias);
    }
    return visibility / float(count);
}
