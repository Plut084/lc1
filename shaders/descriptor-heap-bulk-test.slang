struct PushData {
    uint buffer_index;
    uint first_record;
    uint record_mask;
    uint row_width;
};

[[vk::push_constant]] ConstantBuffer<PushData> push_data;

struct UniformRecords {
    uint4 records[1024];
};

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
uint4 uniformMain(float4 position : SV_Position) : SV_Target
{
    ConstantBuffer<UniformRecords> data = ResourceDescriptorHeap[push_data.buffer_index];
    uint index = (uint(position.x) * 17 + push_data.first_record) & push_data.record_mask;
    return data.records[index];
}

[shader("fragment")]
uint4 storageMain(float4 position : SV_Position) : SV_Target
{
    StructuredBuffer<uint4> data = ResourceDescriptorHeap[push_data.buffer_index];
    uint linear = uint(position.y) * push_data.row_width + uint(position.x);
    uint index = (linear * 17 + push_data.first_record) & push_data.record_mask;
    return data[index];
}
