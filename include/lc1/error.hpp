#pragma once

#include <format>
#include <stdexcept>
#include <utility>

namespace lc1 {

// Unrecoverable setup/runtime failure. main() catches this and prints it
// without a stack trace -- these are configuration problems (missing layer, no
// suitable device), not bugs to be debugged from a backtrace.
class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Formats like std::println. The format string is checked at compile time, so a
// message built at runtime cannot be the format; pass it as an argument:
// fail("{}", message).
template <typename... Args> [[noreturn]] void fail(std::format_string<Args...> fmt, Args &&...args)
{
    throw Error(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace lc1
