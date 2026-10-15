# Spike: RaylibBackend on-device test. Mirrors the framework's own wiring
# (gbarecompx/CMakeLists.txt): static raylib via add_subdirectory with
# PLATFORM=Android, backend lib with GBARECOMP_USE_RAYLIB=1, plus the NDK's
# native_app_glue (android_main lives in OUR .so — NativeActivity loads it).
cmake_minimum_required(VERSION 3.22)
project(raylibtest CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Parameterized for the tree — the /tmp original hard-coded machine paths.
set(FW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../../..")
set(RAYLIB_SRC "" CACHE PATH "raylib 6 source tree (pin dbc56a87…)")
if(NOT EXISTS "${RAYLIB_SRC}/CMakeLists.txt")
    message(FATAL_ERROR "Pass -DRAYLIB_SRC=<raylib source tree> (see README)")
endif()

set(PLATFORM "Android" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${RAYLIB_SRC} raylib_build)

add_library(raylibtest SHARED
    raytest.cpp
    ${FW_ROOT}/src/runtime/raylib_backend.cpp
    ${FW_ROOT}/src/runtime/recomp_audio_drc.cpp
    ${FW_ROOT}/src/runtime/color_lut.cpp
    ${FW_ROOT}/src/runtime/touch_input.cpp
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(raylibtest PRIVATE
    ${FW_ROOT}/src/runtime
    ${ANDROID_NDK}/sources/android/native_app_glue)

target_compile_definitions(raylibtest PRIVATE GBARECOMP_USE_RAYLIB=1)

target_link_libraries(raylibtest
    raylib log android EGL GLESv2 OpenSLES m z)
