cmake_minimum_required(VERSION 4.5)
project(formlight-renderer LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

add_executable(formlight-renderer src/main.cpp)
target_compile_features(formlight-renderer PRIVATE cxx_std_23)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME cli_help COMMAND formlight-renderer --help)
    set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: formlight-renderer")
endif()
