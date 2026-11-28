cmake_minimum_required(VERSION 4.5)
file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}" build_dir)
string(RANDOM LENGTH 16 nonce)
set(scratch "${build_dir}/viewer-display-${nonce}")
if(EXISTS "${scratch}")
    message(FATAL_ERROR "Display test directory already exists: ${scratch}")
endif()
file(MAKE_DIRECTORY "${scratch}")

set(scene [=[
[scene]
preset = "final_scene"
seed = 42

[render]
width = 67
height = 43
ssaa = 2
seed = 7
path_samples = 19
]=])

function(render_references name exposure mapping)
    file(WRITE "${scratch}/${name}.toml"
        "${scene}\n[display]\nexposure_ev = ${exposure}\ntone_mapping = \"${mapping}\"\n")
    foreach(samples IN LISTS ARGN)
        execute_process(COMMAND "${RENDERER}" --scene "${name}.toml" --path-samples "${samples}"
            --output "${name}-${samples}.png" WORKING_DIRECTORY "${scratch}" COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
endfunction()

render_references(clip 0.0 clip 8 19)
render_references(darker -1.0 clip 16 19)
render_references(reinhard 0.0 reinhard 19)
execute_process(COMMAND "${CHECK}" WORKING_DIRECTORY "${scratch}" COMMAND_ERROR_IS_FATAL ANY)
file(REAL_PATH "${scratch}" actual)
if(actual STREQUAL scratch)
    file(REMOVE_RECURSE "${scratch}")
endif()
