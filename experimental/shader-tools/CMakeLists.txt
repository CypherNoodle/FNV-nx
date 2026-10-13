cmake_minimum_required(VERSION 3.25)
project(fnv_native_shader_tools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(rexglue 0.10.0 EXACT CONFIG REQUIRED)
set(FNV_HORIZON_SDK "" CACHE PATH "Pinned Horizon SDK source")
if(NOT EXISTS "${FNV_HORIZON_SDK}/thirdparty/glslang/SPIRV/SpvBuilder.h")
    message(FATAL_ERROR "Set FNV_HORIZON_SDK to the pinned Horizon SDK with third-party sources")
endif()
set(_thirdparty "${FNV_HORIZON_SDK}/thirdparty")
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${_thirdparty}/glslang" glslang)
set(_shader "${FNV_HORIZON_SDK}/src/graphics/pipeline/shader")
add_executable(fnv-shader-translate translate.cpp
    "${_shader}/shader.cpp" "${_shader}/translator.cpp" "${_shader}/translator_disasm.cpp"
    "${_shader}/spirv.cpp" "${_shader}/spirv_builder.cpp" "${_shader}/spirv_translator.cpp"
    "${_shader}/spirv_translator_alu.cpp" "${_shader}/spirv_translator_fetch.cpp"
    "${_shader}/spirv_translator_memexport.cpp" "${_shader}/spirv_translator_rb.cpp"
    "${FNV_HORIZON_SDK}/src/graphics/flags.cpp"
    "${FNV_HORIZON_SDK}/src/graphics/format/ucode.cpp"
    "${FNV_HORIZON_SDK}/src/graphics/registers.cpp"
    "${FNV_HORIZON_SDK}/src/graphics/util/draw.cpp")
target_include_directories(fnv-shader-translate BEFORE PRIVATE "${FNV_HORIZON_SDK}/include")
target_include_directories(fnv-shader-translate PRIVATE "${_thirdparty}/glslang"
    "${_thirdparty}/renderdoc" "${_thirdparty}/vulkan-headers/include")
target_link_libraries(fnv-shader-translate PRIVATE rex::runtime SPIRV)
