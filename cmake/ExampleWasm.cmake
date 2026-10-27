# Browser-only setup for the combined example runner.
file(GLOB_RECURSE example_shaders CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/*/settings.h"
)
set(example_shell "${CMAKE_CURRENT_SOURCE_DIR}/examples/wasm/examples.html")
set(example_viewer_files
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/wasm/viewer.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/wasm/viewer.js"
)
foreach(asset IN LISTS example_viewer_files)
    get_filename_component(filename "${asset}" NAME)
    file(GENERATE OUTPUT "${SLANG_RHI_BINARY_DIR}/${filename}" INPUT "${asset}")
endforeach()

function(configure_wasm_example name)
    set_target_properties(${name} PROPERTIES SUFFIX ".html")
    target_link_options(${name} PRIVATE
        "-sUSE_GLFW=3"
        "--use-port=emdawnwebgpu"
        "SHELL:--shell-file \"${example_shell}\""
    )
    foreach(shader IN LISTS example_shaders)
        file(RELATIVE_PATH path "${CMAKE_CURRENT_SOURCE_DIR}" "${shader}")
        target_link_options(${name} PRIVATE "SHELL:--preload-file \"${shader}@/${path}\"")
    endforeach()
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${example_shaders} "${example_shell}")
endfunction()

# A site is the runner's HTML, runtime files, and the two viewer assets.
set(example_site_dir "${SLANG_RHI_BINARY_DIR}/site")
add_custom_target(example-site
    COMMAND ${CMAKE_COMMAND} -E make_directory "${example_site_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:example-runner>" "${example_site_dir}/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:example-runner>/example-runner.js"
        "$<TARGET_FILE_DIR:example-runner>/example-runner.wasm"
        "$<TARGET_FILE_DIR:example-runner>/example-runner.data"
        ${example_viewer_files} "${example_site_dir}"
    DEPENDS example-runner
    COMMENT "Packaging the browser examples"
    VERBATIM
)
