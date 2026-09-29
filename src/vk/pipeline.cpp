#include "lc1/vk/pipeline.hpp"

#include "lc1/vk/device.hpp"
#include "lc1/vk/gpu-image.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <vector>

namespace lc1 {

Pipeline::Pipeline(Device const &device, ShaderStages const &shader_stages,
                   std::vector<vk::Format> const &color_attachment_formats,
                   vk::Format depth_attachment_format, vk::SampleCountFlagBits samples,
                   PipelineKind kind)
{
    bool const depth_only = kind == PipelineKind::Shadow;
    bool const preview = kind == PipelineKind::ShadowPreview;
    // Viewport/scissor follow the swapchain; front face follows each model's handedness.
    // Neither a resize nor a mirrored draw requires another pipeline.
    std::vector<vk::DynamicState> dynamic_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eFrontFace,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.setDynamicStates(dynamic_states);

    auto binding_description = Vertex::binding_description();
    auto attribute_descriptions = Vertex::attribute_descriptions();
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

    // Sample shading: the fragment shader is invoked minSampleShading *
    // rasterizationSamples times per fragment -- every sample gets its own
    // invocation, and FragCoord then reports that sample's location instead of
    // the fragment center. MSAA on its own antialiases triangle coverage only;
    // this is what also antialiases what the shader itself computes, at up to
    // one fragment invocation per sample.
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = samples,
        .sampleShadingEnable = vk::True,
        // Samples per pixel: max(minSampleShading × rasterizationSamples, 1)
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
        .blendEnable = vk::True,
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
    if (depth_only)
        color_blend_attachments.clear();
    color_blending.setAttachments(color_blend_attachments);

    // Stable low sets: camera (0), object (1), then material (2).
    // Materials are shared across frames; writable camera/object data is not.
    frame_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(frame_bindings));

    object_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(object_bindings));

    vk::DescriptorSetLayoutBinding const material_binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    };
    material_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(material_binding));

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
