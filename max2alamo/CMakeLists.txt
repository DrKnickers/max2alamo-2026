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
    src/alamo_utility.cpp
    src/animation_settings_dlg.cpp
    src/alamo_proxy_helper.cpp
    src/legacy_clip_importer.cpp
    resources/max2alamo.def
    resources/max2alamo.rc
    resources/utility_dialogs.rc
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
    bmm          # BitmapInfo::Name (used when reading DXMaterial bitmaps)
    paramblk2    # IUtil / UtilityObj (Alamo Utility command-panel plugin)
    Maxscrpt     # ExecuteMAXScriptScript (Phase 8f propagation button)
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

# --- Install path ----------------------------------------------------------
#
# Drop the built .dle into <repo>/plugin/max2alamo.dle on every build, so
# Max can load it from a user-writable folder (no Program Files elevation
# on each rebuild). The dev workflow is then:
#
#   1. One-time setup: in Max, Customize -> Configure System Paths ->
#      3rd Party Plug-Ins -> Add this directory, then restart Max.
#   2. Every rebuild: just `cmake --build ... --target max2alamo` and
#      restart Max -- no copy step, no UAC prompt.
#
# Implemented as POST_BUILD copy_if_different rather than RUNTIME_OUTPUT_
# DIRECTORY so the MSBuild config subfolder (Release\) doesn't end up in
# the install path. The destination is a stable, deterministic name Max's
# plugin loader can rely on.
set(MAX2ALAMO_INSTALL_DIR "${CMAKE_SOURCE_DIR}/plugin"
    CACHE PATH "Folder the built max2alamo.dle is copied to after each build")

add_custom_command(TARGET max2alamo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MAX2ALAMO_INSTALL_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:max2alamo>"
            "${MAX2ALAMO_INSTALL_DIR}/max2alamo.dle"
    COMMENT "Installing max2alamo.dle to ${MAX2ALAMO_INSTALL_DIR}/"
    VERBATIM
)
