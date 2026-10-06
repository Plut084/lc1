#pragma once

#include <glm/mat4x4.hpp>

namespace lc1 {

class GpuMesh;
struct MaterialInfo;

// One entry of "what to draw this frame", rebuilt every frame. Non-owning: the
// material parameters must survive record(); mesh, textures and samplers must
// survive GPU execution. Renderer snapshots parameters into the current frame slot.
struct DrawItem {
    GpuMesh const *mesh = nullptr;
    MaterialInfo const *material = nullptr;
    glm::mat4 model{1.0F};
    // Printed labels still render, but need not behave like raised shadow casters.
    bool casts_shadow = true;
};

} // namespace lc1
