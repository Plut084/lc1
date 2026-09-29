#pragma once

#include "lc1/error.hpp"

#include <glm/glm.hpp>
#include <span>
#include <stb_image.h>

#include <filesystem>
#include <memory>

namespace lc1 {

struct StbiFree {
    void operator()(stbi_uc *data) const { stbi_image_free(data); }
};

struct Image {
    std::unique_ptr<stbi_uc, StbiFree> data;
    glm::uvec2 extent;

    // 4 bytes per pixel: load() asks stb for RGBA whatever the file holds.
    std::span<stbi_uc const> bytes() const
    {
        return {data.get(), std::size_t{extent.x} * extent.y * 4};
    }

    static Image load_from_file(std::filesystem::path const &path)
    {
        int width = 0;
        int height = 0;
        int channels_in_file = 0;
        stbi_uc *data =
            stbi_load(path.string().c_str(), &width, &height, &channels_in_file, STBI_rgb_alpha);
        if (data == nullptr) {
            fail("failed to load {}: {}", std::filesystem::absolute(path).string(),
                 stbi_failure_reason());
        }
        return {
            .data = std::unique_ptr<stbi_uc, StbiFree>{data},
            .extent = {width, height},
        };
    }
};

} // namespace lc1
