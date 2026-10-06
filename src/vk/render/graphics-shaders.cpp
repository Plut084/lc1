#include "lc1/vk/render/graphics-shaders.hpp"

#include <array>

namespace lc1 {

GraphicsShaders::GraphicsShaders(Device const &device, std::filesystem::path const &path,
                                 std::string const &vertex_entry, std::string const &fragment_entry)
    : vertex_(ShaderObject::load_from_file(
          device, path, vertex_entry, vk::ShaderStageFlagBits::eVertex,
          fragment_entry.empty() ? vk::ShaderStageFlags{} : vk::ShaderStageFlagBits::eFragment))
{
    if (!fragment_entry.empty())
        fragment_.emplace(ShaderObject::load_from_file(device, path, fragment_entry,
                                                       vk::ShaderStageFlagBits::eFragment));
}

void GraphicsShaders::bind(vk::raii::CommandBuffer const &commands) const
{
    // Explicitly clear unused stages on every pass switch, including fragment
    // for depth-only rendering. No mesh/task shader features are enabled.
    constexpr std::array stages{
        vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eTessellationControl,
        vk::ShaderStageFlagBits::eTessellationEvaluation, vk::ShaderStageFlagBits::eGeometry,
        vk::ShaderStageFlagBits::eFragment};
    std::array<vk::ShaderEXT, 5> const shaders{
        *vertex_.raii(), {}, {}, {}, fragment_ ? *fragment_->raii() : vk::ShaderEXT{}};
    commands.bindShadersEXT(stages, shaders);
}

} // namespace lc1
