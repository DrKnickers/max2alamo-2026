# max2alamo: the actual 3ds Max plugin (.dle).
#
# This target only configures when MAX_SDK_DIR is provided (or is
# autodetected at one of the standard install paths). The Max SDK is not
# redistributable, so it cannot be fetched in CI -- developers build this
# locally. See docs/build.md for setup steps.

# --- Locate the Max SDK ----------------------------------------------------

set(_max_sdk_default_paths
    "C:/Program Files/Autodesk/3ds Max 2026 SDK/maxsdk"
    "D:/Autodesk/3ds Max 2026 SDK/maxsdk"
)

set(MAX_SDK_DIR "" CACHE PATH "Path to <SDK install>/maxsdk for 3ds Max 2026")

if(NOT MAX_SDK_DIR)
    foreach(p IN LISTS _max_sdk_default_paths)
        if(IS_DIRECTORY "${p}")
            set(MAX_SDK_DIR "${p}" CACHE PATH "Path to <SDK install>/maxsdk for 3ds Max 2026" FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT MAX_SDK_DIR OR NOT IS_DIRECTORY "${MAX_SDK_DIR}/include")
    message(STATUS "max2alamo: 3ds Max 2026 SDK not found; skipping plugin build.")
    message(STATUS "  To build the plugin, set -DMAX_SDK_DIR=<path>/maxsdk and reconfigure.")
    message(STATUS "  See docs/build.md for details.")
    return()
endif()

message(STATUS "max2alamo: building against MAX_SDK_DIR=${MAX_SDK_DIR}")

set(MAX_SDK_INCLUDE "${MAX_SDK_DIR}/include")
set(MAX_SDK_LIB "${MAX_SDK_DIR}/lib/x64/Release")

# --- The plugin ------------------------------------------------------------

add_library(max2alamo SHARED
    src/plugin_entry.cpp
    src/alo_export.cpp
    src/scene_walker.cpp
    resources/max2alamo.def
    resources/max2alamo.rc
)

# Plugins use the .dle extension; Max only loads files with that suffix.
set_target_properties(max2alamo PROPERTIES
    PREFIX ""
    SUFFIX ".dle"
    OUTPUT_NAME "max2alamo"
)

target_include_directories(max2alamo PRIVATE
    "${MAX_SDK_INCLUDE}"
)

target_link_directories(max2alamo PRIVATE
    "${MAX_SDK_LIB}"
)

# Minimum SceneExport surface. Additional Max libs (geom.lib, mnmath.lib,
# IGame.lib, paramblk2.lib) get linked as Phase 4+ pulls in their APIs.
target_link_libraries(max2alamo PRIVATE
    core
    maxutil
    mesh
    geom         # Point3 / Matrix3 helpers used by scene_walker
    IGame        # IGameScene / IGameNode / IGameMesh
)

target_link_libraries(max2alamo PRIVATE alamo_format)

target_compile_definitions(max2alamo PRIVATE
    UNICODE
    _UNICODE
    _CRT_NON_CONFORMING_SWPRINTFS
    NOMINMAX  # suppress Windows.h's min/max macros (they break std::min etc.)
    WIN32_LEAN_AND_MEAN
)

target_compile_features(max2alamo PRIVATE cxx_std_17)
