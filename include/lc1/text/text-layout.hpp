#pragma once

#include "lc1/text/glyph-atlas.hpp"
#include "lc1/vertex.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace lc1::text {

// Where a run of text goes, in whatever plane the caller draws in: x to the
// right, y up, z at the plane's depth. A label in the world passes its world
// position; screen text passes pixels counted from the bottom-left corner (its
// transform flips y, matching the world passes' negative-height viewport).
struct TextPlacement {
    // Top of the first line, on its ascent line; later lines fall below it. x is
    // the left edge, or the horizontal centre when `center_x` is set.
    glm::vec3 origin{};
    // Caller units per atlas pixel.
    float scale = 1.0F;
    glm::vec3 color{1.0F};
    // Centres the text horizontally about origin.x instead of starting there.
    bool center_x = false;
};

// Extent of a laid-out run in atlas pixels before `scale` is applied.
struct TextMetrics {
    float width = 0.0F;
    float height = 0.0F;
    std::size_t lines = 0;
};

// Measured with the same pen arithmetic as append_text, kerning included. Width
// is the widest line. Unknown codepoints are skipped, so they contribute nothing.
TextMetrics measure_text(GlyphAtlas const &atlas, std::string_view text);

// Appends six vertices per visible glyph (triangle list, no index buffer) with
// the winding the rest of the project uses: the front faces the plane's +z.
// Glyphs without a bitmap (spaces, codepoints outside the atlas) only move the
// pen. '\n' starts a new line.
void append_text(std::vector<Vertex> &vertices, GlyphAtlas const &atlas, std::string_view text,
                 TextPlacement const &placement);

} // namespace lc1::text
