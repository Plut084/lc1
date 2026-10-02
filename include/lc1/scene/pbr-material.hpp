#pragma once

#include <glm/glm.hpp>

namespace lc1 {
struct Image;
}

namespace lc1::scene {

// Linear factors, with glTF metallic-roughness defaults. Procedural nonmetals
// must explicitly choose metallic_factor = 0.
struct PbrParameters {
    glm::vec4 base_color_factor{1.0F};
    glm::vec3 emissive_factor{0.0F};
    float metallic_factor = 1.0F;
    float roughness_factor = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
};

struct PbrMaterial {
    PbrParameters parameters;
    // Non-owning, optional CPU images. Their scene owner must keep addresses stable.
    Image const *base_color_texture = nullptr;
    Image const *metallic_roughness_texture = nullptr;
    Image const *normal_texture = nullptr;
    Image const *occlusion_texture = nullptr;
    Image const *emissive_texture = nullptr;
};

} // namespace lc1::scene
