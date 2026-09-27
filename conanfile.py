from conan import ConanFile
from conan.tools.cmake import cmake_layout


class Lc1Recipe(ConanFile):
    name = "lc1"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("vulkan-headers/1.4.350.0")
        self.requires("glfw/3.4")
        self.requires("glm/1.0.3")
        self.requires("spdlog/1.17.0")
        self.requires("stb/cci.20240531")

        # Validation layers are a debug-only dependency: nothing needs them in a
        # Release build, and building them from source is expensive.
        #
        # They come from conan rather than a system package because Windows has no
        # system package manager at all -- keeping one dependency story for both
        # platforms is worth the one-time build cost. The recipe exports
        # VK_LAYER_PATH via runenv_info, so Debug runs must source the generated
        # conanrun.sh (see AGENTS.md).
        if self.settings.build_type == "Debug":
            self.requires("vulkan-validationlayers/1.4.350.0")

    def layout(self):
        cmake_layout(self)


# Dependency OPTIONS are deliberately not set here. They live in profiles/linux.
#
# A conanfile's configure() loses to a profile-level option, and the default
# profile sets "*:shared=False" -- so setting wayland/xkbcommon to shared here
# silently had no effect and the graph failed to resolve. Conan says as much:
# "It is recommended to define options values in profiles, not in recipes."
#
# Deliberately NOT a dependency: vulkan-loader.
#
# The system loader is dlopened exactly once, by lc1::VulkanLoader
# (vk::raii::Context's no-argument constructor: libvulkan.so on Linux,
# vulkan-1.dll on Windows), which hands its vkGetInstanceProcAddr to GLFW via
# glfwInitVulkanLoader so that GLFW never loads one itself. A conan
# vulkan-loader could only be a *second* loader: Context{} dlopens by name no
# matter what is linked, and VK_NO_PROTOTYPES leaves nothing that would call
# into the linked copy. This holds on both platforms -- it is not a Linux
# workaround.
#
# This was volk's job until 2026-09-21. volk is no longer a dependency: after
# the vk::raii migration nothing called a bare vk* function, so volk's
# per-level function-pointer tables were never read, and its only remaining
# work was one dlopen plus one vkGetInstanceProcAddr lookup -- which
# vk::raii::Context already does. See the note at the top of include/lc1/vk/common.hpp.
