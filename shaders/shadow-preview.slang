import pass_data;
import shadows;

struct PreviewVertex {
    float4 position : SV_Position;
    float2 uv;
};

[shader("vertex")]
PreviewVertex previewVert(uint vertex_id : SV_VertexID)
{
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    // Fullscreen triangle with UV Y matching the negative viewport height.
    return {float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1), uv};
}

[shader("fragment")]
float4 previewFrag(PreviewVertex input) : SV_TARGET
{
    // Raw normalized depth; UVs cover the entire map.
    Texture2D<float> shadow_map = ResourceDescriptorHeap[pass_data.shadow_image];
    SamplerState shadow_sampler = SamplerDescriptorHeap[pass_data.sampler];
    float depth = shadow_map.SampleLevel(shadow_sampler, input.uv, 0);
    return float4(float3(depth), 1);
}
