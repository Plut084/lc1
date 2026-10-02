#include "lc1/scene/character.hpp"
#include "lc1/game/continent.hpp"
#include "lc1/game/player-controller.hpp"
#include "lc1/model.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string{message});
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.001F;
}
bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 0.001F;
}

template <typename Function> void rejects(Function function, std::string_view message)
{
    bool rejected = false;
    try {
        function();
    }
    catch (lc1::Error const &) {
        rejected = true;
    }
    check(rejected, message);
}

lc1::Continent make_world()
{
    return lc1::Continent{{
        .origin = {-16.0F, -16.0F},
        .tile_size = 16.0F,
        .tiles = std::vector(2, std::vector<lc1::TileData>(2)),
        .spawn = {1.0F, 0.0F, 2.0F},
        .patches = {},
        .blocks = {{{5, 0, -10}, {5.1F, 4, 10}}},
        .regions = {{lc1::RegionId{1}, "平原", lc1::RegionType::Plain}},
        .locations = {},
        .roads = {},
    }};
}

void pose_and_movement()
{
    auto const world = make_world();
    lc1::scene::Character character{world};
    check(near(character.position(), world.spawn()), "default character uses world spawn");
    check(near(character.body_heading(), {0, 0, -1}) &&
              near(character.look_direction(), {0, 0, -1}),
          "initial body and look face -Z");
    character.set_body_yaw(450.0F);
    check(near(character.body_heading(), {-1, 0, 0}), "body yaw wraps and uses +Y rotation");
    character.set_look_angles(-90.0F, 100.0F);
    check(near(character.look_yaw(), -85) && near(character.look_pitch(), 89),
          "relative look angles have head limits");
    check(near(glm::length(character.look_direction()), 1) &&
              near(glm::length(character.right()), 1),
          "look basis remains normalized near vertical pitch");

    auto const body = character.body_heading();
    character.move(world, {10, 0, 3});
    check(near(character.position(), {4.7F, 0, 5}), "displacement sweeps and slides at a wall");
    check(near(character.body_heading(), body), "moving does not silently turn the body");
    auto const matrix = character.transform().model_matrix();
    check(near(glm::vec3{matrix * glm::vec4{0, 0, 0, 1}}, character.position()),
          "drawing and collision use the same world position");
    check(near(glm::vec3{matrix * glm::vec4{0, 0, -1, 0}}, body),
          "drawing and body heading use the same rotation");
    check(near(character.eye_position(), character.position() + glm::vec3{0, 1.65F, 0}),
          "eye height stays relative to the resolved feet position");

    lc1::scene::Character walker{world};
    walker.set_look_angles(0, 89);
    walker.walk(world, walker.look_direction(), 0.1F, false);
    check(near(walker.position(),
               world.spawn() + glm::vec3{0, 0, -0.45F} * std::cos(glm::radians(89.0F))),
          "walk discards vertical direction and preserves analog magnitude");
    walker.walk(world, {0, 0, -1}, 0.1F, true);
    check(near(walker.position().y, 0), "running cannot make the ground character fly");

    lc1::scene::Character configured{
        world, {.transform = {.position = {0, 0, 0}}}, {.eye_height = 1.2F, .walk_speed = 2.0F}};
    configured.walk(world, {0, 0, -0.5F}, 0.1F, false);
    check(near(configured.position(), {0, 0, -0.1F}) && near(configured.eye_position().y, 1.2F),
          "each character has its own dimensions and movement settings");

    auto const before = character.position();
    rejects([&] { character.move(world, {0, 1, 0}); }, "ground move rejects vertical displacement");
    rejects([&] { character.walk(world, {1, 0, 0}, -1, false); }, "negative time is rejected");
    rejects([&] { character.set_look_angles(std::numeric_limits<float>::quiet_NaN(), 0); },
            "non-finite look input is rejected");
    check(near(character.position(), before), "invalid input leaves position unchanged");
    rejects([&] { (void)character.draw_item(); }, "headless simulation cannot emit a null draw");
    rejects([&] { lc1::scene::Character blocked{world, {.transform = {.position = {5, 0, 0}}}}; },
            "obstructed character spawn is rejected");
    rejects([&] { lc1::scene::Character scaled{world, {.transform = {.scale = {2, 2, 2}}}}; },
            "visual unit correction cannot silently scale a collision body");
    rejects([&] { lc1::scene::Character invalid{world, {}, {.height = 0}}; },
            "invalid body configuration is rejected");
}

