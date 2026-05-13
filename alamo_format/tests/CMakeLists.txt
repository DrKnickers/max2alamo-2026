include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(alamo_format_tests
    chunk_io_test.cpp
    chunk_reader_test.cpp
    chunk_writer_test.cpp
    chunk_tree_test.cpp
    export_scene_test.cpp
    alo_build_test.cpp
    ala_build_test.cpp
    skin_weights_test.cpp
)
target_link_libraries(alamo_format_tests PRIVATE alamo_format Catch2::Catch2WithMain)
target_compile_features(alamo_format_tests PRIVATE cxx_std_17)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(alamo_format_tests)
