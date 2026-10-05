#include "lc1/vk/resources/shader.hpp"
#include "lc1/vk/core/device.hpp"

#include <fstream>
#include <sstream>

namespace lc1 {
namespace {

std::string read_file(std::filesystem::path const &path)
{
    std::ifstream file(path, std::ios::binary);
    // The absolute path, because a relative one is resolved against the working
    // directory -- which is the usual reason the open failed.
    if (!file)
        fail("failed to open {}", std::filesystem::absolute(path).string());

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

ShaderModule ShaderModule::load_from_file(Device const &device, std::filesystem::path const &path)
{
    return ShaderModule{device, read_file(path)};
}

ShaderModule::ShaderModule(Device const &device, std::string_view code)
    : shader_module_(
          device.raii(),
          vk::ShaderModuleCreateInfo{.codeSize = code.size(),
                                     // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                                     .pCode = reinterpret_cast<uint32_t const *>(code.data())})
{
}

} // namespace lc1
