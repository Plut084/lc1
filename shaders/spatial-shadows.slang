import pass_data;
import shadow_values;
import lights;

struct LightsData {
    uint count;
    Light lights[10];
};

[shader("vertex")]
float4 spatialVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
uint4 spatialFrag(float4 position : SV_Position) : SV_Target0
{
    ConstantBuffer<LightsData> lighting = ResourceDescriptorHeap[pass_data.lights];
    Texture2D<uint4> resolved_visibility = ResourceDescriptorHeap[pass_data.resolved_visibility];
    Texture2D<float4> current_surface = ResourceDescriptorHeap[pass_data.current_surface];

    int2 pixel = int2(position.xy);
    uint4 center = resolved_visibility.Load(int3(pixel, 0));
    float4 surface = current_surface.Load(int3(pixel, 0));
    float footprint = max(length(ddx(surface.xyz)), length(ddy(surface.xyz)));
    uint surface_id = center.z >> 16;
    if (surface_id == 0)
        return center;

    uint light_count = min(lighting.count, 10u);
    bool has_area_light = false;
    for (uint i = 0; i < light_count; ++i)
        has_area_light = has_area_light || (lighting.lights[i].type == 3 && lighting.lights[i].radius > 0.0);
    if (!has_area_light)
        return center;

    float3 normal = unpack_normal(surface.w);
    float plane_sigma = max(0.002, min(footprint, 0.5) * 0.1);
    float sums[10];
    float weights[10];
    for (uint i = 0; i < light_count; ++i) {
        sums[i] = unpack_visibility(center, i);
        weights[i] = 1.0;
    }
    uint width, height;
    resolved_visibility.GetDimensions(width, height);
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0)
                continue;
            int2 neighbor_pixel = pixel + int2(x, y);
            if (any(neighbor_pixel < 0) || any(neighbor_pixel >= int2(width, height)))
                continue;
            uint4 neighbor = resolved_visibility.Load(int3(neighbor_pixel, 0));
            if ((neighbor.z >> 16) != surface_id)
                continue;
            float4 neighbor_surface = current_surface.Load(int3(neighbor_pixel, 0));
            float3 neighbor_normal = unpack_normal(neighbor_surface.w);
            float agreement = dot(normal, neighbor_normal);
            if (agreement < 0.95)
                continue;
            float3 delta = neighbor_surface.xyz - surface.xyz;
            // Symmetric tangent-plane distance preserves slopes without mixing
            // parallel surfaces at different depths or faces meeting at corners.
            float plane_distance = max(abs(dot(delta, normal)), abs(dot(delta, neighbor_normal)));
            if (plane_distance > 3.0 * plane_sigma ||
                length(delta) > max(0.01, footprint) * (length(float2(x, y)) + 1.0) * 2.0)
                continue;
            float geometry_weight = exp(-float(x*x + y*y) / 4.0) * pow(max(agreement, 0.0), 32.0) *
                                    exp(-plane_distance / plane_sigma);
            for (uint i = 0; i < light_count; ++i) {
                if (lighting.lights[i].type != 3 || lighting.lights[i].radius <= 0.0)
                    continue;
                float value = unpack_visibility(neighbor, i);
                // Trust contrast only after both histories have accumulated:
                // raw binary samples must not be mistaken for shadow edges.
                float confidence = saturate((float(min(center.w, neighbor.w)) - 1.0) / 15.0);
                float sigma = lerp(1.0, 0.1, confidence);
                float difference = value - unpack_visibility(center, i);
                float weight = geometry_weight * exp(-difference * difference / (2.0 * sigma * sigma));
                sums[i] += weight * value;
                weights[i] += weight;
            }
        }
    uint4 output = center;
    for (uint i = 0; i < light_count; ++i)
        if (lighting.lights[i].type == 3 && lighting.lights[i].radius > 0.0)
            pack_visibility(output, i, sums[i] / weights[i]);
    return output;
}
