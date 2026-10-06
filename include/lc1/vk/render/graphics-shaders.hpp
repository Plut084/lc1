#pragma once

#include "lc1/vk/resources/shader-object.hpp"

#include <optional>

namespace lc1 {

// A pass owns its stages through completion of all submitted GPU uses.
// An empty fragment entry selects a vertex-only depth pass.
class GraphicsShaders {
  public:
    GraphicsShaders(Device const &device, std::filesystem::path const &path,
                    std::string const &vertex_entry = "vertMain",
                    std::string const &fragment_entry = "fragMain");

    void bind(vk::raii::CommandBuffer const &commands) const;

  private:
    ShaderObject vertex_;
    std::optional<ShaderObject> fragment_;
};

} // namespace lc1
