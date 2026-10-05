#pragma once

#include "lc1/game/continent.hpp"
#include "lc1/vk/render/renderer.hpp"

#include <deque>
#include <memory>

namespace lc1::demo {

// Walkable exhibits placed beside the main road, relative to the continent spawn.
// Geometry and materials are shared by instances; borrowed draw pointers stay stable.
class PbrShowcase {
  public:
    static std::vector<ContinentBlock> spawn_blocks();
    PbrShowcase(Device const &device, Renderer &renderer, glm::vec3 origin);
    PbrShowcase(PbrShowcase const &) = delete;
    PbrShowcase &operator=(PbrShowcase const &) = delete;
    std::span<DrawItem const> draws() const { return draws_; }
    std::span<scene::Light const> lights() const { return lights_; }
    void update(float seconds);

  private:
    enum class TexturePattern { Checks, MetallicRoughness, Emission, Floor, Normal, FineNormal };
    GpuMaterial const &material(Renderer &renderer, glm::vec3 color, float metallic,
                                float roughness, glm::vec3 emission = {});
    std::size_t place(GpuMesh const &mesh, GpuMaterial const &material, glm::vec3 position,
                      glm::vec3 scale = glm::vec3{1}, float yaw = 0.0F);
    GpuTexture const &texture(Device const &device, TexturePattern pattern,
                              TextureColorSpace color_space);

    glm::vec3 origin_;
    GpuMesh sphere_;
    GpuMesh cube_;
    GpuMesh vertex_colors_;
    std::optional<GpuMesh> labels_;
    // Textures are declared before materials, which borrow them.
    std::vector<std::unique_ptr<GpuTexture>> textures_;
    std::deque<GpuMaterial> materials_;
    std::vector<DrawItem> draws_;
    std::vector<scene::Light> lights_;
    std::size_t rotating_draw_ = 0;
    std::size_t moving_shadow_draw_ = 0;
};

} // namespace lc1::demo
