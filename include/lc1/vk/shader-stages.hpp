#pragma once

#include "lc1/vk/common.hpp"
#include "lc1/vk/shader.hpp"

#include <deque>

namespace lc1 {

class ShaderStages {
  public:
    ShaderStages() = default;

    void append(vk::ShaderStageFlagBits stage, ShaderModule const &module, std::string name);

    operator std::vector<vk::PipelineShaderStageCreateInfo> const &() const;
    operator std::vector<vk::PipelineShaderStageCreateInfo> &();
    auto const &value() const { return shader_stages_; }
    auto &value() { return shader_stages_; }

  private:
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages_;
    // Must outlive shader_stages_ because the create infos reference them.
    // And this should be stable: elements shouldn't be moved. std::deque meets the requirement
    // because `push_back` on a deque doesn't move the existing elements.
    std::deque<std::string> entry_points_;
};

} // namespace lc1
