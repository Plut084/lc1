#pragma once

#include <glm/mat4x4.hpp>

namespace lc1 {

class GpuMesh;
class GpuMaterial;

// One entry of "what to draw this frame", rebuilt every frame. Non-owning: the
// GpuMesh and the Material must outlive the frame that draws them.
struct DrawItem {
    GpuMesh const *mesh = nullptr;
    GpuMaterial const *material = nullptr;
    glm::mat4 model{1.0F};
    // Printed labels still render, but need not behave like raised shadow casters.
    bool casts_shadow = true;
};

} // namespace lc1
