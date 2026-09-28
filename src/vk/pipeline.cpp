#include "lc1/vk/pipeline.hpp"

#include "lc1/vk/device.hpp"
#include "lc1/vk/image.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/vk/vertex.hpp"

#include <array>
#include <vector>

namespace lc1 {

Pipeline::Pipeline(Device const &device, ShaderStages const &shader_stages,
                   std::vector<vk::Format> const &color_attachment_formats,
                   vk::Format depth_attachment_format)
{
    // Dynamic so that they follow the swapchain extent: Renderer::record sets
    // them every frame, and a resize never rebuilds the pipeline.
    std::vector<vk::DynamicState> dynamic_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.setDynamicStates(dynamic_states);

    auto binding_description = Vertex::binding_description();
    auto attribute_descriptions = Vertex::attribute_descriptions();
    vk::PipelineVertexInputStateCreateInfo vertex_input_info;
    vertex_input_info.setVertexBindingDescriptions(binding_description)
        .setVertexAttributeDescriptions(attribute_descriptions);

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
        .cullMode = vk::CullModeFlagBits::eBack,
        // Counter-clockwise because the negative viewport height in
        // Renderer::record flips Y, and a flip reverses every triangle's winding.
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0F,
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
    };

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
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
    color_blending.setAttachments(color_blend_attachments);

    // Split by how often they change, not by what they hold: a set is bound as
    // a whole, so one that mixed per-frame and per-draw data would have to
    // exist once per (frame, material) pair.
    vk::DescriptorSetLayoutBinding const frame_binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    };
    frame_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(frame_binding));

    vk::DescriptorSetLayoutBinding const material_binding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
    };
    material_set_layout_ = device.raii().createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}.setBindings(material_binding));

    // Index in this array is the set number the shader declares.
    std::array<vk::DescriptorSetLayout, 2> const set_layouts{*frame_set_layout_,
                                                             *material_set_layout_};
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
