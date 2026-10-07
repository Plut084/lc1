cmake_minimum_required(VERSION 3.30)
project(lc1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake warns for `FetchContent` and asks for this.
set(DOWNLOAD_EXTRACT_TIMESTAMP ON)

# clangd reads a compilation database, and .clangd points at this tree's; without this
# the file is only produced when someone remembers to pass -DCMAKE_EXPORT_COMPILE_COMMANDS,
# which is exactly the kind of thing that silently rots. Every build tree gets its own.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
include(CheckCXXSourceCompiles)

# clang-cl reports compiler ID Clang but accepts the MSVC command-line dialect.
# Probe the chosen flags rather than assuming every compiler in a family supports them.
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(lc1_warning_candidates /W4)
else()
    set(lc1_warning_candidates -Wall -Wextra -Wpedantic)
endif()
set(lc1_warning_options)
foreach(flag IN LISTS lc1_warning_candidates)
    string(MAKE_C_IDENTIFIER "${flag}" flag_id)
    check_cxx_compiler_flag("${flag}" lc1_supports_${flag_id})
    if(lc1_supports_${flag_id})
        list(APPEND lc1_warning_options "${flag}")
    endif()
endforeach()

function(lc1_enable_warnings target)
    target_compile_options(${target} PRIVATE ${lc1_warning_options})
endfunction()

# AddressSanitizer, off by default: it costs real runtime and memory, which is the wrong
# default for an ordinary build. Probed rather than assumed -- the MinGW cross compiler
# here has no sanitizer runtime installed at all, so a clear configure-time failure beats
# an unexplained link error.
#
# check_cxx_source_compiles, not check_cxx_compiler_flag: the warning flags above only
# need the compile line, while -fsanitize=address must reach the link line too, and
# check_cxx_compiler_flag passes its flag to the compile line alone. Against clang that
# check fails on its own missing link flag -- "undefined reference to __asan_init" --
# reporting no sanitizer support for a compiler that has it.
option(lc1_enable_asan "Build with AddressSanitizer" OFF)
if(lc1_enable_asan)
    set(CMAKE_REQUIRED_FLAGS -fsanitize=address)
    check_cxx_source_compiles("int main() { return 0; }" lc1_supports_asan)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT lc1_supports_asan)
        message(FATAL_ERROR
            "lc1_enable_asan is ON, but ${CMAKE_CXX_COMPILER} cannot compile and link with -fsanitize=address")
    endif()
    # Global options rather than a per-target function like lc1_enable_warnings: ASan
    # instruments every object that ends up in the executable, the imgui backends and
    # each test target included, so a target list kept here would rot the next time a
    # target is added. The compile option instruments; the link option is what pulls the
    # runtime in, and without it the instrumented objects fail to link. Frame pointers
    # keep ASan's stack traces readable above -O0, where the compiler drops them.
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Static MinGW runtimes are required for standalone Windows executables.
# Fail during configuration if the selected linker cannot provide them.
if(MINGW)
    check_linker_flag(CXX "-static" lc1_supports_static_link)
    if(NOT lc1_supports_static_link)
        message(FATAL_ERROR "The MinGW target requires a linker supporting -static")
    endif()
