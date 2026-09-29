struct VSInput {
    float3 position;
    float2 uv;
};

struct Camera {
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;

struct ObjectData {
    float4x4 model;
};

[[vk::binding(0, 1)]]
ConstantBuffer<ObjectData> object_data;

[[vk::binding(0, 2)]]
Sampler2D material;

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.pos = mul(camera.proj, mul(camera.view, mul(object_data.model, float4(input.position, 1.0))));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET
{
    // float4(input.color, 1) * ;
    return material.Sample(input.uv);
}
