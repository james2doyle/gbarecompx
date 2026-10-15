# Shared native root for gbarecomp Android games (driven by Gradle's
# externalNativeBuild, see gbarecomp-app.gradle). NativeActivity loads the
# game's libmain.so (the manifest's android.app.lib_name = main); there is no
# shell subproject — the game project's CMake pulls raylib through the framework
# (FetchContent, PLATFORM=Android, the same pin as desktop) and the vendored
# native_app_glue supplies android_main.
cmake_minimum_required(VERSION 3.22)
project(GbarecompAndroidApp C CXX)

if(NOT GBA_GAME_ROOT OR NOT EXISTS "${GBA_GAME_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "GBA_GAME_ROOT must name the game repository root")
endif()

# raylib's Android platform (GLES2). Set before the game's CMake adds the
# framework's raylib subproject — the on-device spike proved this wiring.
set(PLATFORM "Android" CACHE STRING "" FORCE)

# One raylib checkout per ABI, shared across native configs. FetchContent's
# default base dir lives inside the per-config .cxx dir, so every distinct
# config (e.g. a different job-pool value) re-cloned raylib — observed live,
# and the release flow would need network whenever its config differs from
# the dev one. ANDROID_ABI is known here (NDK toolchain); the source and the
# subbuild are shared per ABI, and different ABIs stay isolated (a shared
# base cannot hold two ABIs' build trees).
#
# The FetchContent module CACHES its default on the first configure, so a
# "not defined" guard would never fire for existing config dirs. Replace the
# default value only; an explicit -DFETCHCONTENT_BASE_DIR is respected.
if(NOT DEFINED FETCHCONTENT_BASE_DIR
   OR FETCHCONTENT_BASE_DIR STREQUAL "${CMAKE_BINARY_DIR}/_deps")
    set(FETCHCONTENT_BASE_DIR
        "${GBA_GAME_ROOT}/android/.fetchcontent/${ANDROID_ABI}"
        CACHE PATH "Shared FetchContent base (one raylib checkout per ABI)"
        FORCE)
endif()

add_subdirectory("${GBA_GAME_ROOT}" game_build)

if(NOT TARGET main)
    message(FATAL_ERROR "The game project must define an Android target named 'main'")
endif()
