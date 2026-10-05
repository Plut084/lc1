import lights;
import ray_shadows;
import shadow_values;
import modules.pbr;
import modules.normal;

// SamplerDescriptorHeap
// ResourceDescriptorHeap

struct VSInput {
    float3 position;
    float3 normal;
    float3 color;
    float2 uv;
    float4 tangent;
};

struct Camera {
    float4x4 view;
    float4x4 proj;
    float4 position;
};

struct LightsData {
    uint32_t count;
    Light lights[10];
};

[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;
[[vk::binding(1, 0)]]
ConstantBuffer<LightsData> lights;

struct ObjectData {
    float4x4 model;
    float4x4 normal_transform;
    uint4 shadow_identity;
};

[[vk::binding(0, 1)]]
ConstantBuffer<ObjectData> object_data;

[[vk::binding(0, 2)]]
Sampler2D base_color_texture;

struct MaterialData {
    float4 base_color_factor;
    float4 emissive_factor;
    float4 surface;
    uint4 flags;
};
[[vk::binding(1, 2)]] ConstantBuffer<MaterialData> material;
[[vk::binding(2, 2)]] Sampler2D metallic_roughness_texture;
[[vk::binding(3, 2)]] Sampler2D normal_texture;
[[vk::binding(4, 2)]] Sampler2D occlusion_texture;
[[vk::binding(5, 2)]] Sampler2D emissive_texture;

[[vk::binding(5, 0)]]
Texture2D<uint4> shadow_visibility_values;

struct VSOutput {
    float4 pos : SV_Position;
    float3 world_position;
    float2 uv;
    float3 color;
    float3 world_normal;
    float4 world_tangent;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    float4 world_position = mul(object_data.model, float4(input.position, 1.0));
    output.pos = mul(camera.proj, mul(camera.view, world_position));
    output.world_position = world_position.xyz;
    output.uv = input.uv;
    output.color = input.color;
    output.world_normal = mul((float3x3)object_data.normal_transform, input.normal);
    float handedness = determinant((float3x3)object_data.model) < 0.0 ? -1.0 : 1.0;
    output.world_tangent = float4(mul((float3x3)object_data.model, input.tangent.xyz),
                                  input.tangent.w * handedness);
    return output;
}

[shader("fragment")]
[earlydepthstencil]
float4 fragMain(VSOutput input) : SV_TARGET
{
    uint4 visibility = shadow_visibility_values.Load(int3(int2(input.pos.xy), 0));
    float3 base_color = saturate(base_color_texture.Sample(input.uv).rgb *
                                material.base_color_factor.rgb * input.color);
    float4 mr = metallic_roughness_texture.Sample(input.uv);
    float metallic = saturate(material.surface.x * mr.b);
    float roughness = saturate(material.surface.y * mr.g);
    float3 surface_normal =
        input.world_normal * rsqrt(max(dot(input.world_normal, input.world_normal), 1e-8));
    float3 normal = surface_normal;
    // Missing maps and zero scale use the unperturbed mesh normal, with no sample.
    if ((material.flags.x & 1u) != 0 && material.surface.z > 0.0)
        normal = mapped_normal(surface_normal, input.world_tangent,
                                normal_texture.Sample(input.uv).rgb, material.surface.z);
    float3 to_view = camera.position.xyz - input.world_position;
    float3 view_direction = to_view * rsqrt(max(dot(to_view, to_view), 1e-8));
    // Direct lighting until IBL is added. Emission is independent of shadows.
    float3 color = emissive_texture.Sample(input.uv).rgb * material.emissive_factor.rgb;
    for (uint i = 0; i < min(lights.count, 10u); ++i) {
        LightSample sample = sample_light(lights.lights[i], input.world_position);
        float n_dot_l = saturate(dot(normal, sample.direction));
        // Visibility uses the unperturbed normal. Do not let a normal map light
        // the back of that surface using a skipped/default-white shadow value.
        if (!any(sample.radiance > 0.0) || n_dot_l <= 0.0 ||
            dot(surface_normal, sample.direction) <= 0.0 || dot(surface_normal, view_direction) <= 0.0)
            continue;

        float3 brdf = evaluate_pbr(base_color, metallic, roughness, normal, view_direction,
                                   sample.direction);
        color += unpack_visibility(visibility, i) * sample.radiance * brdf * n_dot_l;
    }

    // Opaque output; preserve HDR up to the largest finite RGBA16F value.
    return float4(min(color, float3(65504.0)), 1.0);
}

// Single-sampled raster pass: cache visibility at pixel frequency, 8 bits per light.
// The MSAA material pass reuses these values instead of tracing per sample.
[shader("fragment")]
[earlydepthstencil]
ShadowOutput visibilityMain(VSOutput input)
{
    float3 normal =
        input.world_normal * rsqrt(max(dot(input.world_normal, input.world_normal), 1e-8));
    uint4 visibility = uint4(0xffffffffu);
    for (uint i = 0; i < min(lights.count, 10u); ++i) {
        LightSample sample = sample_light(lights.lights[i], input.world_position);
        if (!any(sample.radiance > 0.0) || dot(normal, sample.direction) <= 0.0)
            continue;
        float value =
            shadow_visibility(input.world_position, normal, lights.lights[i], sample.direction,
                              uint2(input.pos.xy), i, temporal.frame_info.y);
        pack_visibility(visibility, i, value);
    }
    visibility.z = (visibility.z & 65535u) | (object_data.shadow_identity.x << 16);
    visibility.w = object_data.shadow_identity.y;
    ShadowOutput output;
    output.visibility = visibility;
    output.surface = float4(input.world_position, pack_normal(normal));
    return output;
}
