find_program(SLANGC_EXECUTABLE slangc REQUIRED)

# add_slang_shaders(<target> <source>...)
#
# Compiles each .slang source into ${CMAKE_CURRENT_BINARY_DIR}/<stem>.spv and
# adds <target> (built by default) that depends on all of them.
#
# No -entry flags: slangc picks up every function marked [shader("...")], so one
# file can hold any number of stages. -fvk-use-entrypoint-name keeps their names
# (vertMain, fragMain, ...) in the SPIR-V instead of renaming them all "main".
#
# Debug builds get -O0 -g: debug info for RenderDoc etc., and no spirv-opt pass.
#
# slangc writes a depfile, so editing a module that a shader imports also
# triggers a rebuild, not only editing the shader itself.
function(add_slang_shaders target)
    set(outputs)
    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source NORMALIZE)
        cmake_path(GET source STEM name)
        set(spv ${CMAKE_CURRENT_BINARY_DIR}/${name}.spv)

        add_custom_command(
            OUTPUT  ${spv}
            COMMAND ${SLANGC_EXECUTABLE} ${source}
                    -target spirv
                    -profile spirv_1_4
                    -emit-spirv-directly
                    -fvk-use-entrypoint-name
                    "$<$<CONFIG:Debug>:-O0;-g>"
                    -o ${spv}
                    -depfile ${spv}.d
            DEPENDS ${source}
            DEPFILE ${spv}.d
            COMMENT "Compiling ${name}.slang"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
        list(APPEND outputs ${spv})
    endforeach()
    add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()

add_slang_shaders(shaders
    shader.slang
)
