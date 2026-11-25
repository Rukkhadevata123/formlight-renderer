cmake_minimum_required(VERSION 4.5)
project(formlight-renderer LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

add_library(formlight_renderer_core STATIC)
target_sources(formlight_renderer_core PUBLIC
    FILE_SET CXX_MODULES FILES
        src/geometry.cppm
        src/camera.cppm
        src/triangle.cppm
        src/raster.cppm
        src/ray_query.cppm
)
target_compile_features(formlight_renderer_core PUBLIC cxx_std_23)

add_executable(formlight-renderer src/main.cpp)
target_link_libraries(formlight-renderer PRIVATE formlight_renderer_core)

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
endif()
