// Unified upload record; matches lc1::Light in four 16-byte rows (64-byte stride).
// Currently stored in ConstantBuffer<LightsData>, using Slang's SPIR-V uniform
// buffer layout. Recheck member offsets and array stride if compiler/layout changes.
struct Light {
    float3 position;
    uint type; // 0 = directional, 1 = point, 2 = spot
    // Unit world-space propagation direction: directional rays / spot cone axis.
    // The vector towards a directional source is therefore -direction.
    float3 direction;
    float range;
    float3 color;
    float intensity;
    // Half-angles from direction, in radians: x = inner, y = outer.
    // Require 0 <= inner <= outer < pi/2; equal angles give a hard edge.
    float2 spot_angles;
    uint padding0;
    uint padding1;
};

// Evaluated contribution for one shading point. Distance and angular weights
// are already included; the caller applies BRDF, N dot L and visibility once.
struct LightSample {
    float3 direction; // Unit vector from the shading point towards the light.
    float3 radiance; // If all zero, the caller may skip BRDF / N dot L / visibility.
};

LightSample empty_light_sample()
{
    // Finite unit placeholder; zero radiance means there is no contribution.
    return { float3(0, 1, 0), float3(0) };
}

float3 light_strength(Light light)
{
    return max(light.color, float3(0)) * max(light.intensity, 0.0);
}

float distance_attenuation(float distance_squared, float range)
{
    // Smooth finite support avoids a discontinuity when crossing the cutoff.
    // range <= 0 means unbounded inverse-square falloff.
    float window = 1.0;
    if (range > 0.0) {
        float ratio_squared = distance_squared / (range * range);
        window = saturate(1.0 - ratio_squared * ratio_squared);
        window *= window;
    }
    // Defensive for direct callers; sample_light already rejects this singularity.
    return window / max(distance_squared, 1e-8);
}

// Shared evaluation path: choose the incident vector, then apply distance and
// angular weights where applicable. Types without a weight use its identity, 1.
LightSample sample_light(Light light, float3 position)
{
    if (light.type > 2)
        return empty_light_sample();

    bool directional = light.type == 0;
    float3 offset = directional ? -light.direction : light.position - position;
    float length_squared = dot(offset, offset);
    // Directions must be unit length on upload; this rejects degenerate input,
    // not arbitrary direction magnitudes. For local lights it guards the singularity.
    if (length_squared <= 1e-8)
        return empty_light_sample();
    float3 direction = offset * rsqrt(length_squared);

    float attenuation = 1.0;
    if (!directional)
        attenuation = distance_attenuation(length_squared, light.range);

    if (light.type == 2) {
        float axis_length_squared = dot(light.direction, light.direction);
        if (axis_length_squared <= 1e-8)
            return empty_light_sample();
        float cosine = dot(-direction, light.direction * rsqrt(axis_length_squared));
        float inner_cosine = cos(light.spot_angles.x);
        float outer_cosine = cos(light.spot_angles.y);
        // Equal half-angles are a hard edge; smoothstep would divide by zero.
        float angular_weight;
        if (inner_cosine <= outer_cosine)
            angular_weight = cosine >= outer_cosine ? 1.0 : 0.0;
        else
            angular_weight = smoothstep(outer_cosine, inner_cosine, cosine);
        attenuation *= angular_weight;
    }

    return { direction, light_strength(light) * attenuation };
}
