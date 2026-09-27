#include "lc1/vk/shader.hpp"
#include "lc1/vk/device.hpp"

namespace lc1 {

ShaderModule::ShaderModule(Device const &device, std::string_view code)
    : shader_module_(
          device.raii(),
          vk::ShaderModuleCreateInfo{.codeSize = code.size(),
                                     // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                                     .pCode = reinterpret_cast<uint32_t const *>(code.data())})
{
}

} // namespace lc1
