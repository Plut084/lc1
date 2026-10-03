#pragma once

#include "lc1/scene/transform.hpp"
#include "lc1/vk/gpu-mesh.hpp"

namespace lc1::scene {

// GpuMesh and material are borrowed and must outlive submitted draws.
struct Object {
    Transform transform;
    GpuMesh const *mesh = nullptr;
    GpuMaterial const *material = nullptr;
    bool casts_shadow = true;

    DrawItem draw_item() const
    {
        return {.mesh = mesh,
                .material = material,
                .model = transform.model_matrix(),
                .casts_shadow = casts_shadow};
    }
};

} // namespace lc1::scene
