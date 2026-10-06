// Native SPV_EXT_descriptor_heap: indices come directly from lc1::DescriptorHeap.
struct PushData {
    uint buffer_index;
    uint image_index;
    uint sampler_index;
};

struct Triangle {
    float4 vertices[3];
    float4 tint;
};

[[vk::push_constant]] ConstantBuffer<PushData> push_data;

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    ConstantBuffer<Triangle> data = ResourceDescriptorHeap[push_data.buffer_index];
    return data.vertices[vertex_id];
}

[shader("fragment")]
float4 fragMain() : SV_Target
{
    ConstantBuffer<Triangle> data = ResourceDescriptorHeap[push_data.buffer_index];
    Texture2D<float4> texture = ResourceDescriptorHeap[push_data.image_index];
    SamplerState sampler = SamplerDescriptorHeap[push_data.sampler_index];
    return texture.SampleLevel(sampler, float2(0.5, 0.5), 0) * data.tint;
}

// A full-screen sample-frequency fixture. The resolved moments distinguish
// actual per-sample invocations from a single invocation broadcast to MSAA.
[shader("vertex")]
float4 sampleVert(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(uv * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 sampleFrag(uint sample_index : SV_SampleIndex) : SV_Target
{
    float value = float(sample_index + 1) / 8.0;
    return float4(value, value * value, 1.0, 1.0);
}
