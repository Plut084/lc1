#include "lc1/error.hpp"
#include "lc1/text/glyph-atlas.hpp"
#include "lc1/text/text-layout.hpp"
#include "lc1/text/utf8.hpp"
#include "lc1/vertex.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// The same path the game loads, relative to the build tree. assets/ is not tracked
// in this repository, so the checks below are skipped when the face is absent.
constexpr char const *font_path = "runtime/assets/fonts/LiberationSans-Regular.ttf";

void check(bool condition, std::string const &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void utf8_decoding()
{
    using lc1::text::decode_utf8;
    check(decode_utf8("") == U"", "empty input decodes to nothing");
    check(decode_utf8("ABC") == U"ABC", "ascii round trip");
    check(decode_utf8("\xC3\xA9") == U"é", "two-byte sequence");
    check(decode_utf8("\xE2\x82\xAC") == U"€", "three-byte sequence");
    check(decode_utf8("\xF0\x9F\x99\x82") == U"\U0001F642", "four-byte sequence");
    // Malformed input loses only the offending byte: a stray continuation byte,
    // an overlong encoding, a surrogate and a truncated lead all disappear while
    // their neighbours survive.
    check(decode_utf8("a\x80"
                      "b") == U"ab",
          "a stray continuation byte is dropped");
    check(decode_utf8("\xC0\x80") == U"", "an overlong encoding is dropped");
    check(decode_utf8("\xED\xA0\x80"
                      "A") == U"A",
          "a surrogate is dropped");
    check(decode_utf8("A\xE2\x82") == U"A", "a truncated sequence is dropped");
    check(decode_utf8("\xF4\x90\x80\x80") == U"", "a codepoint beyond U+10FFFF is dropped");
}

void atlas_contents(lc1::text::GlyphAtlas const &atlas)
{
    check(atlas.extent().x > 0 && atlas.extent().y > 0, "atlas extent is positive");
    check(atlas.line_height() > 0.0F, "line height is positive");
    check(atlas.ascent() > 0.0F && atlas.descent() >= 0.0F, "ascent and descent are positive");
    check(atlas.pixels().size() ==
              static_cast<std::size_t>(atlas.extent().x) * atlas.extent().y * 4,
          "pixels are tightly packed RGBA8");

    auto const *m = atlas.glyph(U'M');
    check(m != nullptr, "ascii letters are rasterized");
    check(m->size.x > 0.0F && m->size.y > 0.0F, "M has a bitmap");
    check(m->advance > 0.0F, "M advances the pen");
    check(m->uv_min.x >= 0.0F && m->uv_max.x <= 1.0F && m->uv_min.y >= 0.0F && m->uv_max.y <= 1.0F,
          "glyph uvs stay inside the atlas");
    check(m->uv_min.x < m->uv_max.x && m->uv_min.y < m->uv_max.y,
          "a rasterized glyph covers atlas area");
    check(m->size.x < atlas.line_height() * 2.0F,
          "a glyph bitmap stays near the requested pixel size");

    auto const *space = atlas.glyph(U' ');
    check(space != nullptr && space->advance > 0.0F, "a space advances the pen");
    check(atlas.glyph(U'中') == nullptr, "codepoints outside the set are absent");

    // Coverage lives in alpha; rgb stays white so the shader only has to tint.
    auto const extent = atlas.extent();
    auto const alpha_at = [&](std::uint32_t x, std::uint32_t y) {
        return atlas.pixels()[(static_cast<std::size_t>(y) * extent.x + x) * 4 + 3];
    };
    auto const rgb_at = [&](std::uint32_t x, std::uint32_t y) {
        return atlas.pixels()[(static_cast<std::size_t>(y) * extent.x + x) * 4];
    };
    auto const left = static_cast<std::uint32_t>(m->uv_min.x * static_cast<float>(extent.x));
    auto const top = static_cast<std::uint32_t>(m->uv_min.y * static_cast<float>(extent.y));
    auto const right = static_cast<std::uint32_t>(m->uv_max.x * static_cast<float>(extent.x));
    auto const bottom = static_cast<std::uint32_t>(m->uv_max.y * static_cast<float>(extent.y));
    bool inked = false;
    for (auto y = top; y < bottom; ++y)
        for (auto x = left; x < right; ++x) {
            inked = inked || alpha_at(x, y) > 0;
            check(rgb_at(x, y) == 255, "atlas rgb is white");
        }
    check(inked, "M's cell holds coverage");
    check(alpha_at(left, top - 1) == 0, "the padding above a glyph is empty");
}

void measurement(lc1::text::GlyphAtlas const &atlas)
{
    auto const *a = atlas.glyph(U'A');
    auto const *b = atlas.glyph(U'B');
    auto const one = lc1::text::measure_text(atlas, "A");
    check(std::abs(one.width - a->advance) < 1e-4F, "one glyph measures its advance");
    check(std::abs(one.height - atlas.line_height()) < 1e-4F, "one line measures one line height");
    check(one.lines == 1, "one line is counted");

    auto const two = lc1::text::measure_text(atlas, "AB");
    check(std::abs(two.width - (a->advance + atlas.kerning(U'A', U'B') + b->advance)) < 1e-4F,
          "kerning joins two advances");

    auto const lines = lc1::text::measure_text(atlas, "AB\nA");
    check(lines.lines == 2 && std::abs(lines.height - 2.0F * atlas.line_height()) < 1e-4F,
          "a line break adds one line height");

    // A codepoint the atlas does not hold is skipped, pen included.
    check(std::abs(lc1::text::measure_text(atlas, "A\xE4\xB8\xAD"
                                                  "B")
                       .width -
                   two.width) < 1e-4F,
          "an unknown codepoint takes no space");
}

void single_line(lc1::text::GlyphAtlas const &atlas)
{
    auto const *a = atlas.glyph(U'A');
    std::vector<lc1::Vertex> vertices;
    lc1::text::append_text(
        vertices, atlas, "AB",
        {.origin = {10.0F, 20.0F, 3.0F}, .scale = 1.0F, .color = {0.25F, 0.5F, 1.0F}});
    check(vertices.size() == 12, "one quad per visible glyph");
    for (auto const &vertex : vertices) {
        check(vertex.color == glm::vec3{0.25F, 0.5F, 1.0F}, "the colour reaches every vertex");
        check(vertex.position.z == 3.0F, "the plane keeps the placement's depth");
        check(vertex.uv.x >= 0.0F && vertex.uv.x <= 1.0F && vertex.uv.y >= 0.0F &&
                  vertex.uv.y <= 1.0F,
              "uvs stay inside the atlas");
        check(vertex.normal == glm::vec3{0.0F, 0.0F, 1.0F}, "quads face the plane's +z");
    }
    check(std::abs(vertices[0].position.x - (10.0F + a->bearing.x)) < 1e-4F,
          "the first quad starts at the pen plus its left bearing");
    check(vertices[6].position.x > vertices[0].position.x, "the second glyph follows the first");
    auto const across = vertices[1].position - vertices[0].position;
    auto const up = vertices[2].position - vertices[1].position;
    check(glm::cross(across, up).z > 0.0F, "counter-clockwise winding keeps the front at +z");
    check(std::abs(glm::length(across) - a->size.x) < 1e-4F, "a quad is one bitmap wide");

    // Spaces advance the pen but emit nothing.
    std::vector<lc1::Vertex> spaced;
    lc1::text::append_text(spaced, atlas, "A B", {.scale = 1.0F});
    check(spaced.size() == 12, "a space emits no geometry");
    auto const *b = atlas.glyph(U'B');
    auto const pen_of_b = a->advance + atlas.kerning(U'A', U' ') + atlas.glyph(U' ')->advance +
                          atlas.kerning(U' ', U'B');
    check(std::abs(spaced[6].position.x - (b->bearing.x + pen_of_b)) < 1e-3F,
          "the space still moves the pen");

    std::vector<lc1::Vertex> unknown;
    lc1::text::append_text(unknown, atlas,
                           "A\xE4\xB8\xAD"
                           "B",
                           {.scale = 1.0F});
    check(unknown.size() == 12, "an unknown codepoint emits no geometry");
}

void multiple_lines(lc1::text::GlyphAtlas const &atlas)
{
    std::vector<lc1::Vertex> vertices;
    lc1::text::append_text(vertices, atlas, "A\nA", {.origin = {0.0F, 0.0F, 0.0F}, .scale = 1.0F});
    check(vertices.size() == 12, "two lines of one glyph");
    check(vertices[6].position.y < vertices[0].position.y, "later lines fall below earlier ones");
    check(std::abs((vertices[0].position.y - vertices[6].position.y) - atlas.line_height()) < 1e-4F,
          "line spacing is one line height");
}

void placement(lc1::text::GlyphAtlas const &atlas)
{
    std::vector<lc1::Vertex> left;
    lc1::text::append_text(left, atlas, "AB", {.origin = {100.0F, 0.0F, 0.0F}});
    std::vector<lc1::Vertex> centered;
    lc1::text::append_text(centered, atlas, "AB",
                           {.origin = {100.0F, 0.0F, 0.0F}, .center_x = true});
    auto const width = lc1::text::measure_text(atlas, "AB").width;
    check(std::abs((left[0].position.x - centered[0].position.x) - width * 0.5F) < 1e-4F,
          "centring shifts the run left by half its measured width");

    std::vector<lc1::Vertex> scaled;
    lc1::text::append_text(scaled, atlas, "AB", {.origin = {0.0F, 0.0F, 0.0F}, .scale = 3.0F});
    check(std::abs((scaled[1].position.x - scaled[0].position.x) -
                   3.0F * (left[1].position.x - left[0].position.x)) < 1e-3F,
          "scale multiplies the quad's extent");
}

void oriented_placement(lc1::text::GlyphAtlas const &atlas)
{
    glm::vec3 const origin{3.0F, 5.0F, 7.0F};
    for (auto const normal :
         {glm::vec3{0, 0, 1}, glm::vec3{0, 0, -1}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0},
          glm::vec3{0, -1, 0}, glm::vec3{2, 3, 4}, glm::vec3{0, 1, 0.0001F}, glm::vec3{0, 0, 1e30F},
          glm::vec3{0, 0, 1e-30F}}) {
        std::vector<lc1::Vertex> vertices;
        lc1::text::append_text(
            vertices, atlas, "A\nB",
            {.origin = origin, .scale = 0.1F, .center_x = true, .normal = normal});
        check(vertices.size() == 12, "oriented text retains all glyphs");
        auto const direction = vertices.front().normal;
        check(std::abs(glm::length(direction) - 1.0F) < 1e-5F, "text normal is normalized");
        for (auto const &vertex : vertices)
            check(std::abs(glm::dot(vertex.position - origin, direction)) < 1e-4F,
                  "every glyph stays in the requested plane");
        auto const winding =
            glm::normalize(glm::cross(vertices[1].position - vertices[0].position,
                                      vertices[2].position - vertices[0].position));
        check(glm::dot(winding, direction) > 0.999F, "front winding follows the normal");
        auto const across = glm::normalize(vertices[1].position - vertices[0].position);
        auto const up = glm::normalize(vertices[5].position - vertices[0].position);
        auto const reference =
            std::abs(direction.y) > 0.999F ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
        auto const expected_right = glm::normalize(glm::cross(reference, direction));
        check(glm::dot(across, expected_right) > 0.999F, "text right uses the upright basis");
        check(glm::dot(up, reference) > 0.0F, "text up follows the reference up");
    }
    for (auto const normal : {glm::vec3{0}, glm::vec3{std::numeric_limits<float>::infinity(), 0, 1},
                              glm::vec3{0, std::numeric_limits<float>::quiet_NaN(), 1}}) {
        bool rejected = false;
        try {
            std::vector<lc1::Vertex> vertices;
            lc1::text::append_text(vertices, atlas, "A", {.normal = normal});
        }
        catch (lc1::Error const &) {
            rejected = true;
        }
        check(rejected, "zero and non-finite normals are rejected");
    }
}

} // namespace

int main()
{
    try {
        utf8_decoding();
        if (!std::filesystem::exists(font_path)) {
            std::cout << "font_layout: " << font_path
                      << " is absent, so atlas and layout checks were skipped\n";
            return 0;
        }
        lc1::text::GlyphAtlas const atlas{font_path};
        atlas_contents(atlas);
        measurement(atlas);
        single_line(atlas);
        multiple_lines(atlas);
        placement(atlas);
        oriented_placement(atlas);
        std::cout << "font atlas, metrics and layout passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
