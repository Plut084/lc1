#pragma once

#include <string>
#include <string_view>

namespace lc1::text {

// Strict UTF-8 decoding: overlong encodings, UTF-16 surrogates, values above
// U+10FFFF and truncated sequences are all rejected. Recovery drops the single
// offending byte and resumes at the next one, so a malformed string still yields
// the text around it instead of failing. Never reads past the end of `text`.
std::u32string decode_utf8(std::string_view text);

} // namespace lc1::text
