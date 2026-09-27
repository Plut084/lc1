cmake_minimum_required(VERSION 3.20)
project(lc1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third-party)

# All dependencies but VMA-Hpp (see third-party/) come from conan, so the build is identical on Linux and
# Windows. GLFW must NOT be built with a static Vulkan loader -- see the comment
# at the bottom of conanfile.py for why nothing here links a loader at all.
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(stb CONFIG REQUIRED)
FetchContent_MakeAvailable(vmahpp)

add_subdirectory(shaders)

# lc1_engine: the reusable library. Public headers live under include/lc1/
# and are included as "lc1/..."; src/ holds sources and private headers
# (e.g. vk/checks.hpp) that the app cannot see.
add_library(lc1_engine STATIC
    src/window.cpp
    src/vk/util.cpp
    src/vk/loader.cpp
    src/vk/instance.cpp
    src/vk/device.cpp
    src/vk/swapchain.cpp
    src/vk/shader.cpp
    src/vk/shader-stages.cpp
    src/vk/buffer.cpp
    src/vk/mesh.cpp
    src/vk/pipeline.cpp
    src/vk/frame_loop.cpp
    src/vk/renderer.cpp
    src/vk/memory.cpp

    src/game-clock.cpp
)

target_include_directories(lc1_engine
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(lc1_engine PUBLIC
    glfw
    vulkan-headers::vulkan-headers
    glm::glm
    VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp # VMA-Hpp
    spdlog::spdlog
    stb::stb
)

target_compile_definitions(lc1_engine PUBLIC
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES # Makes glm aligns with shader
)

# app: the game executable. Output name stays lc1 so run.sh / Makefile work.
add_executable(lc1 app/main.cpp)
target_link_libraries(lc1 PRIVATE lc1_engine)
# The .spv files are loaded at runtime, so `--target lc1` must build them too.
add_dependencies(lc1 shaders)

# -Wno-missing-field-initializers: Vulkan's universal idiom is
# `VkFoo foo{VK_STRUCTURE_TYPE_FOO};`, which zero-initializes the remaining
# members by aggregate-init rules. That is correct, and clang warns about it
# anyway, thousands of times.
foreach(target lc1_engine lc1)
    target_compile_options(${target}
        PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
