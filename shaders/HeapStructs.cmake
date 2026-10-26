# Generate a Slang include from the deliberately restricted heap schema language.
# The caller must list the output in the consuming shader command's DEPENDS so
# generation precedes the first compile, before Slang can write its depfile.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

function(generate_heap_structs output source)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/generate_heap_structs.py"
                "${source}" -o "${output}"
        DEPENDS "${source}" "${PROJECT_SOURCE_DIR}/tools/generate_heap_structs.py"
        COMMENT "Generating heap structs from ${source}"
        VERBATIM
    )
endfunction()

# Register any schema as a standalone generation target.
function(add_heap_struct_target target output source)
    generate_heap_structs("${output}" "${source}")
    add_custom_target(${target} DEPENDS "${output}")
endfunction()
