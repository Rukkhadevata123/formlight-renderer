cmake_minimum_required(VERSION 4.5)
string(RANDOM LENGTH 12 nonce)
set(model "${CMAKE_CURRENT_BINARY_DIR}/invalid-attribute-${nonce}.gltf")

function(check_cli status message)
    execute_process(COMMAND "${RENDERER}" ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REPLACE "\\" "/" err "${err}")
    string(FIND "${out}${err}" "${message}" match)
    if(NOT result STREQUAL status OR match EQUAL -1)
        message(FATAL_ERROR "${ARGN} returned ${result}. Expected '${message}', got '${out}${err}'")
    endif()
endfunction()

# Accessor 0 is a valid triangle. Accessor 1 supplies normals to tangent tests.
# Only mesh 1, primitive 1 uses the attribute under test (accessor 2).
function(check_attribute name type count bytes payload reason)
    set(attributes "\"POSITION\":0, \"${name}\":2")
    set(bounds "")
    if(name STREQUAL "POSITION")
        set(attributes "\"POSITION\":2")
        set(bounds ", \"min\":[0,0,0], \"max\":[1,1,0]")
    elseif(name STREQUAL "TANGENT")
        string(APPEND attributes ", \"NORMAL\":1")
    endif()
    file(CONFIGURE OUTPUT "${model}" CONTENT [=[
{
  "asset": {"version":"2.0"}, "scene":0, "scenes":[{"nodes":[0]}], "nodes":[{"mesh":1}],
  "meshes":[
    {"primitives":[{"attributes":{"POSITION":0}}]},
    {"primitives":[{"attributes":{"POSITION":0}}, {"attributes":{@attributes@}}]}
  ],
  "buffers":[
    {"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAvwAAAL8AAAAAAAAAPwAAAL8AAAAAAAAAAAAAAD8AAAAA"},
    {"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/"},
    {"byteLength":@bytes@,"uri":"data:application/octet-stream;base64,@payload@"}
  ],
  "bufferViews":[
    {"buffer":0,"byteLength":36}, {"buffer":1,"byteLength":36}, {"buffer":2,"byteLength":@bytes@}
  ],
  "accessors":[
    {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},
    {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
    {"bufferView":2,"componentType":5126,"count":@count@,"type":"@type@"@bounds@}
  ]
}
]=] @ONLY)
    set(expected "${model}: Mesh 1 primitive 1: ${name}${reason}")
    check_cli(1 "${expected}" --render "${model}" --pixel 0 0)
    if(name STREQUAL "TANGENT" AND count EQUAL 3)
        # Geometry-only inspection checks counts but does not read tangent values.
        check_cli(0 "Instanced triangles: 2" --inspect "${model}")
    else()
        check_cli(1 "${expected}" --inspect "${model}")
    endif()
endfunction()

set(short " has 2 values for 3 POSITION vertices")
check_attribute(NORMAL VEC3 2 24 "AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/" "${short}")
check_attribute(TANGENT VEC4 2 32 "AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgL8=" "${short}")
check_attribute(COLOR_0 VEC3 2 24 "AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/" "${short}")
check_attribute(TEXCOORD_0 VEC2 2 16 "AAAAAAAAAAAAAIA/AACAPw==" "${short}")
check_attribute(POSITION VEC3 3 36 "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAwH8AAAAA" "[2] contains a non-finite coordinate")
check_attribute(NORMAL VEC3 3 36 "AAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/" "[1] has a zero-length direction")
check_attribute(NORMAL VEC3 3 36 "AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AADAfwAAAAAAAIA/" "[2] contains a non-finite direction")
check_attribute(TANGENT VEC4 3 48 "AACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgL8AAIA/AAAAAAAAAAAAAIC/" "[1] has a zero-length direction")
check_attribute(TANGENT VEC4 3 48 "AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgL8AAIB/AAAAAAAAAAAAAIA/" "[2] contains a non-finite direction")
check_attribute(TANGENT VEC4 3 48 "AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgL8AAIA/AAAAAAAAAAAAAAAA" "[2] has w = 0, expected +1 or -1")
file(REMOVE "${model}")
