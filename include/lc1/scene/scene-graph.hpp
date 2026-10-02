#pragma once

#include "lc1/image.hpp"
#include "lc1/model.hpp"
#include "lc1/scene/pbr-material.hpp"

#include <memory>
#include <vector>

namespace lc1::scene {

class SceneGraph {
  public:
    SceneGraph() = default;

    class Node;

  private:
};

class SceneGraph::Node {
  public:
    Node();

    glm::mat4 local_transform_matrix() const { return transform_.model_matrix(); }
    glm::mat4 global_transform_matrix() const
    {
        auto result = local_transform_matrix();
        if (parent_) {
            result = parent_->global_transform_matrix() * result;
        }
        return result;
    }

    std::string const &name() const { return name_; }

    void add_child(std::unique_ptr<Node> child)
    {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }

  private:
    std::string name_;
    Node *parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    Mesh *mesh_;
    Transform transform_;
};

struct AnimationChannel {
    enum class Path : std::uint8_t { Translation, Rotation, Scale };
    Path path;
    SceneGraph::Node *node;
    std::uint32_t sampler_index;
};

struct AnimationSampler {
    enum class Interpolation : std::uint8_t { Linear, Step, CubicSpline };
    Interpolation interpolation;
    std::vector<float> keyframes; // In seconds
    // Only one of these is used, depending on the channel path:
    //  values3 for Translation and Scale
    //  values4 for Rotation (quaternions).
    std::vector<glm::vec3> values3;
    std::vector<glm::vec4> values4;
};

struct Animation {
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    float start{std::numeric_limits<float>::max()};
    float end{std::numeric_limits<float>::min()};
    float current_time{};
};

class SceneModel {
  public:
    SceneGraph::Node *find_node(std::string_view name) const
    {
        auto it =
            std::ranges::find(linear_nodes_, name, [](auto const &node) { return node->name(); });
        return it == linear_nodes_.end() ? nullptr : *it;
    }

    void update_animation(std::size_t anim_index, float delta_time)
    {
        auto &anim = animations_[anim_index];
        anim.current_time += delta_time;
        for (auto &channel : anim.channels) {
            // auto time = ;
            // TODO interpolate the channel's node transform based on the sampler and current_time.
            fail("update_animation is not implemented yet");
        }
    }

  private:
    std::vector<std::unique_ptr<SceneGraph::Node>> nodes_;
    std::vector<SceneGraph::Node *> linear_nodes_;
    std::vector<PbrMaterial> materials_;
    std::vector<Animation> animations_;
};

} // namespace lc1::scene
