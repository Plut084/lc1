import lights;
import shadows;

struct VSInput {
    float3 position;
    float2 uv;
    float3 color;
    float3 normal;
};

struct Camera {
    float4x4 view;
    float4x4 proj;
    float4 position;
};
[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;

struct LightsData {
    uint32_t count;
    Light lights[10];
};
[[vk::binding(1, 0)]]
ConstantBuffer<LightsData> lights;

struct ObjectData {
    float4x4 model;
    float4x4 normal_transform;
};

[[vk::binding(0, 1)]]
ConstantBuffer<ObjectData> object_data;

[[vk::binding(0, 2)]]
Sampler2D material;

struct VSOutput {
    float4 pos : SV_Position;
    float3 world_position;
    float2 uv;
    float3 color;
    float3 world_normal;
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
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET
{
    float4 albedo = material.Sample(input.uv) * float4(input.color, 1.0);

    // Prototype-wide Blinn-Phong parameters; move to material data when materials
    // acquire independent specular colors and shininess. Ambient is added once.
    const float ambient_strength = 0.05;
    const float3 specular_color = float3(0.25);
    const float shininess = 32.0;
    float3 normal =
        input.world_normal * rsqrt(max(dot(input.world_normal, input.world_normal), 1e-8));
    float3 to_view = camera.position.xyz - input.world_position;
    float3 view_direction = to_view * rsqrt(max(dot(to_view, to_view), 1e-8));
    float3 color = albedo.rgb * ambient_strength;
    for (uint i = 0; i < min(lights.count, 10u); ++i) {
        LightSample sample = sample_light(lights.lights[i], input.world_position);
        float n_dot_l = saturate(dot(normal, sample.direction));
        if (!any(sample.radiance > 0.0) || n_dot_l <= 0.0)
            continue;

        float3 halfway = sample.direction + view_direction;
        float halfway_squared = dot(halfway, halfway);
        float specular = 0.0;
        // Opposed light/view directions have no defined half vector. Back-facing
        // lighting must not create highlights, even when N dot H is positive.
        if (halfway_squared > 1e-8 && dot(normal, view_direction) > 0.0)
            specular = pow(saturate(dot(normal, halfway * rsqrt(halfway_squared))), shininess);
        color += shadow_visibility(input.world_position, i) * sample.radiance * (albedo.rgb * n_dot_l + specular_color * specular);
    }

    return float4(color, albedo.a);
}
