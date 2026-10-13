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
