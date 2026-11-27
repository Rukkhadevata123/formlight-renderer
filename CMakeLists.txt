cmake_minimum_required(VERSION 4.5)
option(FORMLIGHT_BUILD_VIEWER "Build the SDL3 and Dear ImGui preview window" OFF)
if(FORMLIGHT_BUILD_VIEWER)
    list(APPEND VCPKG_MANIFEST_FEATURES viewer)
endif()
project(formlight-renderer LANGUAGES C CXX)

find_package(fastgltf CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(TBB CONFIG REQUIRED COMPONENTS tbb)
find_package(tomlplusplus CONFIG REQUIRED)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

add_library(formlight_renderer_core STATIC src/raster/clipping.cpp src/ray/bvh.cpp)
target_sources(formlight_renderer_core PUBLIC
    FILE_SET CXX_MODULES FILES
        src/geometry.cppm
        src/sampling.cppm
        src/image.cppm
        src/texture.cppm
        src/camera.cppm
        src/config/scene_config.cppm
        src/triangle.cppm
        src/mesh_instance.cppm
        src/lighting.cppm
        src/lighting/material.cppm
        src/lighting/direct_lighting.cppm
        src/raster/coverage.cppm
        src/raster/shadow_map.cppm
        src/raster/raster.cppm
        src/raster/shader.cppm
        src/ray_query.cppm
)
target_compile_features(formlight_renderer_core PUBLIC cxx_std_23)

add_library(formlight_io STATIC src/io/image_codec.cpp src/config/scene_toml.cpp)
target_sources(formlight_io PUBLIC FILE_SET CXX_MODULES FILES src/io/image_codec.cppm src/config/scene_toml.cppm)
target_sources(formlight_io PRIVATE FILE_SET internal_modules TYPE CXX_MODULES FILES src/io/output_file.cppm)
target_link_libraries(formlight_io PUBLIC formlight_renderer_core PRIVATE tomlplusplus::tomlplusplus)
target_include_directories(formlight_io SYSTEM PRIVATE "${Stb_INCLUDE_DIR}")

add_library(formlight_gltf STATIC src/io/gltf.cpp src/preview/shading.cpp src/preview/compositing.cpp)
target_sources(formlight_gltf PUBLIC FILE_SET CXX_MODULES FILES src/io/gltf.cppm src/path/integrator.cppm src/preview.cppm)
target_sources(formlight_gltf PRIVATE FILE_SET internal_modules TYPE CXX_MODULES FILES
    src/io/gltf/mesh.cppm src/io/gltf/resources.cppm src/io/gltf/tangents.cppm src/io/gltf/texture_transform.cppm)
target_link_libraries(formlight_gltf PUBLIC formlight_renderer_core PRIVATE fastgltf::fastgltf mikktspace::mikktspace formlight_io TBB::tbb)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # fastgltf's accessor templates emit half-float conversions. CMake invokes the MSVC-style linker directly.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --rtlib=compiler-rt --print-libgcc-file-name
        OUTPUT_VARIABLE clang_builtins OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    target_link_libraries(formlight_gltf PRIVATE "${clang_builtins}")
endif()

if(WIN32)
    set(formlight_app_manifest src/windows/utf8.manifest)
endif()
add_executable(formlight-renderer src/main.cpp src/cli/pixel_report.cpp ${formlight_app_manifest})
target_sources(formlight-renderer PRIVATE FILE_SET CXX_MODULES FILES src/cli/render_options.cppm)
target_link_libraries(formlight-renderer PRIVATE formlight_gltf formlight_io)

add_executable(custom_shader examples/custom_shader.cpp ${formlight_app_manifest})
target_link_libraries(custom_shader PRIVATE formlight_renderer_core formlight_io)

if(FORMLIGHT_BUILD_VIEWER)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    add_library(formlight_viewer_session STATIC)
    target_sources(formlight_viewer_session PUBLIC FILE_SET CXX_MODULES FILES
        src/viewer/orbit_camera.cppm src/viewer/session.cppm src/viewer/document.cppm)
    target_link_libraries(formlight_viewer_session PUBLIC formlight_gltf)
    add_executable(formlight-viewer src/viewer/main.cpp ${formlight_app_manifest})
    target_sources(formlight-viewer PRIVATE FILE_SET CXX_MODULES FILES
        src/viewer/appearance.cppm src/viewer/file_dialogs.cppm src/viewer/settings_panel.cppm src/viewer/window.cppm)
    target_link_libraries(formlight-viewer PRIVATE formlight_viewer_session formlight_io SDL3::SDL3 imgui::imgui)
    add_custom_target(formlight_viewer_fonts
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:formlight-viewer>/fonts"
        COMMENT "Copying viewer fonts" VERBATIM)
    add_dependencies(formlight-viewer formlight_viewer_fonts)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME cli_help COMMAND formlight-renderer --help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: formlight-renderer")

    add_executable(camera_test tests/camera_test.cpp)
    target_link_libraries(camera_test PRIVATE formlight_renderer_core)
    add_test(NAME pinhole_camera COMMAND camera_test)

    add_executable(triangle_visibility_test tests/triangle_visibility_test.cpp)
    target_link_libraries(triangle_visibility_test PRIVATE formlight_renderer_core)
    add_test(NAME triangle_visibility COMMAND triangle_visibility_test)

    add_executable(custom_shader_test tests/custom_shader_test.cpp)
    target_link_libraries(custom_shader_test PRIVATE formlight_renderer_core)
    add_test(NAME custom_shader_clipping COMMAND custom_shader_test)

    add_executable(gltf_geometry_test tests/gltf_geometry_test.cpp)
    target_link_libraries(gltf_geometry_test PRIVATE formlight_gltf)
    add_test(NAME gltf_box_geometry COMMAND gltf_geometry_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/BoxTextured.glb")

    add_executable(box_penumbra_test tests/box_penumbra_test.cpp)
    target_link_libraries(box_penumbra_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME box_penumbra COMMAND box_penumbra_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/lighting/box-area-light.toml")

    add_executable(triangle_modes_test tests/gltf/triangle_modes_test.cpp)
    target_link_libraries(triangle_modes_test PRIVATE formlight_gltf)
    add_test(NAME gltf_triangle_modes COMMAND triangle_modes_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/triangle-modes.gltf")

    add_executable(nested_instances_test tests/gltf/nested_instances_test.cpp)
    target_link_libraries(nested_instances_test PRIVATE formlight_gltf)
    add_test(NAME gltf_nested_instances COMMAND nested_instances_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/nested-instances.gltf")

    add_executable(generated_normals_test tests/gltf/generated_normals_test.cpp)
    target_link_libraries(generated_normals_test PRIVATE formlight_gltf)
    add_test(NAME gltf_generated_normals COMMAND generated_normals_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/folded-normals.gltf")

    add_test(NAME gltf_invalid_index COMMAND "${CMAKE_COMMAND}"
        "-DRENDERER=$<TARGET_FILE:formlight-renderer>" "-DMODEL=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-index.gltf"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/gltf/invalid_index.cmake")

    add_test(NAME gltf_invalid_attributes COMMAND "${CMAKE_COMMAND}"
        "-DRENDERER=$<TARGET_FILE:formlight-renderer>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/gltf/invalid_attributes.cmake")

    add_executable(metallic_roughness_texture_test tests/metallic_roughness_texture_test.cpp)
    target_link_libraries(metallic_roughness_texture_test PRIVATE formlight_gltf)
    add_test(NAME metallic_roughness_texture COMMAND metallic_roughness_texture_test
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/MetallicRoughnessPanel.glb")

    add_executable(texture_coordinates_test tests/texture_coordinates_test.cpp)
    target_link_libraries(texture_coordinates_test PRIVATE formlight_gltf)
    add_test(NAME texture_coordinates COMMAND texture_coordinates_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/DualUvPanel.glb")

    add_executable(texture_transform_test tests/gltf/texture_transform_test.cpp)
    target_link_libraries(texture_transform_test PRIVATE formlight_gltf)
    add_test(NAME gltf_texture_transform COMMAND texture_transform_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/texture-transform.gltf")

    add_test(NAME external_gltf COMMAND "${CMAKE_COMMAND}"
        "-DRENDERER=$<TARGET_FILE:formlight-renderer>" "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/assets"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/gltf/external_gltf.cmake")

    add_executable(texture_minification_test tests/texture_minification_test.cpp)
    target_link_libraries(texture_minification_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME texture_minification COMMAND texture_minification_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/mip-checker.toml")

    add_executable(normal_mapping_test tests/normal_mapping_test.cpp)
    target_link_libraries(normal_mapping_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME normal_mapping COMMAND normal_mapping_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/normal-panels.toml")

    add_executable(mask_visibility_test tests/mask_visibility_test.cpp)
    target_link_libraries(mask_visibility_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME mask_visibility COMMAND mask_visibility_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/mask-panel.toml")

    add_executable(material_sidedness_test tests/material_sidedness_test.cpp)
    target_link_libraries(material_sidedness_test PRIVATE formlight_gltf)
    add_test(NAME material_sidedness COMMAND material_sidedness_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sided-panels.gltf")

    add_executable(shadow_sidedness_test tests/shadow_sidedness_test.cpp)
    target_link_libraries(shadow_sidedness_test PRIVATE formlight_gltf)
    add_test(NAME shadow_sidedness COMMAND shadow_sidedness_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sided-panels.gltf")

    add_executable(shadow_pcf_test tests/shadow_pcf_test.cpp)
    target_link_libraries(shadow_pcf_test PRIVATE formlight_gltf)
    add_test(NAME shadow_pcf COMMAND shadow_pcf_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sided-panels.gltf")

    add_executable(blend_layers_test tests/blend_layers_test.cpp)
    target_link_libraries(blend_layers_test PRIVATE formlight_gltf)
    add_test(NAME blend_layers COMMAND blend_layers_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/BlendLayers.glb")

    add_executable(ssaa_test tests/ssaa_test.cpp)
    target_link_libraries(ssaa_test PRIVATE formlight_gltf)
    add_test(NAME ssaa_edges COMMAND ssaa_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sided-panels.gltf"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/EmissivePanels.glb")

    add_executable(grayscale_alpha_test tests/gltf/grayscale_alpha_test.cpp)
    target_link_libraries(grayscale_alpha_test PRIVATE formlight_gltf)
    add_test(NAME gltf_grayscale_alpha COMMAND grayscale_alpha_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/grayscale-alpha.gltf")

    add_executable(emissive_panels_test tests/emissive_panels_test.cpp)
    target_link_libraries(emissive_panels_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME emissive_panels COMMAND emissive_panels_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/emissive-panels.toml")

    add_executable(path_first_hit_test tests/path_first_hit_test.cpp)
    target_link_libraries(path_first_hit_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME path_first_hit COMMAND path_first_hit_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/emissive-panels.toml")

    add_executable(path_direct_light_test tests/path_direct_light_test.cpp)
    target_link_libraries(path_direct_light_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME path_direct_light COMMAND path_direct_light_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/lighting/box-area-light.toml")

    add_executable(cornell_diffuse_test tests/cornell_diffuse_test.cpp)
    target_link_libraries(cornell_diffuse_test PRIVATE formlight_gltf formlight_io)
    add_test(NAME cornell_diffuse COMMAND cornell_diffuse_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/path/cornell-diffuse.toml")

    add_executable(scene_config_test tests/scene_config_test.cpp)
    target_link_libraries(scene_config_test PRIVATE formlight_io formlight_gltf TBB::tbb)
    add_test(NAME scene_config_round_trip COMMAND scene_config_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/lighting/box-area-light.toml")
    add_test(NAME external_scene_round_trip COMMAND scene_config_test "${CMAKE_CURRENT_SOURCE_DIR}/scenes/materials/dual-uv-panel.toml")

    if(FORMLIGHT_BUILD_VIEWER)
        add_executable(viewer_navigation_test tests/viewer_navigation_test.cpp)
        target_link_libraries(viewer_navigation_test PRIVATE formlight_viewer_session)
        add_test(NAME viewer_navigation COMMAND viewer_navigation_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/BoxTextured.glb"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/DualUvPanel/DualUvPanel.gltf")
        set_tests_properties(viewer_navigation PROPERTIES TIMEOUT 15)
    endif()
endif()
