cmake_minimum_required(VERSION 3.20)
project(max2alamo_2026
    VERSION 0.0.1
    DESCRIPTION "Modern 3ds Max 2026 exporter for the Alamo (.alo/.ala) format"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
endif()

include(CTest)

add_subdirectory(alamo_format)
add_subdirectory(tools/alo_dump)
add_subdirectory(tools/alo_roundtrip)
add_subdirectory(tools/alo_synth)

# The Max plugin only configures when the SDK is found locally. The Max SDK
# is not redistributable, so CI cannot build it; see docs/build.md for the
# local setup.
add_subdirectory(max2alamo)

if(BUILD_TESTING)
    add_subdirectory(alamo_format/tests)
endif()
