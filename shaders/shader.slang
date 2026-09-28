struct VSInput {
    float3 position;
    float3 color;
    float2 uv;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

// Set 1 (the Material), not set 0: without the attribute Slang would place it
// after ubo, at set 0 binding 1.
[[vk::binding(0, 1)]]
Sampler2D texture;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.position, 1.0))));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET
{
    // float4(input.color, 1) * ;
    return texture.Sample(input.uv);
}
