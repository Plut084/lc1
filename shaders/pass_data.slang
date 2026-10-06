struct PassData {
    uint camera;
    uint lights;
    uint object;
    uint temporal;
    uint scene;
    uint current_visibility;
    uint current_surface;
    uint history_visibility;
    uint history_surface;
    uint resolved_visibility;
    uint shadow;
    uint shadow_image;
    uint sampler;
    uint tone_map;
    uint hdr_image;
};
[[vk::push_constant]] ConstantBuffer<PassData> pass_data;
