#include "heap-material.slang"

struct PushData {
    uint frame_buffer_index;
};
[[vk::push_constant]] ConstantBuffer<PushData> push_data;

[shader("fragment")]
float4 fragMain() : SV_Target
{
    FrameBuffer frame = load_frame_buffer(push_data.frame_buffer_index);
    Material material = resolve_material(frame.material);
    float4 color = material.base_color_texture.SampleLevel(
        material.base_color_sampler, float2(0.5, 0.5), 0);
    return mul(frame.camera.view_projection, color * material.base_color_factor)
        + frame.clear_color * material.roughness_factor;
}
