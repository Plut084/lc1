#include "lc1/vk/render/pipeline.hpp"

#include "lc1/vk/core/device.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include "lc1/vk/resources/shader-stages.hpp"
#include "vertex-input.hpp"

#include <array>
#include <vector>

namespace lc1 {

Pipeline::Pipeline(Device const &device, ShaderStages const &shader_stages,
                   std::vector<vk::Format> const &color_attachment_formats,
                   vk::Format depth_attachment_format, vk::SampleCountFlagBits samples,
                   PipelineKind kind)
{
    bool const depth_only = kind == PipelineKind::Shadow;
    bool const preview = kind == PipelineKind::ShadowPreview ||
                         kind == PipelineKind::ShadowTemporal || kind == PipelineKind::ToneMap;
    // Viewport/scissor follow the render target; front face follows each model's handedness.
    // Neither a resize nor a mirrored draw requires another pipeline.
    std::vector<vk::DynamicState> dynamic_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eFrontFace,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.setDynamicStates(dynamic_states);

    auto binding_description = detail::vertex_binding_description();
    auto attribute_descriptions = detail::vertex_attribute_descriptions();
    if (depth_only)
        attribute_descriptions.resize(1);
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    vertex_input_info.setVertexBindingDescriptions(binding_description)
        .setVertexAttributeDescriptions(attribute_descriptions);

    if (preview) {
        vertex_input_info.setVertexBindingDescriptions({});
        vertex_input_info.setVertexAttributeDescriptions({});
    }

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    vk::PipelineViewportStateCreateInfo viewport_state{
        .viewportCount = 1,
        .scissorCount = 1,
    };

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = preview ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
        // Counter-clockwise because the negative viewport height in
        // Renderer::record flips Y, and a flip reverses every triangle's winding.
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = depth_only,
        .depthBiasConstantFactor = depth_only ? 1.25F : 0.0F,
        .depthBiasSlopeFactor = depth_only ? 1.75F : 0.0F,
        .lineWidth = 1.0F,
    };

    // Material/BRDF shading remains per sample. The independent visibility
    // pipeline has one sample and performs the ray queries at pixel frequency.
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = samples,
        .sampleShadingEnable = kind == PipelineKind::Lit,
        .minSampleShading = 1.0F,
    };

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{
        .depthTestEnable = !preview,
        .depthWriteEnable = !preview,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments{{
        // This path supports opaque materials. Alpha masking/blending also needs
        // matching shadow semantics before it can be enabled.
        .blendEnable = vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    }};
    vk::PipelineColorBlendStateCreateInfo color_blending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
    };
    color_blend_attachments.resize(color_attachment_formats.size(),
                                   color_blend_attachments.front());
    color_blending.setAttachments(color_blend_attachments);

    // Stable low sets: camera (0), object (1), then material (2).
    // Materials are shared across frames; writable camera/object data is not.
    auto const bindings =
        kind == PipelineKind::ToneMap
            ? std::span<vk::DescriptorSetLayoutBinding const>{tone_map_bindings}
        : depth_only || kind == PipelineKind::ShadowPreview
            ? std::span<vk::DescriptorSetLayoutBinding const>{shadow_map_frame_bindings}
            : std::span<vk::DescriptorSetLayoutBinding const>{frame_bindings};
    frame_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings));

    object_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(object_bindings));

    material_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(material_bindings));

    // Index in this array is the set number the shader declares.
    std::array<vk::DescriptorSetLayout, 3> const set_layouts{
        *frame_set_layout_, *object_set_layout_, *material_set_layout_};
    vk::PipelineLayoutCreateInfo pipeline_layout_info{
        .pushConstantRangeCount = 0,
    };
    pipeline_layout_info.setSetLayouts(set_layouts);
    layout_ = device.raii().createPipelineLayout(pipeline_layout_info);

    vk::GraphicsPipelineCreateInfo graphics_pipeline{
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil_state,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = layout_,
        .renderPass = nullptr,
    };
    graphics_pipeline.setStages(shader_stages.value());

    vk::PipelineRenderingCreateInfo pipeline_rendering;
    pipeline_rendering.setColorAttachmentFormats(color_attachment_formats)
        .setDepthAttachmentFormat(depth_attachment_format);

    vk::StructureChain pipeline_create_info_chain{graphics_pipeline, pipeline_rendering};

    graphics_pipeline_ =
        device.raii().createGraphicsPipeline(nullptr, pipeline_create_info_chain.get());
}

} // namespace lc1
