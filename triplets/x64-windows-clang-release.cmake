set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)

set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH)
set(ENV{CC} clang-cl.exe)
set(ENV{CXX} clang-cl.exe)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_C_COMPILER=clang-cl.exe"
    "-DCMAKE_CXX_COMPILER=clang-cl.exe"
    # cmcldeps forwards rc.exe's /c65001 option to clang-cl during dependency scanning.
    "-DCMAKE_NINJA_CMCLDEPS_RC=OFF"
)