void player_controllers()
{
    auto const world = make_world();
    lc1::FirstPersonCharacterController const first_person{world};
    lc1::ThirdPersonCharacterController const third_person{world};
    lc1::scene::Character character{world};
    first_person.update(0.1F, {.movement = {0, 1}, .look_delta = {1800, -1200}}, character);
    check(near(character.position(), world.spawn() + glm::vec3{0.45F, 0, 0}),
          "first-person movement follows mouse yaw but stays horizontal at high pitch");
    check(near(character.body_heading(), {1, 0, 0}) && near(character.look_pitch(), 60),
          "mouse right turns right and mouse up looks up");
    auto const facing = character.body_heading();
    first_person.update(0.1F, {.movement = {0, -1}}, character);
    check(near(character.position(), world.spawn()) && near(character.body_heading(), facing),
          "walking backward does not turn the character around");
    first_person.update(0.1F, {.movement = {1, 0}}, character);
    check(near(character.position(), world.spawn() + glm::vec3{0, 0, 0.45F}),
          "first-person strafe uses the horizontal right vector");
    auto const before = character.position();
    third_person.update(0.1F, {.movement = {0, 1}}, {0, -2, -4}, character);
    check(near(character.position(), before + glm::vec3{0, 0, -0.45F}) &&
              near(character.body_heading(), {0, 0, -1}),
          "switching controllers moves the same character using the new camera basis");
    check(near(character.look_pitch(), 0), "third-person movement restores a level look target");
    auto const stopped = character.position();
    first_person.update(0.1F, {}, character);
    third_person.update(0.1F, {}, {0, 0, -1}, character);
    check(near(character.position(), stopped),
          "neutral input from the UI/focus gate does not move");
}

std::vector<lc1::Vertex> triangle()
{
    return {
        {.position = {0, 0, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {0, 1, 0}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
}

void model_correction()
{
    lc1::Model plain{triangle(), {0, 1, 2}};
    plain.generate_tangents();
    check(lc1::valid_tangent_frame(plain.vertices()[0]), "model exposes tangent preprocessing");
    auto with_tangents = triangle();
    std::vector<std::uint32_t> tangent_indices{0, 1, 2};
    lc1::generate_tangents(with_tangents, tangent_indices);
    lc1::Model const tangent_model{
        with_tangents, tangent_indices, {.rotation = {-90, 0, 0}, .scale = {-2, 3, 4}}};
    for (auto const &vertex : tangent_model.vertices()) {
        check(lc1::valid_tangent_frame(vertex), "baked tangent remains valid");
        check(near(glm::vec3{vertex.tangent}, {-1, 0, 0}) && near(vertex.tangent.w, -1),
              "baked reflection transforms tangent and handedness");
        check(near(vertex.normal, {0, 1, 0}), "baked normal uses the inverse transpose");
    }
    auto diagonal_uv = triangle();
    for (auto &vertex : diagonal_uv)
        vertex.uv = {vertex.position.x + vertex.position.y, vertex.position.y - vertex.position.x};
    lc1::generate_tangents(diagonal_uv, tangent_indices);
    lc1::Model const diagonal_model{
        diagonal_uv, tangent_indices, {.rotation = {-90, 0, 0}, .scale = {-2, 3, 4}}};
    check(
        near(glm::vec3{diagonal_model.vertices()[0].tangent}, glm::normalize(glm::vec3{-2, 0, -3})),
        "non-axis-aligned tangents use the linear transform, not its inverse transpose");
    lc1::Model const corrected{
        triangle(),
        {0, 1, 2},
        {.position = {1, 2, 3}, .rotation = {-90, 0, 0}, .scale = {2, 3, 4}}};
    check(near(corrected.vertices()[0].position, {1, 2, 3}) &&
              near(corrected.vertices()[1].position, {3, 2, 3}) &&
              near(corrected.vertices()[2].position, {1, 2, 0}),
          "model-level unit, axis and origin correction is baked into the shared vertices");
    check(near(corrected.vertices()[0].normal, {0, 1, 0}), "axis correction rotates normals");
    check(corrected.vertices()[1].uv == glm::vec2{1, 0}, "model correction preserves UVs");

    auto tilted = triangle();
    tilted[2].position = {0, 1, 1};
    for (auto &vertex : tilted)
        vertex.normal = glm::normalize(glm::vec3{0, -1, 1});
    lc1::Model const mirrored{tilted, {0, 1, 2}, {.scale = {-2, 3, 4}}};
    auto const points = mirrored.vertices();
    auto const indices = mirrored.indices();
    auto const face_normal =
        glm::normalize(glm::cross(points[indices[1]].position - points[indices[0]].position,
                                  points[indices[2]].position - points[indices[0]].position));
    check(near(face_normal, points[0].normal),
          "nonuniform mirrored correction preserves winding and inverse-transpose normals");
    check(near(points[0].normal, {0, -0.8F, 0.6F}), "nonuniform scale keeps normals perpendicular");
    rejects([] { lc1::Model invalid{triangle(), {0, 1, 2}, {.scale = {1, 0, 1}}}; },
            "singular model correction is rejected");
    rejects([] { lc1::Model invalid{triangle(), {0, 1, 9}}; },
            "invalid model indices are rejected");
}

} // namespace

int main()
{
    try {
        pose_and_movement();
        player_controllers();
        model_correction();
        std::cout << "character tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
