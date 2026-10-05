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
