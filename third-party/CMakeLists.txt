# Download VMA-Hpp release
include(FetchContent)

FetchContent_Declare(
    vmahpp
    URL      https://github.com/YaaZ/VulkanMemoryAllocator-Hpp/releases/download/v3.4.0%2B2/VulkanMemoryAllocator-Hpp-3.4.0.tar.gz
    URL_HASH SHA256=588e4aa20898dedca99e72d1bfa428303d3076102108884b8b6a178195404bc9 # Copy from the asset list on the release page
    SYSTEM
)


function(add_imgui_backends)
    set(one_value_args TARGET)
    set(multi_value_args BACKENDS DEPS)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "${one_value_args}"
                          "${multi_value_args}")
    cmake_path(SET imgui_bindings_dir NORMALIZE
               "${imgui_INCLUDE_DIR}/../res/bindings")

    foreach (backend ${arg_BACKENDS})
        find_file(imgui_backend "imgui_impl_${backend}.h"
            NO_DEFAULT_PATH
            PATHS ${imgui_bindings_dir}
            REQUIRED
            )
        if (backend STREQUAL "metal")
            set(extension "mm")
        else()
            set(extension "cpp")
        endif()
        list(APPEND backend_sources
             "${imgui_bindings_dir}/imgui_impl_${backend}.${extension}")
    endforeach()
    MESSAGE(STATUS "Adding ImGui backends: ${arg_BACKENDS}")
    add_library(imgui_backend "${backend_sources}")
    target_include_directories(imgui_backend PUBLIC ${imgui_bindings_dir})
    target_link_libraries(imgui_backend PUBLIC imgui::imgui ${arg_DEPS})
    if ("vulkan" IN_LIST arg_BACKENDS)
        # imgui_impl_vulkan.cpp calls bare vk* functions, but this project links no
        # Vulkan loader: lc1::VulkanLoader is the process's only dlopen of it (see
        # conanfile.py). This define -- imgui's supported way of using a custom
        # loader -- switches the backend to its own function-pointer table, which
        # the app fills in with ImGui_ImplVulkan_LoadFunctions before
        # ImGui_ImplVulkan_Init.
        target_compile_definitions(imgui_backend PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    endif()
    target_link_libraries(${arg_TARGET} PRIVATE imgui_backend)
endfunction()
