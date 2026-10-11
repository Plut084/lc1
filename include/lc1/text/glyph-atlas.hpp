#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace lc1::text {

// One rasterized glyph, measured in atlas pixels. `bearing` is the offset from
// the pen position to the bitmap's top-left corner with y pointing up (FreeType's
// convention), so the bitmap's top edge sits `bearing.y` above the baseline.
// Glyphs without a bitmap (spaces, for example) keep their advance and report a
// zero size; text layout advances the pen without emitting geometry for them.
struct Glyph {
    glm::vec2 uv_min{};
    glm::vec2 uv_max{};
    glm::vec2 size{};
    glm::vec2 bearing{};
    float advance = 0.0F;
};

struct GlyphAtlasOptions {
    // Em size the face is rasterized at. Layout `scale` is world or screen units
    // per atlas pixel, so callers usually divide a target height by `ascent()`.
    float pixel_size = 48.0F;
    // Codepoints to rasterize. Empty means printable ASCII, U+0020..U+007E.
    std::u32string codepoints;
};

// A face rasterized once into one RGBA8 atlas plus its metrics. Vulkan-free:
// `pixels()` is plain CPU data that GpuFont uploads. FreeType is used only inside
// the constructor, so layout code and its tests never touch it.
class GlyphAtlas {
  public:
    explicit GlyphAtlas(std::filesystem::path const &ttf, GlyphAtlasOptions options = {});

    GlyphAtlas(GlyphAtlas const &) = delete;
    GlyphAtlas &operator=(GlyphAtlas const &) = delete;
    GlyphAtlas(GlyphAtlas &&) = default;
    GlyphAtlas &operator=(GlyphAtlas &&) = default;
    ~GlyphAtlas() = default;

    // Null when the face has no glyph for the codepoint or the charmap did not
    // map it, so a codepoint outside the rasterized set is simply not drawn.
    Glyph const *glyph(char32_t code_point) const;

    // Pen adjustment between two adjacent codepoints in pixels; zero when the
    // face has no kerning for the pair.
    float kerning(char32_t left, char32_t right) const;

    float pixel_size() const { return pixel_size_; }

    float line_height() const { return line_height_; }

    // Positive above and below the baseline respectively.
    float ascent() const { return ascent_; }

    float descent() const { return descent_; }

    glm::uvec2 extent() const { return extent_; }

    // Tightly packed RGBA8: white RGB, coverage in alpha.
    std::span<std::uint8_t const> pixels() const { return pixels_; }

  private:
    std::unordered_map<char32_t, Glyph> glyphs_;
    // Packed (left codepoint << 32 | right codepoint) -> pen adjustment.
    std::unordered_map<std::uint64_t, float> kerning_;
    std::vector<std::uint8_t> pixels_;
    glm::uvec2 extent_{};
    float pixel_size_ = 0.0F;
    float line_height_ = 0.0F;
    float ascent_ = 0.0F;
    float descent_ = 0.0F;
};

} // namespace lc1::text
