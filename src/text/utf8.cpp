#include "lc1/text/utf8.hpp"

#include <array>
#include <cstddef>

namespace lc1::text {

std::u32string decode_utf8(std::string_view text)
{
    // Smallest value each encoded length may represent; anything below is an
    // overlong encoding of a smaller code point.
    constexpr std::array minimum{0x0U, 0x80U, 0x800U, 0x10000U};

    std::u32string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        auto const lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        char32_t code_point = 0;
        if (lead < 0x80) {
            code_point = lead;
            length = 1;
        }
        else if ((lead & 0xE0) == 0xC0) {
            code_point = lead & 0x1F;
            length = 2;
        }
        else if ((lead & 0xF0) == 0xE0) {
            code_point = lead & 0x0F;
            length = 3;
        }
        else if ((lead & 0xF8) == 0xF0) {
            code_point = lead & 0x07;
            length = 4;
        }
        else {
            // A continuation byte where a lead belongs, or a 5/6-byte lead that
            // UTF-8 has forbidden since 1993.
            ++index;
            continue;
        }
        if (index + length > text.size()) {
            ++index;
            continue;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            auto const continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                length = 0;
                break;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (length == 0 || code_point < minimum[length - 1] || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            ++index;
            continue;
        }
        result.push_back(code_point);
        index += length;
    }
    return result;
}

} // namespace lc1::text
