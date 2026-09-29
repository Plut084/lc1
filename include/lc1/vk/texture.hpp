#pragma once

#include "lc1/resource-manager.hpp"
#include "lc1/vk/common.hpp"
#include "lc1/vk/image.hpp"

#include <filesystem>

namespace lc1 {

class Device;

// An image loaded once from a file and shared, like Mesh: a hundred soldiers
// carrying the same banner sample one Texture. After construction the image is
// in SHADER_READ_ONLY_OPTIMAL, ready for the fragment shader to sample.
class Texture : public Resource {
  public:
    static Texture load_from_file(Device const &device, std::filesystem::path const &path);

    Texture(Texture const &) = delete;
    Texture(Texture &&) = default;
    Texture &operator=(Texture const &) = delete;
    Texture &operator=(Texture &&) = default;
    ~Texture() override = default;

    Image const &image() const { return image_; }

  private:
    // Decoded pixels. Defined in texture.cpp, so stb stays out of this header.
    struct Pixels;
    static Pixels load_from_file(std::filesystem::path const &path, int);

    Texture(Device const &device, std::filesystem::path const &path);

    // Delegated to: image_'s size is only known once the file is decoded, and
    // image_ has to be built in the initializer list.
    Texture(Device const &device, Pixels const &pixels);

    Image image_;
};

} // namespace lc1