endif()

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
find_package(TinyGLTF CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_subdirectory(shaders)

# lc1_engine: the reusable library. Public headers live under include/lc1/
# and are included as "lc1/..."; src/ holds sources and private headers
# (e.g. vk/render/vertex-input.hpp) that the app cannot see.
add_library(lc1_engine STATIC
    src/window.cpp
    src/input.cpp
    src/tangent-space.cpp
    src/resource-manager.cpp

    # Vulkan setup and dispatch.
    src/vk/core/common.cpp
    src/vk/core/device.cpp
    src/vk/core/instance.cpp
    src/vk/core/loader.cpp
    src/vk/core/memory.cpp

    # GPU storage, uploads and binding primitives.
    src/vk/resources/acceleration-structure.cpp
    src/vk/resources/descriptor-heap.cpp
    src/vk/resources/descriptor-heap-cache.cpp
    src/vk/resources/gpu-buffer.cpp
    src/vk/resources/gpu-image.cpp
    src/vk/resources/gpu-mesh.cpp
    src/vk/resources/gpu-texture.cpp
    src/vk/resources/image-barrier.cpp
    src/vk/resources/shader-object.cpp

    # Window surfaces, swapchains and presentation policy.
    src/vk/presentation/surface.cpp
    src/vk/presentation/swapchain-policy.cpp
    src/vk/presentation/swapchain.cpp

    # Concrete lighting, shadow and output passes.
    src/vk/render/graphics-shaders.cpp
    src/vk/render/graphics-state.cpp
    src/vk/render/material.cpp
    src/vk/render/ray-query-shadows.cpp
    src/vk/render/renderer.cpp
    src/vk/render/temporal-shadows.cpp
    src/vk/render/tone-mapping.cpp

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
    TinyGLTF::TinyGLTF
    KTX::ktx
)
target_link_libraries(lc1_engine PRIVATE
    mikktspace::mikktspace
)

# Conan's MinGW KTX package installs its ASTC encoder as a separate archive,
# but KTX::ktx omits it from the link interface. Its package lib directory is
# already propagated by that target; keep the companion archive after KTX.
if(MINGW)
    target_link_libraries(lc1_engine PRIVATE astcenc-avx2-static)
endif()

target_compile_definitions(lc1_engine PUBLIC
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

# Game rules, world resources and camera behavior depend on the engine.
# The engine does not link back to the game or application.
add_library(lc1_game STATIC
    src/game/continent.cpp
    src/game/continent-prototype.cpp
    src/game/continent-present.cpp
    src/game/continent-layout.cpp
    src/game/character.cpp
    src/game/player-controller.cpp
    src/game/map-camera-controller.cpp
    src/game/continent-view.cpp
    src/game/game-session.cpp
    src/game/player-view.cpp
    src/game/debug/pbr-showcase.cpp
)
target_link_libraries(lc1_game PUBLIC lc1_engine)

# app: the game executable. Output name stays lc1 so the Makefile works.
add_executable(lc1
    app/main.cpp
    app/application.cpp
)
target_link_libraries(lc1 PRIVATE lc1_game)
add_imgui_backends(TARGET lc1 BACKENDS vulkan glfw DEPS
    vulkan-headers::vulkan-headers glfw)

# A MinGW link pulls the compiler's own runtime in as DLLs -- libstdc++-6.dll,
# libgcc_s_seh-1.dll and libwinpthread-1.dll -- which are present on the machine that
# compiled it and absent everywhere the exe is actually meant to run. Nothing else in
# the link is affected: the Conan dependency graph already uses static libraries here,
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
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME descriptor_heap_shader_interfaces
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/check_heap_shaders.py"
            --sample-shading "${PROJECT_BINARY_DIR}/shaders/shader.spv"
            "${PROJECT_BINARY_DIR}/shaders/shader.spv"
            "${PROJECT_BINARY_DIR}/shaders/visibility.spv"
            "${PROJECT_BINARY_DIR}/shaders/temporal-shadows.spv"
            "${PROJECT_BINARY_DIR}/shaders/spatial-shadows.spv"
            "${PROJECT_BINARY_DIR}/shaders/tone-map.spv")
    add_test(NAME heap_struct_generator
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/test_heap_struct_generator.py"
                --slangc "${SLANGC_EXECUTABLE}")
    add_executable(lc1_world_map_tests tests/world-map.cpp)
    target_link_libraries(lc1_world_map_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_world_map_tests)
    add_executable(lc1_continent_tests tests/continent.cpp)
    target_link_libraries(lc1_continent_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_continent_tests)
    add_executable(lc1_character_tests tests/character.cpp)
    target_link_libraries(lc1_character_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_character_tests)
    add_executable(lc1_tangent_tests tests/tangent-space.cpp)
    target_link_libraries(lc1_tangent_tests PRIVATE lc1_engine)
    lc1_enable_warnings(lc1_tangent_tests)
    add_executable(lc1_player_view_tests tests/player-view.cpp)
    target_link_libraries(lc1_player_view_tests PRIVATE lc1_game)
    lc1_enable_warnings(lc1_player_view_tests)
    if(MINGW)
        target_link_options(lc1_world_map_tests PRIVATE -static)
        target_link_options(lc1_continent_tests PRIVATE -static)
        target_link_options(lc1_character_tests PRIVATE -static)
        target_link_options(lc1_tangent_tests PRIVATE -static)
        target_link_options(lc1_player_view_tests PRIVATE -static)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(lc1_descriptor_heap_tests EXCLUDE_FROM_ALL tests/descriptor-heap.cpp)
        target_link_libraries(lc1_descriptor_heap_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_descriptor_heap_tests)
        add_dependencies(lc1_descriptor_heap_tests descriptor_heap_test_shader)
        add_executable(lc1_descriptor_heap_bulk_tests EXCLUDE_FROM_ALL tests/descriptor-heap-bulk.cpp)
        target_link_libraries(lc1_descriptor_heap_bulk_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_descriptor_heap_bulk_tests)
        add_dependencies(lc1_descriptor_heap_bulk_tests descriptor_heap_bulk_test_shader)
        # Explicit GPU integration target: needs a display/GPU and the Conan run env.
        # Kept out of ordinary CPU-only CTest runs.
        add_executable(lc1_temporal_shadow_tests EXCLUDE_FROM_ALL tests/temporal-shadows.cpp)
        target_link_libraries(lc1_temporal_shadow_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_temporal_shadow_tests)
        add_dependencies(lc1_temporal_shadow_tests shaders)
        add_executable(lc1_shadow_path_tests EXCLUDE_FROM_ALL tests/shadow-paths.cpp)
        target_link_libraries(lc1_shadow_path_tests PRIVATE lc1_engine)
        lc1_enable_warnings(lc1_shadow_path_tests)
        add_dependencies(lc1_shadow_path_tests shaders copy_assets)
        add_executable(lc1_pbr_tests EXCLUDE_FROM_ALL tests/pbr.cpp)
        target_link_libraries(lc1_pbr_tests PRIVATE lc1_game)
        lc1_enable_warnings(lc1_pbr_tests)
        add_dependencies(lc1_pbr_tests shaders)
        add_test(NAME world_map COMMAND lc1_world_map_tests)
        add_test(NAME continent COMMAND lc1_continent_tests)
        add_test(NAME character COMMAND lc1_character_tests)
        add_test(NAME tangent_space COMMAND lc1_tangent_tests)
        add_test(NAME player_view COMMAND lc1_player_view_tests)
    endif()
endif()

# Apply the same checked warning policy to both libraries and the application.
foreach(target lc1_engine lc1_game lc1)
    lc1_enable_warnings(${target})
endforeach()
