cmake_minimum_required(VERSION 3.25)
project(fnv_deko_prototype LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(_root "${CMAKE_CURRENT_LIST_DIR}/../..")
if(NOT REXGLUE_PLATFORM_SWITCH)
    message(FATAL_ERROR "Use the project's Switch toolchain; this library uses native deko3d.")
endif()
add_library(fnv_deko_prototype STATIC
    "${_root}/src/deko/constant_repack.cpp"
    "${_root}/src/deko/shader_module.cpp"
    "${_root}/src/deko/draw_recorder.cpp")
target_include_directories(fnv_deko_prototype PUBLIC "${_root}/src/deko" "${DEVKITPRO}/libnx/include")
target_link_directories(fnv_deko_prototype PUBLIC "${DEVKITPRO}/libnx/lib")
target_link_libraries(fnv_deko_prototype PUBLIC deko3d nx)
option(FNV_BUILD_DEKO_REPLAY "Build private-data native mesh replay NRO" ON)
if(FNV_BUILD_DEKO_REPLAY)
    find_program(_uam uam HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
    find_program(_nacp nacptool HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
    find_program(_elf2nro elf2nro HINTS "${DEVKITPRO}/tools/bin" NO_DEFAULT_PATH REQUIRED)
    add_executable(fnv-deko-replay replay.cpp)
    target_link_libraries(fnv-deko-replay PRIVATE fnv_deko_prototype m)
    add_custom_command(TARGET fnv-deko-replay POST_BUILD
        COMMAND "${_uam}" -s frag -o "${CMAKE_CURRENT_BINARY_DIR}/flat.dksh" "${CMAKE_CURRENT_SOURCE_DIR}/flat.frag"
        COMMAND "${_nacp}" --create "FNV deko replay" "CypherNoodle" "0.1.0" "${CMAKE_CURRENT_BINARY_DIR}/replay.nacp"
        COMMAND "${_elf2nro}" "$<TARGET_FILE:fnv-deko-replay>" "${CMAKE_CURRENT_BINARY_DIR}/fnv-deko-replay.nro"
            "--nacp=${CMAKE_CURRENT_BINARY_DIR}/replay.nacp" "--icon=${_root}/icon.jpg"
        VERBATIM)
endif()
