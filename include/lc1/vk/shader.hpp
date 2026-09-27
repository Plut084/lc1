#pragma once

#include "lc1/vk/common.hpp"

#include <string_view>

namespace lc1 {

class Device;

class ShaderModule {
  public:
    ShaderModule(Device const &device, std::string_view code);

    ShaderModule(ShaderModule const &) = delete;
    ShaderModule(ShaderModule &&) = delete;
    ShaderModule &operator=(ShaderModule const &) = delete;
    ShaderModule &operator=(ShaderModule &&) = delete;

    ~ShaderModule() = default;

    vk::raii::ShaderModule const &raii() const { return shader_module_; }

  private:
    vk::raii::ShaderModule shader_module_;
};

} // namespace lc1
