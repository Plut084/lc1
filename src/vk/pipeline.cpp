#include "lc1/vk/pipeline.hpp"

#include "lc1/vk/device.hpp"
#include "lc1/vk/shader-stages.hpp"
#include "lc1/vk/swapchain.hpp"
#include "lc1/vk/vertex.hpp"

namespace lc1 {

Pipeline::Pipeline(Device const &device, Swapchain const &swapchain,
                   ShaderStages const &shader_stages)
{
    // Dynamic so that they follow the swapchain extent: Renderer::record sets
    // them every frame, and a resize never rebuilds the pipeline.
    std::vector<vk::DynamicState> dynamicStates{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.setDynamicStates(dynamicStates);

    auto bindingDescription = Vertex::binding_description();
    auto attributeDescriptions = Vertex::attribute_descriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.setVertexBindingDescriptions(bindingDescription)
        .setVertexAttributeDescriptions(attributeDescriptions);

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    vk::PipelineViewportStateCreateInfo viewportState{
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

    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments{{
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
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
    };
    colorBlending.setAttachments(colorBlendAttachments);

    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.push_back(vk::DescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
    });
    vk::DescriptorSetLayoutCreateInfo desc_set_layout_info;
    desc_set_layout_info.setBindings(bindings);
    descriptor_set_layout_ = device.raii().createDescriptorSetLayout(desc_set_layout_info);
    vk::PipelineLayoutCreateInfo pipeline_layout_info{
        .pushConstantRangeCount = 0,
    };
    pipeline_layout_info.setSetLayouts(*descriptor_set_layout_);
    layout_ = device.raii().createPipelineLayout(pipeline_layout_info);

    vk::GraphicsPipelineCreateInfo graphics_pipeline{
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = layout_,
        .renderPass = nullptr,
    };
    graphics_pipeline.setStages(shader_stages.value());

    vk::PipelineRenderingCreateInfo pipeline_rendering;
    std::vector<vk::Format> colorAttachmentFormats{swapchain.format()};
    pipeline_rendering.setColorAttachmentFormats(colorAttachmentFormats);

    vk::StructureChain pipelineCreateInfoChain{graphics_pipeline, pipeline_rendering};

    graphics_pipeline_ =
        device.raii().createGraphicsPipeline(nullptr, pipelineCreateInfoChain.get());
}

} // namespace lc1
