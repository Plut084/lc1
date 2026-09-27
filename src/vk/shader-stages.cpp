#include "lc1/vk/shader-stages.hpp"

namespace lc1 {

void ShaderStages::append(vk::ShaderStageFlagBits stage, ShaderModule const &module,
                          std::string name)
{
    entry_points_.push_back(std::move(name));
    vk::PipelineShaderStageCreateInfo shader_stage{
        .stage = stage,
        .module = module.raii(),
        .pName = entry_points_.back().c_str(),
    };
    shader_stages_.push_back(shader_stage);
}

ShaderStages::operator std::vector<vk::PipelineShaderStageCreateInfo> const &() const
{
    return shader_stages_;
}

ShaderStages::operator std::vector<vk::PipelineShaderStageCreateInfo> &()
{
    return shader_stages_;
}

} // namespace lc1
