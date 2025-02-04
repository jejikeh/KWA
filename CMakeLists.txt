cmake_minimum_required(VERSION 3.20)
project(KWA)

set(CMAKE_CXX_STANDARD 17)
set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT "render_example")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")
set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DKW_DEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNICODE -D_UNICODE")

find_program(
    vulkan_dxc_executable
    NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

function(target_add_shaders target shaders)
    set(vertex_shaders ${shaders})
    list(FILTER vertex_shaders INCLUDE REGEX ".+_vertex\\.hlsl$")

    foreach(absolute_path_hlsl ${vertex_shaders})
        cmake_path(RELATIVE_PATH absolute_path_hlsl BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path_hlsl)
        cmake_path(REPLACE_EXTENSION relative_path_hlsl ".cso" OUTPUT_VARIABLE relative_path_cso)
        cmake_path(REPLACE_EXTENSION relative_path_hlsl ".spv" OUTPUT_VARIABLE relative_path_spv)
        cmake_path(REMOVE_FILENAME relative_path_hlsl OUTPUT_VARIABLE relative_path_dir)
        
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_cso}" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_spv}"
            MAIN_DEPENDENCY ${absolute_path_hlsl}
            COMMAND ${CMAKE_COMMAND} -E make_directory "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_dir}\""
            COMMAND ${vulkan_dxc_executable} -T vs_6_0 -Fo "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_cso}\"" "$<$<CONFIG:Debug>:-Od;-Zi;-O0;-Qembed_debug>" -Wno-ignored-attributes "\"${absolute_path_hlsl}\""
            COMMAND ${vulkan_dxc_executable} -T vs_6_0 -Fo "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_spv}\"" "$<$<CONFIG:Debug>:-Od;-Zi;-O0;-Qembed_debug;-fspv-debug=line>" -fspv-reflect -fvk-invert-y -fvk-use-dx-layout -spirv "\"${absolute_path_hlsl}\""
            COMMAND_EXPAND_LISTS
        )
    endforeach()
    
    set(fragment_shaders ${shaders})
    list(FILTER fragment_shaders INCLUDE REGEX ".+_fragment\\.hlsl$")

    foreach(absolute_path_hlsl ${fragment_shaders})
        cmake_path(RELATIVE_PATH absolute_path_hlsl BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path_hlsl)
        cmake_path(REPLACE_EXTENSION relative_path_hlsl ".cso" OUTPUT_VARIABLE relative_path_cso)
        cmake_path(REPLACE_EXTENSION relative_path_hlsl ".spv" OUTPUT_VARIABLE relative_path_spv)
        cmake_path(REMOVE_FILENAME relative_path_hlsl OUTPUT_VARIABLE relative_path_dir)

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_cso}" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_spv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_dir}\""
            COMMAND ${vulkan_dxc_executable} -T ps_6_0 -Fo "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_cso}\"" "$<$<CONFIG:Debug>:-Od;-Zi;-O0;-Qembed_debug>" -Wno-ignored-attributes "\"${absolute_path_hlsl}\""
            COMMAND ${vulkan_dxc_executable} -T ps_6_0 -Fo "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_spv}\"" "$<$<CONFIG:Debug>:-Od;-Zi;-O0;-Qembed_debug;-fspv-debug=line>" -fspv-reflect -fvk-use-dx-position-w -fvk-use-dx-layout -spirv "\"${absolute_path_hlsl}\""
            MAIN_DEPENDENCY ${absolute_path_hlsl}
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    target_sources(${target} PRIVATE ${shaders})

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/resource"
        PREFIX "Resource Files"
        FILES ${shaders}
    )

    set_source_files_properties(${shaders} PROPERTIES VS_TOOL_OVERRIDE "None")
endfunction()

function(target_add_textures target textures)
    foreach(absolute_path_dds ${textures})
        cmake_path(RELATIVE_PATH absolute_path_dds BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path_dds)
        cmake_path(REPLACE_EXTENSION relative_path_dds LAST_ONLY "kwt" OUTPUT_VARIABLE relative_path_kwt)

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_kwt}"
            COMMAND $<TARGET_FILE:texture_converter> "\"${absolute_path_dds}\"" "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_kwt}\""
            MAIN_DEPENDENCY ${absolute_path_dds}
            #DEPENDS texture_converter
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    target_sources(${target} PRIVATE ${textures})

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/resource"
        PREFIX "Resource Files"
        FILES ${textures}
    )
endfunction()

function(target_add_geometries target geometries)
    foreach(absolute_path_glb ${geometries})
        cmake_path(RELATIVE_PATH absolute_path_glb BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path_glb)
        cmake_path(REPLACE_EXTENSION relative_path_glb LAST_ONLY "kwg" OUTPUT_VARIABLE relative_path_kwg)

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_kwg}"
            COMMAND $<TARGET_FILE:geometry_converter> "\"${absolute_path_glb}\"" "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path_kwg}\""
            MAIN_DEPENDENCY ${absolute_path_glb}
            #DEPENDS geometry_converter
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    target_sources(${target} PRIVATE ${geometries})

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/resource"
        PREFIX "Resource Files"
        FILES ${geometries}
    )
endfunction()

function(target_add_materials target materials)
    foreach(absolute_path ${materials})
        cmake_path(RELATIVE_PATH absolute_path BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path)

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path}"
            COMMAND ${CMAKE_COMMAND} -E copy "\"${absolute_path}\"" "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path}\""
            MAIN_DEPENDENCY ${absolute_path}
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    target_sources(${target} PRIVATE ${materials})

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/resource"
        PREFIX "Resource Files"
        FILES ${materials}
    )
endfunction()

function(target_add_height_fields target height_fields)
    foreach(absolute_path ${height_fields})
        cmake_path(RELATIVE_PATH absolute_path BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE relative_path)

        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path}"
            COMMAND ${CMAKE_COMMAND} -E copy "\"${absolute_path}\"" "\"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${relative_path}\""
            MAIN_DEPENDENCY ${absolute_path}
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    target_sources(${target} PRIVATE ${height_fields})

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/resource"
        PREFIX "Resource Files"
        FILES ${height_fields}
    )
endfunction()

function(target_add_resources target)
    file(GLOB_RECURSE shaders "resource/*.hlsl")
    target_add_shaders(${target} "${shaders}")

    file(GLOB_RECURSE textures "resource/*.dds")
    target_add_textures(${target} "${textures}")

    file(GLOB_RECURSE geometries "resource/*.glb")
    target_add_geometries(${target} "${geometries}")

    file(GLOB_RECURSE materials "resource/*.kwm")
    target_add_materials(${target} "${materials}")

    file(GLOB_RECURSE height_fields "resource/*.tsv")
    target_add_height_fields(${target} "${height_fields}")
endfunction()

add_subdirectory("thirdparty")
add_subdirectory("engine")
add_subdirectory("tools")
add_subdirectory("examples")
