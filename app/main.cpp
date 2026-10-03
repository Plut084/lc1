#include "application.hpp"

#include "lc1/error.hpp"

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>

namespace {

// Signal handlers only set a flag; GLFW calls are not async-signal-safe.
std::sig_atomic_t volatile interrupted = 0;

void interrupt_handler(int /*unused*/)
{
    interrupted = 1;
}

} // namespace

int main()
{
    // Validation aborts on errors. Preserve startup logs even when stdout is piped.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    try {
        spdlog::set_pattern("[%^%l%$] %v");
        std::signal(SIGINT, interrupt_handler);
        std::signal(SIGTERM, interrupt_handler);

        lc1::app::ApplicationConfig config;
#ifdef NDEBUG
        config.enable_validation = false;
#endif
        if (std::getenv("LC1_NO_VALIDATION") != nullptr)
            config.enable_validation = false;
        if (auto const *asset_root = std::getenv("LC1_ASSET_ROOT"))
            config.asset_root = asset_root;

        lc1::app::Application application{config};
        application.run([] { return interrupted != 0; });
    }
    catch (lc1::Error const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
        return EXIT_FAILURE;
    }
    // Vulkan exceptions already name the failing call and VkResult.
    catch (std::exception const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
