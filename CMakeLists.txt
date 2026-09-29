cmake_minimum_required(VERSION 3.20)
project(lc1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clangd reads a compilation database, and .clangd points at this tree's; without this
# the file is only produced when someone remembers to pass -DCMAKE_EXPORT_COMPILE_COMMANDS,
# which is exactly the kind of thing that silently rots. Every build tree gets its own.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
find_package(tinyobjloader CONFIG REQUIRED)

add_subdirectory(shaders)

# lc1_engine: the reusable library. Public headers live under include/lc1/
# and are included as "lc1/..."; src/ holds sources and private headers
# (e.g. vk/checks.hpp) that the app cannot see.
add_library(lc1_engine STATIC
    src/window.cpp
    src/input.cpp
    src/game/continent.cpp
    src/game/continent-prototype.cpp
    src/game/player-controller.cpp
    src/game/map-camera-controller.cpp
    src/game/continent-view.cpp
    src/resource-manager.cpp
    src/vk/common.cpp
    src/vk/loader.cpp
    src/vk/instance.cpp
    src/vk/surface.cpp
    src/vk/device.cpp
    src/vk/swapchain.cpp
    src/vk/swapchain-policy.cpp
    src/vk/shader.cpp
    src/vk/shader-stages.cpp
    src/vk/buffer.cpp
    src/vk/descriptor-set.cpp
    src/vk/gpu-image.cpp
    src/vk/gpu-texture.cpp
    src/vk/gpu-mesh.cpp
    src/vk/pipeline.cpp
    src/vk/renderer.cpp
    src/vk/memory.cpp

    src/stb-image.cpp
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
    tinyobjloader::tinyobjloader
)

target_compile_definitions(lc1_engine PUBLIC
    GLM_FORCE_DEFAULT_ALIGNED_GENTYPES # Makes glm aligns with shader
    GLM_FORCE_DEPTH_ZERO_TO_ONE # Force depth from glm in range of [0, 1], which
                                # is required by Vulkan.
    GLM_ENABLE_EXPERIMENTAL # Introduces glm::gtx
)

# GCC puts the non-inline half of <print> in its own library, libstdc++exp -- including
# the terminal helpers vprint_unicode calls, std::__open_terminal and
# std::__write_to_terminal. Linux needs none of this spelled out because libstdc++.so.6
# exports them already, which is why the native build links without it; MinGW's
# libstdc++.dll.a does not, and the Windows link then fails with
#   undefined reference to `std::__open_terminal(_iobuf*)'
# after every object has compiled cleanly -- a linker error that looks like a missing
# implementation of a standard function and is really a missing -l. std::println is used
# by the library (window.cpp, device.cpp, instance.cpp, swapchain.cpp), not only by the
# app, so the requirement belongs to lc1_engine and travels with it.
if(MINGW)
    target_link_libraries(lc1_engine INTERFACE stdc++exp)
endif()

# app: the game executable. Output name stays lc1 so the Makefile works.
add_executable(lc1 app/main.cpp)
target_link_libraries(lc1 PRIVATE lc1_engine)

# A MinGW link pulls the compiler's own runtime in as DLLs -- libstdc++-6.dll,
# libgcc_s_seh-1.dll and libwinpthread-1.dll -- which are present on the machine that
# compiled it and absent everywhere the exe is actually meant to run. Nothing else in
# the link is affected: conan builds every dependency static already (*:shared=False),
# and the Vulkan loader is dlopen'd at runtime rather than linked. -static covers all
# three at once (-static-libgcc/-static-libstdc++ alone would leave winpthread).
#
# This lives here rather than in profiles/mingw64's tools.build:exelinkflags because
# CMAKE_EXE_LINKER_FLAGS_INIT is read only on a tree's *first* configure: a profile
# change silently does nothing to an existing build tree, and the resulting exe looks
# fine until it is copied to another machine.
if(MINGW)
    target_link_options(lc1 PRIVATE -static)
endif()

# The .spv files are loaded at runtime, so `--target lc1` must build them too.
# Keep the runtime tree relocatable. Copy on each build so edited assets are refreshed.
add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
    VERBATIM
)
add_dependencies(lc1 shaders copy_assets)

include(CTest)
if(BUILD_TESTING)
    add_executable(lc1_world_map_tests tests/world-map.cpp)
    target_link_libraries(lc1_world_map_tests PRIVATE lc1_engine)
    target_compile_options(lc1_world_map_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_executable(lc1_continent_tests tests/continent.cpp)
    target_link_libraries(lc1_continent_tests PRIVATE lc1_engine)
    target_compile_options(lc1_continent_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(lc1_world_map_tests PRIVATE -static)
        target_link_options(lc1_continent_tests PRIVATE -static)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME world_map COMMAND lc1_world_map_tests)
        add_test(NAME continent COMMAND lc1_continent_tests)
    endif()
endif()

# -Wno-missing-field-initializers: Vulkan's universal idiom is
# `VkFoo foo{VK_STRUCTURE_TYPE_FOO};`, which zero-initializes the remaining
# members by aggregate-init rules. That is correct, and clang warns about it
# anyway, thousands of times.
foreach(target lc1_engine lc1)
    target_compile_options(${target}
        PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
