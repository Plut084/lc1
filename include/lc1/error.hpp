#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace lc1 {

// Unrecoverable setup/runtime failure. main() catches this and prints it
// without a stack trace -- these are configuration problems (missing layer, no
// suitable device), not bugs to be debugged from a backtrace.
class Error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(std::string_view what)
{
    throw Error(std::string(what));
}

} // namespace lc1
