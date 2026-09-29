#pragma once

#include "lc1/vk/mesh.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

namespace lc1 {

struct Transform {
    glm::vec3 position{0.0F};
    glm::vec3 rotation{0.0F}; // Degrees, counter-clockwise around each axis
    glm::vec3 scale{1.0F};

    glm::mat4 model_matrix() const
    {
        auto model = glm::mat4(1.0F);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0F, 0.0F, 0.0F));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0F, 1.0F, 0.0F));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0F, 0.0F, 1.0F));
        model = glm::scale(model, scale);
        return model;
    }
};

// Mesh and material are borrowed and must outlive submitted draws.
struct RenderObject {
    Transform transform;
    Mesh const *mesh = nullptr;
    Material const *material = nullptr;

    DrawItem draw_item() const
    {
        return {.mesh = mesh, .material = material, .model = transform.model_matrix()};
    }
};

} // namespace lc1
