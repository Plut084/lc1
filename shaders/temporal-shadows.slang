import shadow_values;

[[vk::binding(5, 0)]] Texture2D<uint4> current_visibility;
[[vk::binding(7, 0)]] Texture2D<float4> current_surface;
[[vk::binding(8, 0)]] Texture2D<uint4> history_visibility;
[[vk::binding(9, 0)]] Texture2D<float4> history_surface;

[shader("vertex")]
float4 temporalVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
ShadowOutput temporalFrag(float4 position : SV_Position)
{
    int2 pixel = int2(position.xy);
    uint4 current = current_visibility.Load(int3(pixel, 0));
    float4 surface = current_surface.Load(int3(pixel, 0));
    ShadowOutput output;
    output.visibility = current;
    output.visibility.w = 1;
    output.surface = surface;
    uint surface_id = current.z >> 16;
    // Derivatives must be evaluated before divergent early exits.
    float footprint = max(length(ddx(surface.xyz)), length(ddy(surface.xyz)));
    if (temporal.frame_info.x == 0 || surface_id == 0 || current.w != 0)
        return output;

    uint width, height;
    current_visibility.GetDimensions(width, height);
    int2 dimensions = int2(width, height);
    float4 clip = mul(temporal.previous_view_projection, float4(surface.xyz, 1));
    if (clip.w <= 0.0)
        return output;
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv >= 1.0) || projected.z <= 0.0 || projected.z >= 1.0)
        return output;
    int2 previous_pixel = int2(uv * float2(dimensions));
    uint4 history = history_visibility.Load(int3(previous_pixel, 0));
    float4 previous_surface = history_surface.Load(int3(previous_pixel, 0));
    float3 normal = unpack_normal(surface.w);
    float3 difference = previous_surface.xyz - surface.xyz;
    // Reject disocclusion, a different draw/face, and mismatched depth/position.
    float tolerance = clamp(footprint * 1.5, 0.01, 0.5);
    if ((history.z >> 16) != surface_id || history.w == 0 ||
        dot(normal, unpack_normal(previous_surface.w)) < 0.95 ||
        length(difference) > tolerance || abs(dot(difference, normal)) > max(0.005, footprint * 0.1))
        return output;

    // Gather only neighbors on this receiver. Clamp history to the current local
    // range so an occluder moving across a fully lit/shadowed region does not leave
    // a long trail. Stochastic penumbrae retain their mixed 0..1 sample range.
    float minimum[10];
    float maximum[10];
    uint light_count = min(temporal.frame_info.w, 10u);
    for (uint i = 0; i < light_count; ++i)
        minimum[i] = maximum[i] = unpack_visibility(current, i);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            int2 neighbor_pixel = clamp(pixel + int2(x, y), int2(0), dimensions - 1);
            uint4 neighbor = current_visibility.Load(int3(neighbor_pixel, 0));
            if ((neighbor.z >> 16) != surface_id)
                continue;
            float4 neighbor_surface = current_surface.Load(int3(neighbor_pixel, 0));
            if (dot(normal, unpack_normal(neighbor_surface.w)) < 0.95 ||
                abs(dot(neighbor_surface.xyz - surface.xyz, normal)) > max(0.005, footprint * 0.1))
                continue;
            for (uint i = 0; i < light_count; ++i) {
                float value = unpack_visibility(neighbor, i);
                minimum[i] = min(minimum[i], value);
                maximum[i] = max(maximum[i], value);
            }
        }
    uint count = min(history.w + 1u, temporal.frame_info.z);
    for (uint i = 0; i < light_count; ++i) {
        float previous = clamp(unpack_visibility(history, i), minimum[i], maximum[i]);
        pack_visibility(output.visibility, i, lerp(previous, unpack_visibility(current, i), 1.0 / float(count)));
    }
    output.visibility.w = count;
    return output;
}
