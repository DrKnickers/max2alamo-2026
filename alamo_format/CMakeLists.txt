add_library(alamo_format STATIC
    src/chunk_writer.cpp
    src/chunk_reader.cpp
    src/alo_writer.cpp
    src/alo_reader.cpp
    src/ala_writer.cpp
    src/ala_reader.cpp
    src/alo_build.cpp
    src/shader_table.cpp
    src/skin_weights.cpp
    src/anim_clip_list.cpp
    src/legacy_clip_scan.cpp
    src/shadow_volume_check.cpp
)

target_include_directories(alamo_format PUBLIC include)
target_compile_features(alamo_format PUBLIC cxx_std_17)

add_library(alamo_format::alamo_format ALIAS alamo_format)
