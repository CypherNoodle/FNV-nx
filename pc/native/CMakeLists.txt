cmake_minimum_required(VERSION 3.20)
project(fnv_native_pe C)
add_library(fnv_native_pe STATIC pe_image.c)
target_include_directories(fnv_native_pe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fnv_native_pe PUBLIC c_std_11)
target_compile_options(fnv_native_pe PRIVATE -Wall -Wextra -Werror)
# This is a data loader library, not a game launcher or an NRO target.
