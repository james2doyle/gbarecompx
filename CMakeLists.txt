cmake_minimum_required(VERSION 3.20)
project(gbarecomp C CXX)

# clang-tidy reads this (tools/check_clang_tidy.sh); harmless to always write.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Large generated corpora are expensive to rebuild in fresh build trees.
# Reuse identical objects automatically when ccache/sccache is available, while
# preserving an explicit launcher selected by the parent project or user.
set(GBARECOMP_COMPILER_CACHE "AUTO" CACHE STRING
    "Compiler cache: AUTO, OFF, or a ccache/sccache executable")
if(GBARECOMP_COMPILER_CACHE STREQUAL "OFF")
    if(GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM AND
       "${CMAKE_C_COMPILER_LAUNCHER}" STREQUAL
           "${GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM}")
        unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
    endif()
    if(GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM AND
       "${CMAKE_CXX_COMPILER_LAUNCHER}" STREQUAL
           "${GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM}")
        unset(CMAKE_CXX_COMPILER_LAUNCHER CACHE)
    endif()
    unset(GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM CACHE)
elseif(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    unset(GBARECOMP_COMPILER_CACHE_PROGRAM CACHE)
    if(GBARECOMP_COMPILER_CACHE STREQUAL "AUTO")
        find_program(GBARECOMP_COMPILER_CACHE_PROGRAM
            NAMES sccache ccache
            HINTS C:/msys64/mingw64/bin)
    elseif(EXISTS "${GBARECOMP_COMPILER_CACHE}")
        set(GBARECOMP_COMPILER_CACHE_PROGRAM
            "${GBARECOMP_COMPILER_CACHE}")
    else()
        find_program(GBARECOMP_COMPILER_CACHE_PROGRAM
            NAMES "${GBARECOMP_COMPILER_CACHE}")
    endif()
    if(GBARECOMP_COMPILER_CACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER
            "${GBARECOMP_COMPILER_CACHE_PROGRAM}" CACHE STRING
            "Launcher for C compilation")
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${GBARECOMP_COMPILER_CACHE_PROGRAM}" CACHE STRING
            "Launcher for C++ compilation")
        set(GBARECOMP_MANAGED_COMPILER_CACHE_PROGRAM
            "${GBARECOMP_COMPILER_CACHE_PROGRAM}" CACHE INTERNAL
            "Compiler launcher selected by gbarecomp" FORCE)
        message(STATUS "gbarecomp: compiler cache enabled: "
            "${GBARECOMP_COMPILER_CACHE_PROGRAM}")
    else()
        message(STATUS "gbarecomp: compiler cache not found")
    endif()
endif()

if(MSVC)
    add_compile_options(/W3 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Release builds bake libstdc++ + libgcc + libwinpthread into the executable
# so the shipped .exe has no DLL dependencies on a vanilla Windows install
# (Phase 4b retired the SDL half; the raylib cross-build is issue #3's R2).
option(GBARECOMP_STATIC_RELEASE
    "Statically link the mingw C++ runtime into release exes"
    OFF)
option(GBARECOMP_SELFHEAL_RECOMPILE_DEFAULT
    "Enable runtime self-heal overlay compilation by default"
    ON)

# First-divergence co-simulation oracle (COSIM_ORACLE.md). When ON, compiles the
# cosim full-state hash + cycle-keyed park/step TCP engine and arms the runtime_tick
# checkpoint hook. Defined PUBLIC on gbarecomp_debug so it propagates through
# gbarecomp_runtime (runtime.cpp / runtime_bus_bridge.cpp) to the game exe. The
# force-interp backend (g_force_interp / GBARECOMP_FORCE_INTERP) is always compiled
# and independent of this option. OFF = byte-identical to a normal build.
option(GBA_COSIM
    "Build the first-divergence co-simulation oracle (state hash + TCP park/step)"
    OFF)

if(GBARECOMP_STATIC_RELEASE)
    # NOTE: -static / -static-libgcc / -static-libstdc++ MUST be set
    # via -DCMAKE_EXE_LINKER_FLAGS at configure time (see package_release.ps1).
    # add_link_options() here would only apply to targets defined in
    # gbarecomp's subdir, not to a parent project that includes us via
    # add_subdirectory() (e.g. MinishCapRecomp). The cache var works
    # across the whole tree.
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Raylib 6 — the window backend. The game project (FFTA) defaults this ON
# (Phase 4c); the framework keeps OFF as its standalone default, which builds
# a stub compile-check that needs no network.
# ─────────────────────────────────────────────────────────────────────────────
option(GBARECOMP_USE_RAYLIB
    "Fetch/link raylib 6 as the windowing/input backend (OFF = stub compile-check)"
    OFF)
option(GBARECOMP_RAYLIB_HEADLESS
    "Build the fetched raylib for the headless memory platform (rlsw +
    PLATFORM_MEMORY, Phase 5 tier 3). Requires a fresh build dir; ignored
    when the parent provides the raylib target."
    OFF)

set(GBARECOMP_RAYLIB_OK FALSE)
if(GBARECOMP_USE_RAYLIB)
    if(TARGET raylib)
        # A parent project already provided the target (the release cross
        # prefix via find_package(raylib CONFIG), Phase 4e): use it as-is --
        # no FetchContent, no network, deterministic.
        message(STATUS "gbarecompx: raylib target provided by the parent "
                       "project (no fetch)")
        if(GBARECOMP_RAYLIB_HEADLESS)
            message(WARNING "gbarecompx: GBARECOMP_RAYLIB_HEADLESS ignored "
                            "(parent-provided raylib target)")
        endif()
    else()
        include(FetchContent)
        # raylib 6.0, pinned by commit rather than tag: tags can be moved, SHAs
        # cannot (matches this repo's submodule-pinning convention).
        #   refs/tags/6.0 = dbc56a87da87d973a9c5baa4e7438a9d20121d28
        set(GBARECOMP_RAYLIB_TAG "6.0"
            CACHE STRING "raylib tag (pinned to the SHA below)")
        set(GBARECOMP_RAYLIB_SHA "dbc56a87da87d973a9c5baa4e7438a9d20121d28"
            CACHE STRING "raylib commit SHA to build against")
        FetchContent_Declare(raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG ${GBARECOMP_RAYLIB_SHA}
            GIT_SHALLOW FALSE)  # a shallow clone cannot resolve an arbitrary SHA
        message(STATUS "gbarecompx: building raylib ${GBARECOMP_RAYLIB_TAG} "
                       "(${GBARECOMP_RAYLIB_SHA})")
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        if(GBARECOMP_RAYLIB_HEADLESS)
            # Phase 5 tier 3: raylib's cmake/LibraryConfigurations maps
            # PLATFORM=Memory -> PLATFORM_MEMORY + software graphics (rlsw).
            # Set before MakeAvailable so the subbuild picks it up; the
            # definitions export PUBLIC on the raylib target, so this TU sees
            # PLATFORM_MEMORY / GRAPHICS_API_OPENGL_SOFTWARE too.
            set(PLATFORM Memory CACHE STRING "raylib platform (headless tier)" FORCE)
            message(STATUS "gbarecompx: raylib headless tier (PLATFORM=Memory)")
        endif()
        FetchContent_MakeAvailable(raylib)
    endif()
    if(TARGET raylib)
        set(GBARECOMP_RAYLIB_OK TRUE)
        message(STATUS "gbarecompx: raylib 6 dependency available")
    else()
        message(WARNING "gbarecompx: raylib 6 dependency unavailable")
    endif()
endif()

# RaylibBackend. Built unconditionally so the seam stays compile-checked (and
# unit-testable) in the stub (raylib OFF) configuration; GBARECOMP_USE_RAYLIB
# is only defined when raylib was actually fetched, and every raylib call site
# is guarded by it. One source, two configurations.
# The audio DRC/RAB implementation, in its own archive so that linking the
# audio code does not drag in host_window.cpp (and the ROM-derived dispatch
# table). See src/runtime/recomp_audio_drc.cpp for why.
# Screen color grading (ColorLut) in its own archive, same reason as the
# audio DRC above: it is pure CPU code with no windowing dependency, and the
# raylib backend needs it without dragging in host_window.cpp. See
# src/runtime/recomp_audio_drc.cpp for the full rationale.
add_library(gbarecomp_color_lut STATIC src/runtime/color_lut.cpp)
target_include_directories(gbarecomp_color_lut PUBLIC src/runtime)

# Touch input hub (TouchHub, gestures, clock). Pure logic with no windowing
# dependency; extracted for the same reason as the two archives below.
add_library(gbarecomp_touch_input STATIC src/runtime/touch_input.cpp)
target_include_directories(gbarecomp_touch_input PUBLIC src/runtime)

# tinyfiledialogs (v3.21.2, zlib, Guillaume Vareille) — native file dialogs
# for the asset picker. Standalone static target; linked into
# gbarecomp_runtime in Phase 2. Step 1.1 is additive: nothing references it
# yet, so this target builds but is not part of any link line.
add_library(gbarecomp_tinyfiledialogs STATIC
    third_party/tinyfiledialogs/tinyfiledialogs.c)
target_include_directories(gbarecomp_tinyfiledialogs PUBLIC
    third_party/tinyfiledialogs)

add_library(gbarecomp_audio_drc STATIC src/runtime/recomp_audio_drc.cpp)

# Phase 3a — the pure menu model (plan §3.4): no backend, no toolkit.
add_library(gbarecomp_menu_model STATIC src/runtime/menu_model.cpp)
target_include_directories(gbarecomp_menu_model PUBLIC src/runtime)
target_include_directories(gbarecomp_audio_drc PUBLIC src/runtime)
find_package(Threads REQUIRED)
target_link_libraries(gbarecomp_audio_drc PUBLIC Threads::Threads)

# Phase 3c — host-side wiring for the generic menu (plan §3.4): build the
# default rows from a backend's settings, apply MenuResult back to the
# backend, suppress game input while open. Backend-neutral (the seam facade).
add_library(gbarecomp_host_menu STATIC src/runtime/host_menu.cpp)
target_include_directories(gbarecomp_host_menu PUBLIC src/runtime)
target_link_libraries(gbarecomp_host_menu PUBLIC gbarecomp_menu_model)

# The legacy menu engine: menu_draw.h over the HostOverlay surface, behind
# the MenuEngine contract. Its own target so the headless contract test can
# link it without raylib (the raylib backend also links it directly).
add_library(gbarecomp_menu_engine_legacy STATIC
    src/runtime/menu_engine_legacy.cpp)
target_include_directories(gbarecomp_menu_engine_legacy PUBLIC src/runtime)
target_link_libraries(gbarecomp_menu_engine_legacy PUBLIC gbarecomp_menu_model)

add_library(gbarecomp_raylib_backend STATIC src/runtime/raylib_backend.cpp
    src/runtime/raylib_api.cpp src/runtime/host_clock.cpp
    # The menu engine contract + engines. make_menu_engine() lives in the
    # legacy TU and compiles its raygui branch under the backend's PUBLIC
    # GBARECOMP_USE_RAYLIB.
    src/runtime/menu_engine_legacy.cpp
    src/runtime/menu_engine_raygui.cpp)
target_include_directories(gbarecomp_raylib_backend PUBLIC src/runtime)
target_link_libraries(gbarecomp_raylib_backend PUBLIC gbarecomp_menu_engine_legacy)
target_link_libraries(gbarecomp_raylib_backend PUBLIC gbarecomp_audio_drc
    gbarecomp_color_lut gbarecomp_touch_input gbarecomp_menu_model)
# Presentation blend primitive (issue #9 strategy #1): the same pure-C
# recomp_frame_blend.c the SDL backend compiles into gbarecomp_runtime, so
# both backends average byte-identical frames. PUBLIC so the integration
# test can adapt (blend assertions vs passthrough) to whether it exists.
# Absent (framework standalone build, no recomp-ui in scope): the flag is
# still recorded, but nothing is ever applied — the SDL-without-RUNTIME_UI
# parity case.
if(GBARECOMP_RUNTIME_UI_ROOT AND
   EXISTS "${GBARECOMP_RUNTIME_UI_ROOT}/src/common/recomp_frame_blend.c")
    target_sources(gbarecomp_raylib_backend PRIVATE
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/common/recomp_frame_blend.c")
    target_include_directories(gbarecomp_raylib_backend PUBLIC
        "${GBARECOMP_RUNTIME_UI_ROOT}/src")
    target_compile_definitions(gbarecomp_raylib_backend
        PUBLIC GBARECOMP_HAVE_FRAME_BLEND=1)
    message(STATUS "gbarecompx: RaylibBackend blends frames CPU-side")
else()
    message(STATUS "gbarecompx: frame-blend primitive absent "
                   "(no recomp-ui in scope); blend flag recorded only")
endif()
if(GBARECOMP_RAYLIB_OK)
    target_compile_definitions(gbarecomp_raylib_backend
        PUBLIC GBARECOMP_USE_RAYLIB=1)
    # The vendored raygui (raygui_impl.cpp + menu_engine_raygui.cpp).
    target_include_directories(gbarecomp_raylib_backend PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor")
    # The single raygui implementation TU. raylib is present here, so the
    # raygui.h include resolves and the #error guard inside cannot fire.
    target_sources(gbarecomp_raylib_backend PRIVATE
        src/runtime/raygui_impl.cpp)
    target_link_libraries(gbarecomp_raylib_backend PUBLIC raylib)
    # raylib bundles stb_image_write (stbi_write_* symbols) and recomp-ui's
    # launcher_debug.c also defines STB_IMAGE_WRITE_IMPLEMENTATION, so the two
    # static archives both emit stbi_write_jpg. They are the same upstream stb
    # implementation, so allowing the duplicate is benign -- but it must be
    # scoped to targets that actually link raylib, not applied globally.
    target_link_options(gbarecomp_raylib_backend
        INTERFACE "LINKER:--allow-multiple-definition")
    # Publish the *effective* result to the parent scope. A parent project
    # cannot see a normal variable set in this subdirectory, and
    # "GBARECOMP_USE_RAYLIB was requested" is not the same claim as "raylib was
    # actually fetched and linked" -- the target gbarecomp_raylib_backend
    # exists either way (stub-only when the fetch fails).
    set(GBARECOMP_RAYLIB_ENABLED ON CACHE INTERNAL
        "RaylibBackend is built against real raylib")
    message(STATUS "gbarecompx: RaylibBackend linked against raylib 6")
else()
    # The effective flag must not survive a flip to OFF in the same build dir:
    # a stale INTERNAL=ON would make parents add raylib-only targets (and the
    # Phase 4c default probe would falsely pass).
    unset(GBARECOMP_RAYLIB_ENABLED CACHE)
    message(STATUS "gbarecompx: RaylibBackend built stub-only "
                   "(enable GBARECOMP_USE_RAYLIB for real calls)")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# armv4t — portable ARM7TDMI decoder, IR, codegen. Reusable for ARM9 later.
# Must NOT depend on src/gba or src/runtime.
# ─────────────────────────────────────────────────────────────────────────────
# Parent game projects can opt into a double-clickable MinGW development
# directory without duplicating the same four-DLL staging recipe. Static
# release targets deliberately skip this because those dependencies are baked
# into the executable.
set(GBARECOMP_MINGW_RUNTIME_BIN "C:/msys64/mingw64/bin"
    CACHE PATH "Directory containing MinGW runtime DLLs for development builds")

function(gbarecomp_stage_mingw_runtime target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR
            "gbarecomp_stage_mingw_runtime: unknown target '${target_name}'")
    endif()
    if(NOT WIN32 OR NOT MINGW OR GBARECOMP_STATIC_RELEASE)
        return()
    endif()

    foreach(runtime_dll IN ITEMS
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll)
        set(runtime_source "${GBARECOMP_MINGW_RUNTIME_BIN}/${runtime_dll}")
        if(EXISTS "${runtime_source}")
            add_custom_command(TARGET "${target_name}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${runtime_source}"
                    "$<TARGET_FILE_DIR:${target_name}>/${runtime_dll}"
                VERBATIM)
        else()
            message(WARNING
                "gbarecomp: development runtime DLL not found: ${runtime_source}")
        endif()
    endforeach()
endfunction()

set(ARM_RECOMP_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/arm-recomp-core")
# arm-recomp-core carries a local patch on top of the pinned upstream commit
# (in-body goto targets; see tools/patches/arm-recomp-core-local.patch).
# `git submodule update` does not re-apply it — fail loudly here instead of
# silently regressing codegen. To fix (run from the repo root):
#   git -C external/arm-recomp-core apply ../../tools/patches/arm-recomp-core-local.patch
file(READ "${ARM_RECOMP_CORE_DIR}/profiles/armv4t_gba/arm_codegen.h"
     _arm_codegen_h)
if(NOT _arm_codegen_h MATCHES "in_body_goto_targets")
    message(FATAL_ERROR
        "arm-recomp-core is missing the local patch (in_body_goto_targets). "
        "Apply it from the repo root:\n"
        "  git -C external/arm-recomp-core apply ../../tools/patches/arm-recomp-core-local.patch")
endif()
include("${ARM_RECOMP_CORE_DIR}/cmake/ArmRecompCore.cmake")
arm_recomp_core_sources(GBA_ARM_RECOMP_CORE_SOURCES armv4t_gba)
arm_recomp_core_include_dirs(GBA_ARM_RECOMP_CORE_INCLUDE_DIRS armv4t_gba)

add_library(gbarecomp_armv4t STATIC
    ${GBA_ARM_RECOMP_CORE_SOURCES}
    src/armv4t/runtime_arm.cpp
    src/armv4t/symbol_lookup.cpp
)
target_include_directories(gbarecomp_armv4t PUBLIC
    src/armv4t
    ${GBA_ARM_RECOMP_CORE_INCLUDE_DIRS})
set_property(TARGET gbarecomp_armv4t PROPERTY POSITION_INDEPENDENT_CODE ON)

# ─────────────────────────────────────────────────────────────────────────────
# recompile_core — function discovery + per-function codegen, as a LIBRARY.
# Extracted from tools/gba_recompile so the SAME emitter serves both the
# offline corpus build (the tool) and the Stage-2 runtime self-healing
# recompiler (a dispatch miss → emit one function → gcc → DLL). Depends only
# on armv4t (decoder/IR/codegen); must NOT depend on src/gba or src/runtime.
# ─────────────────────────────────────────────────────────────────────────────
add_library(gbarecomp_recompile_core STATIC
    src/recompile/function_finder.cpp
    src/recompile/emit_function.cpp
    src/recompile/codegen_shards.cpp
    src/recompile/dispatch_table_entries.cpp
)
target_include_directories(gbarecomp_recompile_core PUBLIC src/recompile)
target_link_libraries(gbarecomp_recompile_core PUBLIC gbarecomp_armv4t)
set_property(TARGET gbarecomp_recompile_core PROPERTY POSITION_INDEPENDENT_CODE ON)

# ─────────────────────────────────────────────────────────────────────────────
# gba — GBA-specific bus, IO, IRQ, DMA, timers, PPU, audio, input, save, BIOS.
# Aggressively GBA-specific. Depends on armv4t for CPU types.
# ─────────────────────────────────────────────────────────────────────────────
add_library(gbarecomp_gba STATIC
    src/gba/gba_memory.cpp
    src/gba/gba_bus.cpp
    src/gba/gba_waitstates.cpp
    src/gba/gba_matrix_memory.cpp
    src/gba/gba_io.cpp
    src/gba/gba_serial_device.cpp
    src/gba/gba_link_hub.cpp
    src/gba/gba_wireless.cpp
    src/gba/gba_simulation_state.cpp
    src/gba/gba_irq.cpp
    src/gba/gba_dma.cpp
    src/gba/gba_timers.cpp
    src/gba/gba_ppu.cpp
    src/gba/gba_audio.cpp
    src/gba/mod_audio.cpp
    src/gba/gba_m4a.cpp
    src/gba/audio_shadow.cpp
    src/gba/mp2k_shadow.cpp
    src/gba/gba_input.cpp
    src/gba/gba_save.cpp
    src/gba/gba_gpio.cpp
    src/gba/gba_gyro.cpp
    src/gba/gba_solar.cpp
    src/gba/gba_rtc.cpp
    src/gba/gba_bios.cpp
    src/gba/gba_scheduler.cpp
    src/gba/gba_rom_header.cpp
    src/gba/sha1.cpp
    src/gba/crc32.cpp
)
target_include_directories(gbarecomp_gba PUBLIC src/gba)
# src/debug for snapshot.h — the save-state SnapshotWriter/Reader are
# header-only, so this is an include-path dependency only (gbarecomp_gba
# does NOT link gbarecomp_debug; that would invert the layering).
target_include_directories(gbarecomp_gba PRIVATE src/debug)
target_link_libraries(gbarecomp_gba PUBLIC gbarecomp_armv4t)
set_property(TARGET gbarecomp_gba PROPERTY POSITION_INDEPENDENT_CODE ON)

# ─────────────────────────────────────────────────────────────────────────────
# heal_gate — P6 differential-gate primitives (snapshot/journal/diff). PURE:
# depends only on gbarecomp_gba (GbaBus + ArmCpuState), no runtime globals, so
# it unit-tests standalone and the runtime gate state machine layers on top.
# ─────────────────────────────────────────────────────────────────────────────
add_library(gbarecomp_heal_gate STATIC src/runtime/heal_gate.cpp)
target_include_directories(gbarecomp_heal_gate PUBLIC src/runtime)
target_link_libraries(gbarecomp_heal_gate PUBLIC gbarecomp_gba)
set_property(TARGET gbarecomp_heal_gate PROPERTY POSITION_INDEPENDENT_CODE ON)

set(GBARECOMP_HOST_STACK_RESERVE_BYTES "16777216" CACHE STRING
    "Host stack reserve for Windows game executables that link gbarecomp_runtime")

function(gbarecomp_compiler_uses_msvc_linker out_var)
    if(MSVC OR
       CMAKE_C_SIMULATE_ID STREQUAL "MSVC" OR
       CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR
       CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR
       CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(gbarecomp_target_link_host_stack target_name scope)
    gbarecomp_compiler_uses_msvc_linker(_gbarecomp_msvc_linker)
    if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR
       CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_link_options(${target_name} ${scope}
            "/STACK:${GBARECOMP_HOST_STACK_RESERVE_BYTES}")
    elseif(CMAKE_EXECUTABLE_SUFFIX STREQUAL ".exe" AND
           _gbarecomp_msvc_linker)
        # MSVC-style Clang/lld-link does not understand GNU --stack. Route
        # through CMake's linker wrapper so the compiler driver forwards the
        # Windows /STACK form to the linker.
        target_link_options(${target_name} ${scope}
            "LINKER:/STACK:${GBARECOMP_HOST_STACK_RESERVE_BYTES}")
    elseif(CMAKE_EXECUTABLE_SUFFIX STREQUAL ".exe" AND
           (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR
            CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        target_link_options(${target_name} ${scope}
            "LINKER:--stack,${GBARECOMP_HOST_STACK_RESERVE_BYTES}")
    endif()
endfunction()

# ─────────────────────────────────────────────────────────────────────────────
# runtime — what game binaries link against. Dispatch, host glue.
# ─────────────────────────────────────────────────────────────────────────────
option(GBARECOMP_ENABLE_MODS
    "Build the GBA mod package loader and trusted-plugin runtime"
    OFF)

add_library(gbarecomp_runtime STATIC
    src/runtime/multiplayer_config.cpp
    src/runtime/multiplayer_session.cpp
    src/runtime/multiplayer_slice.cpp
    src/runtime/runtime.cpp
    src/runtime/save_config.cpp
    src/runtime/bios_hle.cpp
    src/runtime/generated_dispatch.cpp
    src/runtime/runtime_bus_bridge.cpp
    src/runtime/arm_cpu_bridge.cpp
    src/runtime/overlay_emit.cpp
    src/runtime/overlay_loader.cpp
    src/runtime/overlay_compile.cpp
    # Trusted function-entry hooks are always linked: opted-in generated code
    # must work even when the package catalog itself is disabled.
    src/runtime/mod_function_hooks.cpp
    src/runtime/host_platform.cpp
    src/runtime/host_window.cpp
    src/runtime/host_backend_factory.cpp
    # touch_input.cpp lives in gbarecomp_touch_input (linked below).
    src/runtime/mobile_platform.cpp
    # color_lut.cpp is NOT listed here: it lives in gbarecomp_color_lut (linked
    # below) so the raylib backend can use it without host_window.cpp.
    src/runtime/asset_picker.cpp
    src/runtime/launcher.cpp
    src/runtime/launcher_raygui.cpp
    # Production abort stubs for runtime_dispatch_miss +
    # runtime_unimplemented_op. Test binaries that link only
    # gbarecomp_armv4t (not gbarecomp_runtime) supply their own.
    src/runtime/runtime_arm_default_aborts.cpp
    src/runtime/self_heal_cluster.cpp
    # BIOS dispatch/body selection is handled below. Clean checkouts use a
    # tracked empty stub; locally generated BIOS output is selected as a unit.
)
gbarecomp_target_link_host_stack(gbarecomp_runtime INTERFACE)

# Host wiring for the generic menu (plan §3.4 / Phase 3d): runtime.cpp owns
# the model and the trigger policy, so the runtime links it. Pure — no
# backend, no raylib.
target_link_libraries(gbarecomp_runtime PUBLIC gbarecomp_host_menu)

# tinyfiledialogs: native file dialogs for the asset picker (asset_picker.cpp).
# PUBLIC so the include dir propagates to anything linking the runtime.
target_link_libraries(gbarecomp_runtime PUBLIC gbarecomp_tinyfiledialogs)

# The backend factory (in gbarecomp_runtime) selects RaylibBackend when raylib
# was fetched. Linked here, after the target exists -- CMake resolves
# target_link_libraries at configure time.
if(GBARECOMP_RAYLIB_OK)
    target_link_libraries(gbarecomp_runtime PUBLIC gbarecomp_raylib_backend)
    message(STATUS "gbarecompx: runtime backend factory can select raylib")
endif()
if(GBARECOMP_ENABLE_MODS)
    target_sources(gbarecomp_runtime PRIVATE src/runtime/mod_runtime.cpp)
    target_compile_definitions(gbarecomp_runtime PUBLIC
        GBARECOMP_ENABLE_MODS=1)
    # recomp-ui retains its own compile-time gate in addition to requiring a
    # non-null provider. Only an explicitly opted-in game opens that gate.
    set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL
        "Enable recomp-ui Mods view for this GBA mod-enabled game" FORCE)
endif()

# Keep private BIOS-derived output out of a framework checkout when a game
# supplies a build-local directory. The source-tree default preserves the
# existing standalone workflow.
set(GBARECOMP_GENERATED_BIOS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/generated_bios"
    CACHE PATH "Directory containing locally generated BIOS recompilation sources")

# When the user has run `gba_recompile --bios`, the recompiled BIOS
# functions live in src/runtime/generated_bios/bios_recompiled.cpp.
# We can't list it unconditionally because the placeholder build
# doesn't have it. Pull it in if present.
if(EXISTS "${GBARECOMP_GENERATED_BIOS_DIR}/bios_recompiled.cpp")
    target_sources(gbarecomp_runtime PRIVATE
        "${GBARECOMP_GENERATED_BIOS_DIR}/bios_dispatch_table.cpp"
        "${GBARECOMP_GENERATED_BIOS_DIR}/bios_recompiled.cpp")
    target_compile_definitions(gbarecomp_runtime PUBLIC
        GBARECOMP_HAVE_BIOS_RECOMP=1)
    if(NOT WIN32)
        # The recompiled BIOS reset vector is named `_start` after its crt0
        # label. On ELF hosts that collides with the process entry symbol from
        # Scrt1.o, so rename the guest symbol consistently across the
        # generated BIOS translation units. PUBLIC keeps any dependent that
        # includes bios_recompiled.h in agreement. Windows entry symbols
        # differ, so the historical name is preserved there.
        target_compile_definitions(gbarecomp_runtime PUBLIC
            _start=gbarecomp_bios_reset_start)
    endif()
    message(STATUS "gbarecomp: BIOS recompiled output present — linking")
else()
    target_sources(gbarecomp_runtime PRIVATE
        src/runtime/generated_bios/bios_dispatch_stub.cpp)
    message(STATUS "gbarecomp: BIOS recompiled output absent — placeholder dispatch only. "
                   "Run `build/gba_recompile.exe --bios bios/gba_bios.bin` "
                   "to populate.")
endif()
target_include_directories(gbarecomp_runtime PUBLIC src/runtime
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor")
if(GBARECOMP_RUNTIME_UI_ROOT AND
   EXISTS "${GBARECOMP_RUNTIME_UI_ROOT}/src/recomp_runtime_ui.h")
    target_compile_definitions(gbarecomp_runtime PUBLIC GBARECOMP_RUNTIME_UI=1)
    target_include_directories(gbarecomp_runtime PUBLIC
        "${GBARECOMP_RUNTIME_UI_ROOT}/src"
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/common"
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/third_party/imgui"
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/third_party/imgui/backends")
    if(GBARECOMP_ENABLE_MODS AND
       EXISTS "${GBARECOMP_RUNTIME_UI_ROOT}/src/recomp_launcher.h")
        target_compile_definitions(gbarecomp_runtime PRIVATE
            GBARECOMP_MOD_UI=1)
    endif()
    # Shared presentation blend primitive (issue #9 strategy #1): compiled
    # into the runtime so HostWindow blends CPU-side on cached memory. Pure
    # arithmetic, no SDL; has its own upstream test (recomp-frame-blend).
    target_sources(gbarecomp_runtime PRIVATE
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/common/recomp_frame_blend.c"
        # renderer-agnostic in-game overlay + settings surface:
        # runtime.cpp calls recomp_runtime_ui_* under GBARECOMP_RUNTIME_UI;
        # these sources live in recomp-ui for now (Phase 5.8 audits their final
        # home once recomp-ui is removed).
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/common/recomp_runtime_ui.c"
        "${GBARECOMP_RUNTIME_UI_ROOT}/src/common/recomp_runtime_settings.c")
endif()
# Bake the gbarecomp source root so the Stage-2 runtime compiler can find the
# overlay shim headers (src/runtime + src/armv4t) when it invokes g++ or the
# bundled tcc on a healed function. CMAKE_CURRENT_SOURCE_DIR is the gbarecomp
# worktree root even
# when a game project (MinishCapRecomp) pulls us in via add_subdirectory.
target_compile_definitions(gbarecomp_runtime PRIVATE
    GBARECOMP_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GBARECOMP_SELFHEAL_RECOMPILE_DEFAULT=$<BOOL:${GBARECOMP_SELFHEAL_RECOMPILE_DEFAULT}>)
target_link_libraries(gbarecomp_runtime PUBLIC
    gbarecomp_gba gbarecomp_debug gbarecomp_recompile_core
    gbarecomp_heal_gate
    # The window backend calls rab_*; the implementation lives in its own
    # archive rather than inside host_window.cpp.
    gbarecomp_audio_drc
    # Same for the screen color LUT (color_lut.cpp is pure CPU, no windowing).
    gbarecomp_color_lut
    # And the touch hub (pure logic, no windowing).
    gbarecomp_touch_input)
set_property(TARGET gbarecomp_runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
# Android builds have no platform API to ask for the app-private files
# directory; the applicationId is baked in instead (see
# mobile_internal_storage_path()). Empty = unavailable on non-Android.
set(GBARECOMP_ANDROID_PACKAGE "" CACHE STRING
    "Android applicationId for SDL-less raylib builds (internal-storage path)")
if(ANDROID AND GBARECOMP_ANDROID_PACKAGE)
    target_compile_definitions(gbarecomp_runtime PUBLIC
        GBARECOMP_ANDROID_PACKAGE=${GBARECOMP_ANDROID_PACKAGE})
endif()
if(WIN32 OR MSYS)
    # tinyfiledialogs uses GetOpenFileNameA (comdlg32) + Ole32 on Windows;
    # host_platform's FramePacer uses timeBeginPeriod/timeEndPeriod (winmm).
    target_link_libraries(gbarecomp_runtime PUBLIC comdlg32 ole32 winmm)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# debug — TCP server + always-on ring buffers + snapshot. Always built.
# ─────────────────────────────────────────────────────────────────────────────
add_library(gbarecomp_debug STATIC
    src/debug/tcp_debug_server.cpp
    src/debug/trace_ring.cpp
    src/debug/snapshot.cpp
    src/debug/mod_state.cpp
    src/debug/ws_provenance.cpp
    src/debug/ws_sidecar.cpp
    # First-divergence co-sim engine + full-state hash. Empty TUs unless
    # GBA_COSIM is defined (guarded internally), so safe in the normal build.
    src/debug/cosim.cpp
    src/debug/cosim_state.cpp
)
target_include_directories(gbarecomp_debug PUBLIC src/debug)
# ws_provenance.cpp + cosim_state.cpp consult the active bus via runtime_bus_bridge.h.
target_include_directories(gbarecomp_debug PRIVATE src/runtime)
target_link_libraries(gbarecomp_debug PUBLIC gbarecomp_gba)
set_property(TARGET gbarecomp_debug PROPERTY POSITION_INDEPENDENT_CODE ON)
if(WIN32 OR MSYS)
    target_link_libraries(gbarecomp_debug PUBLIC ws2_32)
endif()
# Propagate GBA_COSIM PUBLIC so the cosim files here, the runtime_tick hook in
# gbarecomp_runtime, and runtime.cpp's cosim_init() all compile the oracle in.
if(GBA_COSIM)
    target_compile_definitions(gbarecomp_debug PUBLIC GBA_COSIM)
    message(STATUS "gbarecomp: GBA_COSIM ON — first-divergence oracle compiled in")
endif()

# ─────────────────────────────────────────────────────────────────────────────
# tools — small CLI utilities. Each is a separate executable.
# ─────────────────────────────────────────────────────────────────────────────
function(target_link_gbarecomp_runtime_stack target_name)
    # The four archives are mutually recursive, so a single-pass linker needs an
    # explicit group. The gate is the LINKER, not the platform: this used to test
    # MINGW OR MSYS, which left native Linux -- GNU ld, but neither of those --
    # taking the plain-list branch and failing to link the test executables:
    #
    #   runtime_arm.cpp: undefined reference to `overlay_try_dispatch'
    #   runtime_arm.cpp: undefined reference to `kBiosDispatchTable'
    #
    # Apple's ld64 and MSVC-style linkers resolve archive cycles without help.
    # ld64 and lld-link reject --start-group outright, so they take the plain
    # list. GNU-style linkers, including MinGW Clang's GNU frontend, need the
    # explicit group.
    gbarecomp_compiler_uses_msvc_linker(_gbarecomp_msvc_linker)
    if(APPLE OR _gbarecomp_msvc_linker)
        target_link_libraries(${target_name} PRIVATE
            ${ARGN} gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug)
    else()
        target_link_libraries(${target_name} PRIVATE
            -Wl,--start-group
            ${ARGN} gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug
            -Wl,--end-group)
    endif()
endfunction()

function(add_gbarecomp_runtime_stack_test target_name source_file)
    add_executable(${target_name}
        ${source_file}
        tools/bios_smoke/dispatch_stub.cpp)
    target_include_directories(${target_name} PRIVATE src/debug)
    target_link_gbarecomp_runtime_stack(${target_name})
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

include(cmake/GbaLinkProbe.cmake)

add_executable(gba_scan tools/gba_scan/main.cpp)
target_link_libraries(gba_scan PRIVATE gbarecomp_armv4t gbarecomp_gba)

# A local toml++ checkout remains useful for offline development, but it is
# intentionally ignored.  Fresh clones/worktrees must still be able to build
# gba_recompile, so resolve the pinned upstream dependency into the build tree
# when the local single-header checkout is absent.
find_path(GBARECOMP_TOMLPP_INCLUDE_DIR
    NAMES toml.hpp
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlpp"
    NO_DEFAULT_PATH)
if(NOT GBARECOMP_TOMLPP_INCLUDE_DIR)
    include(FetchContent)
    set(TOMLPLUSPLUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TOMLPLUSPLUS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG 30172438cee64926dc41fdd9c11fb3ba5b2ba9de)
    FetchContent_MakeAvailable(tomlplusplus)
    set(GBARECOMP_TOMLPP_INCLUDE_DIR "${tomlplusplus_SOURCE_DIR}")
    message(STATUS
        "gbarecomp: fetched pinned toml++ v3.4.0 for gba_recompile")
else()
    message(STATUS
        "gbarecomp: using local toml++ at ${GBARECOMP_TOMLPP_INCLUDE_DIR}")
endif()

add_executable(gba_recompile
    tools/gba_recompile/main.cpp
    tools/gba_recompile/config.cpp)
target_include_directories(gba_recompile PRIVATE
    tools/gba_recompile
    src/armv4t
    "${GBARECOMP_TOMLPP_INCLUDE_DIR}")
target_link_libraries(gba_recompile PRIVATE
    gbarecomp_recompile_core gbarecomp_armv4t gbarecomp_gba)

# The decomp symbol importer is tools/symbol_import/import_decomp_symbols.py
# (a standalone Python tool that reads readelf/link-map text and writes the
# TSV + overlay TOML gba_recompile consumes). It replaced a C++ stub that only
# printed "stub", so there is no executable to build here. See
# docs/SYMBOL_OVERLAY.md.

# test_rom_runner — direct-boots a recompiled jsmolka gba-suite test
# ROM and reports r12 at exit. Two targets per ROM (ARM + THUMB):
# each ROM is recompiled at build time via gba_recompile and linked
# into its own runner exe. Output is the test_rom_runner main
# (shared) + the per-ROM generated body.

set(GBA_SUITE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/gba_suite)

# Recompile arm.gba at build time.
set(TEST_ROM_ARM_OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/test_rom_arm_gen)
set(TEST_ROM_ARM_DISPATCH ${TEST_ROM_ARM_OUTDIR}/dispatch_table.cpp)
set(TEST_ROM_ARM_BODIES
    ${TEST_ROM_ARM_OUTDIR}/recompiled_000.cpp
    ${TEST_ROM_ARM_OUTDIR}/recompiled_001.cpp)
set(TEST_ROM_ARM_HEADER   ${TEST_ROM_ARM_OUTDIR}/recompiled.h)
file(MAKE_DIRECTORY ${TEST_ROM_ARM_OUTDIR})

# Test targets run with asserts live. CMAKE_CXX_FLAGS_RELEASE carries -DNDEBUG,
# which silently erases every assert() in a test TU — a "passing" test that
# checks nothing (the vacuous-assert trap; see BRINGUP 2026-10-14). -UNDEBUG is
# appended after the build-type flags, so it wins; production targets (created
# earlier) keep NDEBUG. `remove_definitions(-DNDEBUG)` does NOT work here:
# NDEBUG comes from the flags string, not add_definitions(). New tests should
# still prefer an always-active CHECK macro.
add_compile_options(-UNDEBUG)

if(EXISTS ${GBA_SUITE_ROOT}/arm/arm.gba)
    add_custom_command(
        OUTPUT  ${TEST_ROM_ARM_DISPATCH} ${TEST_ROM_ARM_BODIES}
                ${TEST_ROM_ARM_HEADER}
        COMMAND $<TARGET_FILE:gba_recompile>
                --rom ${GBA_SUITE_ROOT}/arm/arm.gba
                --entry 0x080000C0
                --codegen-shards 2
                --out ${TEST_ROM_ARM_OUTDIR}
        DEPENDS gba_recompile
                ${GBA_SUITE_ROOT}/arm/arm.gba
        COMMENT "Recompiling gba-suite arm.gba"
        VERBATIM
    )

    add_executable(test_rom_arm
        tools/test_rom_runner/main.cpp
        ${TEST_ROM_ARM_DISPATCH}
        ${TEST_ROM_ARM_BODIES}
    )
    target_include_directories(test_rom_arm PRIVATE
        ${TEST_ROM_ARM_OUTDIR}
        src/armv4t)
    target_link_gbarecomp_runtime_stack(test_rom_arm)
    add_test(NAME test_rom_arm COMMAND test_rom_arm
        ${GBA_SUITE_ROOT}/arm/arm.gba)
else()
    message(STATUS "gbarecomp: gba-suite arm.gba not found; "
                   "skipping test_rom_arm target")
endif()

# Recompile thumb.gba at build time.
set(TEST_ROM_THUMB_OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/test_rom_thumb_gen)
set(TEST_ROM_THUMB_DISPATCH ${TEST_ROM_THUMB_OUTDIR}/dispatch_table.cpp)
set(TEST_ROM_THUMB_BODIES
    ${TEST_ROM_THUMB_OUTDIR}/recompiled_000.cpp
    ${TEST_ROM_THUMB_OUTDIR}/recompiled_001.cpp)
set(TEST_ROM_THUMB_HEADER   ${TEST_ROM_THUMB_OUTDIR}/recompiled.h)
file(MAKE_DIRECTORY ${TEST_ROM_THUMB_OUTDIR})

if(EXISTS ${GBA_SUITE_ROOT}/thumb/thumb.gba)
    add_custom_command(
        OUTPUT  ${TEST_ROM_THUMB_DISPATCH} ${TEST_ROM_THUMB_BODIES}
                ${TEST_ROM_THUMB_HEADER}
        COMMAND $<TARGET_FILE:gba_recompile>
                --rom ${GBA_SUITE_ROOT}/thumb/thumb.gba
                --entry 0x080000C0
                --codegen-shards 2
                --out ${TEST_ROM_THUMB_OUTDIR}
        DEPENDS gba_recompile
                ${GBA_SUITE_ROOT}/thumb/thumb.gba
        COMMENT "Recompiling gba-suite thumb.gba"
        VERBATIM
    )

    add_executable(test_rom_thumb
        tools/test_rom_runner/main.cpp
        ${TEST_ROM_THUMB_DISPATCH}
        ${TEST_ROM_THUMB_BODIES}
    )
    target_include_directories(test_rom_thumb PRIVATE
        ${TEST_ROM_THUMB_OUTDIR}
        src/armv4t)
    target_link_gbarecomp_runtime_stack(test_rom_thumb)
    add_test(NAME test_rom_thumb COMMAND test_rom_thumb
        ${GBA_SUITE_ROOT}/thumb/thumb.gba)
else()
    message(STATUS "gbarecomp: gba-suite thumb.gba not found; "
                   "skipping test_rom_thumb target")
endif()

# bios_smoke — Phase 2.1 canary. Loads bios/gba_bios.bin, runs the
# interpreter from PC=0 for N instructions, prints a trace.
add_executable(bios_smoke tools/bios_smoke/main.cpp
                          tools/bios_smoke/dispatch_stub.cpp)
target_link_gbarecomp_runtime_stack(bios_smoke)
if(WIN32 OR MSYS)
    target_link_libraries(bios_smoke PRIVATE ws2_32)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# tests — per-subsystem. Run with ctest.
# ─────────────────────────────────────────────────────────────────────────────
enable_testing()
add_executable(link_tests tests/link/test_main.cpp
    tests/heal_gate/runtime_stubs.cpp src/runtime/multiplayer_config.cpp)
target_include_directories(link_tests PRIVATE src/runtime)
target_link_libraries(link_tests PRIVATE gbarecomp_gba)
add_test(NAME link_tests COMMAND link_tests)

# Portable packaging generation is a Unix-only shell path. Register its smoke
# test when the small validation toolchain is available, while keeping ordinary
# engine builds independent of packaging-only host packages.
if(UNIX)
    find_program(GBARECOMP_BASH_EXECUTABLE bash)
    find_program(GBARECOMP_DESKTOP_FILE_VALIDATE desktop-file-validate)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(GBARECOMP_BASH_EXECUTABLE AND GBARECOMP_DESKTOP_FILE_VALIDATE
            AND Python3_Interpreter_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import yaml"
            RESULT_VARIABLE GBARECOMP_PYYAML_RESULT
            OUTPUT_QUIET ERROR_QUIET)
        if(GBARECOMP_PYYAML_RESULT EQUAL 0)
            add_test(NAME packaging_generation_checks
                COMMAND ${CMAKE_COMMAND} -E env
                    PYTHON=${Python3_EXECUTABLE}
                    ${GBARECOMP_BASH_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/tests/test_generation.sh)
        endif()
    endif()
endif()

if(GBARECOMP_ENABLE_MODS)
    add_executable(mod_runtime_tests
        tests/mod_runtime/test_main.cpp
        # The runtime's foreign-background publication API owns only the
        # pointer lifetime.  Keep this unit target independent of the full
        # PPU by providing its otherwise PPU-owned storage symbol.
        tests/mod_runtime/ppu_provider_stub.cpp
        src/runtime/mod_runtime.cpp
        src/runtime/mod_function_hooks.cpp
        src/runtime/asset_picker.cpp
        src/runtime/host_platform.cpp
        src/gba/mod_audio.cpp
        src/gba/crc32.cpp
        src/gba/sha1.cpp)
    target_include_directories(mod_runtime_tests PRIVATE
        src/runtime src/gba src/armv4t)
    if(WIN32 OR MSYS)
        target_link_libraries(mod_runtime_tests PRIVATE comdlg32 user32 winmm)
    endif()
    add_test(NAME mod_runtime_tests COMMAND mod_runtime_tests)
endif()

# Asset picker dialog-seam test (Step 2.1). Standalone recipe: compiles
# asset_picker.cpp + crc32/sha1 by source, no engine link. RED-first: does not
# compile until asset_picker_seam.h lands (Step 2.2).
add_executable(asset_picker_tests
    tests/runtime/asset_picker_test.cpp
    src/runtime/asset_picker.cpp
    src/runtime/host_platform.cpp
    src/gba/crc32.cpp
    src/gba/sha1.cpp)
target_include_directories(asset_picker_tests PRIVATE
    src/runtime src/gba src/armv4t)
target_link_libraries(asset_picker_tests PRIVATE gbarecomp_tinyfiledialogs)
if(WIN32 OR MSYS)
    target_link_libraries(asset_picker_tests PRIVATE comdlg32 ole32 winmm)
endif()
add_test(NAME asset_picker_tests COMMAND asset_picker_tests)

# Launch-screen state model test (Step 3.1). Standalone recipe: compiles
# asset_picker.cpp + host_platform.cpp + crc32/sha1 by source so the model
# can call the real gbarecomp::resolve_asset. Headless (no raylib).
add_executable(launcher_model_tests
    tests/runtime/test_launch_screen_model.cpp
    src/runtime/asset_picker.cpp
    src/runtime/host_platform.cpp
    src/gba/crc32.cpp
    src/gba/sha1.cpp)
target_include_directories(launcher_model_tests PRIVATE
    src/runtime src/gba src/armv4t)
target_link_libraries(launcher_model_tests PRIVATE gbarecomp_tinyfiledialogs)
if(WIN32 OR MSYS)
    target_link_libraries(launcher_model_tests PRIVATE comdlg32 ole32 winmm)
endif()
add_test(NAME launcher_model_tests COMMAND launcher_model_tests)

# Boot skip-rule classification test (Step 4.1). Pure argv classification.
add_executable(launcher_boot_tests
    tests/runtime/test_boot_classification.cpp
    src/runtime/launcher.cpp)
target_include_directories(launcher_boot_tests PRIVATE src/runtime)
add_test(NAME launcher_boot_tests COMMAND launcher_boot_tests)

# LauncherBackend contract test (moved from FFTA tests/unit).
add_executable(launcher_contract_tests
    tests/runtime/test_launcher_contract.cpp
    src/runtime/launcher.cpp)
target_include_directories(launcher_contract_tests PRIVATE src/runtime)
add_test(NAME launcher_contract_tests COMMAND launcher_contract_tests)

# raygui link guard (moved from FFTA tests/unit): confirms raygui/raygui.h
# compiles and its symbols resolve from gbarecomp_raylib_backend. Headless.
# Only meaningful when raylib was actually fetched (raygui.h includes raylib.h).
if(GBARECOMP_RAYLIB_OK)
add_executable(raygui_links_tests
    tests/runtime/test_raygui_links.cpp)
target_include_directories(raygui_links_tests PRIVATE
    src/runtime src/vendor)
target_link_libraries(raygui_links_tests PRIVATE
    gbarecomp_runtime)
target_compile_options(raygui_links_tests PRIVATE -UNDEBUG)
add_test(NAME raygui_links COMMAND raygui_links_tests)
endif()

add_executable(mod_audio_tests
    tests/mod_audio/test_main.cpp
    src/gba/mod_audio.cpp)
target_include_directories(mod_audio_tests PRIVATE src/gba)
add_test(NAME mod_audio_tests COMMAND mod_audio_tests)

add_executable(mod_state_tests tests/mod_state/test_main.cpp)
target_include_directories(mod_state_tests PRIVATE src/debug)
target_link_libraries(mod_state_tests PRIVATE gbarecomp_debug)
add_test(NAME mod_state_tests COMMAND mod_state_tests)

add_test(NAME runtime_monolith_guard
    COMMAND ${CMAKE_COMMAND}
        -DGBARECOMP_RUNTIME_CMAKE=${CMAKE_CURRENT_BINARY_DIR}/runtime.cmake
        -DGBARECOMP_TEST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/runtime_monolith_guard
        -DGBARECOMP_TEST_BINARY=${CMAKE_CURRENT_BINARY_DIR}/runtime_monolith_guard_test
        -DGBARECOMP_TEST_GENERATOR=${CMAKE_GENERATOR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/assert_runtime_monolith_guard.cmake)

add_executable(decoder_smoke tests/armv4t/decoder_smoke.cpp)
target_link_libraries(decoder_smoke PRIVATE gbarecomp_armv4t)
add_test(NAME decoder_smoke COMMAND decoder_smoke)

# Runtime address->name resolvers (function map + imported data-symbol map).
add_executable(symbol_lookup_smoke tests/armv4t/symbol_lookup_smoke.cpp)
target_link_libraries(symbol_lookup_smoke PRIVATE gbarecomp_armv4t)
add_test(NAME symbol_lookup_smoke COMMAND symbol_lookup_smoke)

# Overlay-config merge contract. Compiles config.cpp directly: it belongs to
# the gba_recompile tool rather than a library, and the test drives its public
# entry points (load_config / load_config_overlay) through real TOML files.
add_executable(config_overlay_smoke
    tests/recompile/config_overlay_smoke.cpp
    tools/gba_recompile/config.cpp)
target_include_directories(config_overlay_smoke PRIVATE
    tools/gba_recompile
    src/armv4t
    "${GBARECOMP_TOMLPP_INCLUDE_DIR}")
target_link_libraries(config_overlay_smoke PRIVATE
    gbarecomp_recompile_core gbarecomp_armv4t gbarecomp_gba)
add_test(NAME config_overlay_smoke COMMAND config_overlay_smoke)

# Decomp symbol importer: synthetic readelf + link-map fixtures through
# tools/symbol_import/import_decomp_symbols.py, compared against goldens.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME symbol_import_golden
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/symbol_import/run_golden.py")
else()
    message(STATUS
        "gbarecomp: no Python3 interpreter; skipping symbol_import_golden")
endif()

add_executable(function_finder_tests tests/recompile/function_finder_test.cpp)
target_link_libraries(function_finder_tests PRIVATE gbarecomp_recompile_core)
add_test(NAME function_finder_tests COMMAND function_finder_tests)

add_executable(link_branch_tests tests/recompile/link_branch_test.cpp)
target_link_libraries(link_branch_tests PRIVATE gbarecomp_recompile_core)
add_test(NAME link_branch_tests COMMAND link_branch_tests)

add_executable(idle_elision_tests tests/recompile/idle_elision_test.cpp)
target_link_libraries(idle_elision_tests PRIVATE gbarecomp_recompile_core)
add_test(NAME idle_elision_tests COMMAND idle_elision_tests)

add_executable(codegen_shards_tests tests/recompile/codegen_shards_test.cpp)
target_link_libraries(codegen_shards_tests PRIVATE gbarecomp_recompile_core)
add_test(NAME codegen_shards_tests COMMAND codegen_shards_tests)

add_executable(recompile_config_tests
    tests/recompile/config_test.cpp
    tools/gba_recompile/config.cpp)
target_include_directories(recompile_config_tests PRIVATE
    tools/gba_recompile src/recompile src/armv4t
    "${GBARECOMP_TOMLPP_INCLUDE_DIR}")
target_link_libraries(recompile_config_tests PRIVATE gbarecomp_gba)
add_test(NAME recompile_config_tests COMMAND recompile_config_tests)

add_executable(audio_drc_tests tests/runtime/audio_drc_test.cpp)
target_include_directories(audio_drc_tests PRIVATE src/runtime)
add_test(NAME audio_drc_tests COMMAND audio_drc_tests)

add_executable(audio_gain_tests tests/runtime/audio_gain_test.cpp)
target_include_directories(audio_gain_tests PRIVATE src/runtime)
add_test(NAME audio_gain_tests COMMAND audio_gain_tests)

add_executable(audio_convert_tests tests/runtime/audio_convert_test.cpp)
target_include_directories(audio_convert_tests PRIVATE src/runtime)
add_test(NAME audio_convert_tests COMMAND audio_convert_tests)

# Enforces the single-instance invariant raylib's userdata-less callback needs.
add_executable(audio_slot_tests tests/runtime/audio_slot_test.cpp)
target_include_directories(audio_slot_tests PRIVATE src/runtime)
add_test(NAME audio_slot_tests COMMAND audio_slot_tests)

# Host-neutral input translation. Pure, so it runs with no display -- which is
# what makes the raylib backend's input path testable in CI.
add_executable(input_translate_tests tests/runtime/input_translate_test.cpp)
target_include_directories(input_translate_tests PRIVATE src/runtime)
add_test(NAME input_translate_tests COMMAND input_translate_tests)

# raylib KeyCode name mapping. Pure, so it runs with no display.
add_executable(raylib_keymap_tests tests/runtime/raylib_keymap_test.cpp)
target_include_directories(raylib_keymap_tests PRIVATE src/runtime)
add_test(NAME raylib_keymap_tests COMMAND raylib_keymap_tests)

# LCD subpixel-grid shader policy (mask periods, levels, env/config
# resolution) + GLSL source tokens. Pure, so it runs with no display.
add_executable(lcd_shader_tests tests/runtime/lcd_shader_test.cpp)
target_include_directories(lcd_shader_tests PRIVATE src/runtime)
add_test(NAME lcd_shader_tests COMMAND lcd_shader_tests)

# MMPX edge smoothing (red-first): the CPU mirror is differential-tested
# against the VENDORED MIT reference (the authors' C99, test-only — never
# linked into the game) plus hand-derived pattern pins and GLSL structure
# pins in both dialects.
add_executable(mmpx_tests tests/runtime/mmpx_test.cpp
    tests/runtime/support/mmpx_reference_c99.c)
set_source_files_properties(tests/runtime/support/mmpx_reference_c99.c
    PROPERTIES COMPILE_OPTIONS "-Wno-everything")
target_include_directories(mmpx_tests PRIVATE src/runtime)
add_test(NAME mmpx_tests COMMAND mmpx_tests)

# Touch pad geometry shared by the SDL and raylib backends. Pure.
add_executable(touch_pad_layout_tests tests/runtime/touch_pad_layout_test.cpp)
target_include_directories(touch_pad_layout_tests PRIVATE src/runtime)
add_test(NAME touch_pad_layout_tests COMMAND touch_pad_layout_tests)

# Touch pad drawing against a recording fake overlay. Pure.
add_executable(touch_pad_draw_tests tests/runtime/touch_pad_draw_test.cpp)
target_include_directories(touch_pad_draw_tests PRIVATE src/runtime)
add_test(NAME touch_pad_draw_tests COMMAND touch_pad_draw_tests)

# Real 65536 -> 48000 push/pull cadence, shared by both backends' audio paths.
add_executable(audio_pull_cadence_tests
    tests/runtime/audio_pull_cadence_test.cpp)
target_include_directories(audio_pull_cadence_tests PRIVATE src/runtime)
add_test(NAME audio_pull_cadence_tests COMMAND audio_pull_cadence_tests)

add_executable(audio_queue_pacing_tests tests/runtime/audio_queue_pacing_test.cpp)
target_include_directories(audio_queue_pacing_tests PRIVATE src/runtime)
add_test(NAME audio_queue_pacing_tests COMMAND audio_queue_pacing_tests)

add_executable(display_clock_tests tests/runtime/display_clock_test.cpp)
target_include_directories(display_clock_tests PRIVATE src/runtime)
add_test(NAME display_clock_tests COMMAND display_clock_tests)

add_executable(interp_pacing_tests tests/runtime/interp_pacing_test.cpp)
target_include_directories(interp_pacing_tests PRIVATE src/runtime)
add_test(NAME interp_pacing_tests COMMAND interp_pacing_tests)

add_executable(haptic_pacing_tests tests/runtime/haptic_pacing_test.cpp)
target_include_directories(haptic_pacing_tests PRIVATE src/runtime)
add_test(NAME haptic_pacing_tests COMMAND haptic_pacing_tests)

add_executable(touch_pad_state_tests tests/runtime/touch_pad_state_test.cpp)
target_include_directories(touch_pad_state_tests PRIVATE src/runtime)
add_test(NAME touch_pad_state_tests COMMAND touch_pad_state_tests)

add_executable(ini_section_tests tests/runtime/ini_section_test.cpp)
target_include_directories(ini_section_tests PRIVATE src/runtime)
add_test(NAME ini_section_tests COMMAND ini_section_tests)

add_executable(window_open_params_tests tests/runtime/window_open_params_test.cpp)
target_include_directories(window_open_params_tests PRIVATE src/runtime)
add_test(NAME window_open_params_tests COMMAND window_open_params_tests)

# Skeleton guard: compiles RaylibBackend and checks the seam is fully
# implemented. Compiles WITHOUT raylib on purpose -- proves the header and the
# interface contract need neither SDL nor raylib on the include path.
# Phase 2 — V2 concern contract through the raylib backend's accessors
# (Video step; Audio/Input/Fx/Overlay follow). Display-free: passes in both
# configs, no window required.
add_executable(raylib_backend_v2_tests tests/runtime/raylib_backend_v2_test.cpp)
target_include_directories(raylib_backend_v2_tests PRIVATE src/runtime)
target_link_libraries(raylib_backend_v2_tests PRIVATE gbarecomp_raylib_backend)
add_test(NAME raylib_backend_v2_tests COMMAND raylib_backend_v2_tests)

# Phase 2 tail — §5.2 item 3 fakes + the display-free slice of the lifecycle
# contract test. Runs the §3.3 contracts against the FakeBackend (always) and
# the real backend (generic subset; a display-less box early-outs on
# Err::Window).
add_executable(backend_contract_tests tests/runtime/backend_contract_test.cpp)
target_include_directories(backend_contract_tests PRIVATE src/runtime tests/runtime)
target_link_libraries(backend_contract_tests PRIVATE gbarecomp_raylib_backend)
add_test(NAME backend_contract_tests COMMAND backend_contract_tests)

# The settings overlay actually renders (menu_render_contract_test.cpp):
# open the real backend, stash an open MenuModel, present a guest frame,
# assert the readback pixels moved — catches a silent no-draw (the
# 2026-10-16 wiring bug class) that contract/engine unit tests cannot see.
# PLUS the composited-screen tier: the X framebuffer is read directly
# (X11 comes with the raylib desktop build) and the open menu must show
# PANEL and TEXT pixels — the "open + suppressed + painted nothing" and
# "shapes without text" device regressions both fail here. Display-gated:
# open() failing with Err::Window prints + passes, and without X11 the
# capture tier degrades to a noted skip (CI's xvfb job runs the full tier).
find_package(X11 QUIET)
add_executable(menu_render_contract_tests
    tests/runtime/menu_render_contract_test.cpp)
target_include_directories(menu_render_contract_tests
    PRIVATE src/runtime tests/runtime)
target_link_libraries(menu_render_contract_tests
    PRIVATE gbarecomp_raylib_backend)
if(X11_FOUND)
    target_compile_definitions(menu_render_contract_tests
        PRIVATE GBARECOMP_MENU_CAPTURE_X11)
    target_link_libraries(menu_render_contract_tests PRIVATE X11::X11)
endif()
add_test(NAME menu_render_contract_tests COMMAND menu_render_contract_tests)

# The Audio seam honors set_enabled(false) (audio_mute_contract_test.cpp):
# a disabled device drops every push() BEFORE any device gating, so the
# mute policy is observable without a window or audio device — the
# set-but-never-read regression (menu toggle + boot persistence were both
# no-ops) fails here. Stub builds skip via the menu().available() probe.
add_executable(audio_mute_contract_tests
    tests/runtime/audio_mute_contract_test.cpp)
target_include_directories(audio_mute_contract_tests
    PRIVATE src/runtime tests/runtime)
target_link_libraries(audio_mute_contract_tests
    PRIVATE gbarecomp_raylib_backend)
add_test(NAME audio_mute_contract_tests COMMAND audio_mute_contract_tests)

# The atomic state-file write (snapshot_write_test.cpp): temp + fsync +
# rename needs only the target's DIRECTORY, so a foreign-owned target (an
# adb-pushed state file, shell-owned r-- — the device incident) no longer
# blocks the app's save. Pure file I/O; runs everywhere (the runtime-stack
# factory is the headless link shape for debug-lib consumers).
add_gbarecomp_runtime_stack_test(snapshot_write_tests
    tests/runtime/snapshot_write_test.cpp)

# The raygui menu engine driven HEADLESS via RAYGUI_STANDALONE
# (raygui_standalone_stub.cpp): raygui's implementation with recording
# primitives. This target compiles the engine TU itself with the
# STANDALONE config (target-scoped definitions — the real backend's
# compile of the same file is untouched); the stub TU is the single
# implementation TU here, mirroring raygui_impl.cpp's role in the real
# build. GBARECOMP_USE_RAYLIB only satisfies the engine's compile guard —
# under STANDALONE raygui.h never includes raylib, so no GL is needed.
add_executable(menu_engine_raygui_tests
    tests/runtime/menu_engine_raygui_test.cpp
    tests/runtime/raygui_standalone_stub.cpp)
target_sources(menu_engine_raygui_tests PRIVATE
    src/runtime/menu_engine_raygui.cpp)
target_compile_definitions(menu_engine_raygui_tests PRIVATE
    GBARECOMP_USE_RAYLIB=1 RAYGUI_STANDALONE RAYGUI_NO_ICONS)
target_include_directories(menu_engine_raygui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime")
target_link_libraries(menu_engine_raygui_tests PRIVATE gbarecomp_menu_model
    gbarecomp_host_menu)
add_test(NAME menu_engine_raygui_tests COMMAND menu_engine_raygui_tests)

# The launch screen's shared visual language (launcher_style.h): palette +
# status-line decision, raylib-free and headless-testable. The backend TU
# (launcher_raygui.cpp) maps the values to Colors; the wiring guard
# (tools/check_launch_screen_wiring.py) source-pins the same constants.
add_executable(launcher_style_tests tests/runtime/launcher_style_test.cpp)
target_include_directories(launcher_style_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")
add_test(NAME launcher_style_tests COMMAND launcher_style_tests)

# The launch screen's skip state (launcher_skip.h): the "Skip this screen
# on launch when ready" contract — wording, default, sidecar codec, and
# the skip decision, raylib-free and headless-testable.
add_executable(launcher_skip_tests tests/runtime/launcher_skip_test.cpp)
target_include_directories(launcher_skip_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")
add_test(NAME launcher_skip_tests COMMAND launcher_skip_tests)

# The launcher's DRAW LOOP, headless: launcher_raygui.cpp runs for real
# against a recording raylib stub (stub_raylib/raylib.h + the stub TU,
# which also carries raygui's implementation, mirroring raygui_impl.cpp's
# role). run() is steppable frame by frame — scripted input, recorded
# draws, no display, no raylib fetch (the stub raylib.h needs none).
add_executable(launcher_raygui_tests
    tests/runtime/launcher_raygui_test.cpp
    tests/runtime/launcher_raylib_stub.cpp
    src/runtime/launcher_raygui.cpp
    src/runtime/asset_picker.cpp
    src/runtime/host_platform.cpp
    src/gba/crc32.cpp
    src/gba/sha1.cpp)
target_include_directories(launcher_raygui_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/stub_raylib"
    src/runtime src/gba src/armv4t src/vendor)
target_compile_definitions(launcher_raygui_tests PRIVATE
    GBARECOMP_USE_RAYLIB=1)
target_link_libraries(launcher_raygui_tests PRIVATE gbarecomp_tinyfiledialogs)
if(WIN32 OR MSYS)
    target_link_libraries(launcher_raygui_tests PRIVATE comdlg32 ole32 winmm)
endif()
add_test(NAME launcher_raygui_tests COMMAND launcher_raygui_tests)

# Phase 4d — the per-interface surface walk (ports the deleted flat dispatch
# tests): every method of every interface is called through its interface type
# against the fakes, and each call must land on the composed implementation.
add_executable(window_backend_dispatch_tests
    tests/runtime/window_backend_dispatch_test.cpp)
target_include_directories(window_backend_dispatch_tests PRIVATE
    src/runtime tests/runtime)
target_link_libraries(window_backend_dispatch_tests PRIVATE
    gbarecomp_raylib_backend)
add_test(NAME window_backend_dispatch_tests
         COMMAND window_backend_dispatch_tests)

# Phase 5.2 — headless tier smoke (red-first: needs the PLATFORM_MEMORY
# plumbing + gamepad guard; GL/stub configs assert the contract and skip).
add_executable(raylib_headless_tests tests/runtime/raylib_headless_test.cpp)
target_include_directories(raylib_headless_tests PRIVATE src/runtime)
target_link_libraries(raylib_headless_tests PRIVATE gbarecomp_raylib_backend)
add_test(NAME raylib_headless_tests COMMAND raylib_headless_tests)

# Phase 5.1 — RaylibApi failure injection (red-first: the fake arrives next;
# this target must fail to compile until support/fake_raylib_api.h lands).
add_executable(raylib_api_fake_tests tests/runtime/raylib_api_fake_test.cpp)
target_include_directories(raylib_api_fake_tests PRIVATE src/runtime tests/runtime)
target_link_libraries(raylib_api_fake_tests PRIVATE gbarecomp_raylib_backend)
add_test(NAME raylib_api_fake_tests COMMAND raylib_api_fake_tests)

# Menu settings persistence round-trip (red-first: menu_settings.h lands
# with the implementation below).
add_executable(menu_settings_tests tests/runtime/menu_settings_test.cpp)
target_include_directories(menu_settings_tests PRIVATE src/runtime tests/runtime)
add_test(NAME menu_settings_tests COMMAND menu_settings_tests)

# Phase 3a — the pure menu model suite (ports recomp-ui's runtime_ui_test
# semantics to the V2 shape).
add_executable(menu_model_tests tests/runtime/menu_model_test.cpp)
target_include_directories(menu_model_tests PRIVATE src/runtime)
target_link_libraries(menu_model_tests PRIVATE gbarecomp_menu_model)
add_test(NAME menu_model_tests COMMAND menu_model_tests)

# Phase 3b — the shared menu presentation over a recording HostOverlay
# (geometry, hit-test, drawing; no window, no raylib).
add_executable(menu_draw_tests tests/runtime/menu_draw_test.cpp)
target_include_directories(menu_draw_tests PRIVATE src/runtime)
target_link_libraries(menu_draw_tests PRIVATE gbarecomp_menu_model)
add_test(NAME menu_draw_tests COMMAND menu_draw_tests)

# The MenuEngine contract (menu_engine.h): a pure renderer — no mutation,
# identical repeat draws, closed+toast behavior — over a recording overlay.
# The legacy engine runs headless; the raygui engine is compile-checked via
# gbarecomp_raylib_backend (its draw needs a raylib frame context).
add_executable(menu_engine_tests tests/runtime/menu_engine_test.cpp)
target_include_directories(menu_engine_tests PRIVATE src/runtime tests/runtime)
target_link_libraries(menu_engine_tests PRIVATE gbarecomp_menu_model
    gbarecomp_menu_engine_legacy gbarecomp_host_menu)
add_test(NAME menu_engine_tests COMMAND menu_engine_tests)

# Phase 3c — the host-loop wiring on the Phase 2-tail fakes: build from the
# backend's settings, navigation + presentation, MenuResult application to
# fake Fx/Audio/Video, the persistence hook, suppression. No display.
add_executable(host_menu_tests tests/runtime/host_menu_test.cpp)
target_include_directories(host_menu_tests PRIVATE src/runtime tests/runtime)
target_link_libraries(host_menu_tests PRIVATE gbarecomp_host_menu)
add_test(NAME host_menu_tests COMMAND host_menu_tests)

# Phase 3d — the engine long-press cue (ring collector + drawing) over a
# recording HostOverlay. Pure; no window, no raylib.
add_executable(long_press_ring_tests tests/runtime/long_press_ring_test.cpp)
target_include_directories(long_press_ring_tests PRIVATE src/runtime)
add_test(NAME long_press_ring_tests COMMAND long_press_ring_tests)

# NOTE: the backend-selection and closed-window HostWindow tests deliberately
# live in the APP build (root CMakeLists: ffta_backend_select_tests,
# ffta_host_window_tests), not here: this standalone framework configuration
# cannot compile host_window.cpp at all (GBARECOMP_RUNTIME_UI off, already
# broken on the pinned submodule). Both app configurations build them, so
# host_using_raylib_backend() is exercised where it returns true AND false.
# The closed-window HostWindow contract test deliberately lives in the APP
# build (root CMakeLists, `ffta_host_window_tests`), not here: this standalone
# framework configuration does not compile host_window.cpp at all
# (GBARECOMP_RUNTIME_UI is off, and it is already broken that way on the pinned
# submodule -- 24 errors there before any raylib work). Linking it from the app
# build is the only place it can actually run.

add_executable(gamepad_map_tests tests/runtime/gamepad_map_test.cpp)
target_include_directories(gamepad_map_tests PRIVATE src/runtime)
add_test(NAME gamepad_map_tests COMMAND gamepad_map_tests)

add_executable(android_gamepad_tests tests/runtime/android_gamepad_test.cpp)
target_include_directories(android_gamepad_tests PRIVATE src/runtime)
add_test(NAME android_gamepad_tests COMMAND android_gamepad_tests)

add_executable(assist_pad_binding_tests tests/runtime/assist_pad_binding_test.cpp)
target_include_directories(assist_pad_binding_tests PRIVATE src/runtime)
add_test(NAME assist_pad_binding_tests COMMAND assist_pad_binding_tests)

add_executable(backend_v2_types_tests tests/runtime/backend_v2_types_test.cpp)
target_include_directories(backend_v2_types_tests PRIVATE src/runtime)
add_test(NAME backend_v2_types_tests COMMAND backend_v2_types_tests)


add_executable(value_clamps_tests tests/runtime/value_clamps_test.cpp)
target_include_directories(value_clamps_tests PRIVATE src/runtime)
add_test(NAME value_clamps_tests COMMAND value_clamps_tests)

add_executable(safe_insets_tests tests/runtime/safe_insets_test.cpp)
target_include_directories(safe_insets_tests PRIVATE src/runtime)
add_test(NAME safe_insets_tests COMMAND safe_insets_tests)

add_executable(color_settings_tests
    tests/runtime/color_settings_test.cpp
    src/runtime/color_lut.cpp)
target_include_directories(color_settings_tests PRIVATE src/runtime)
add_test(NAME color_settings_tests COMMAND color_settings_tests)

add_executable(color_lut_tests
    tests/runtime/color_lut_test.cpp
    src/runtime/color_lut.cpp)
target_include_directories(color_lut_tests PRIVATE src/runtime)
add_test(NAME color_lut_tests COMMAND color_lut_tests)

add_executable(diagnostic_capture_tests tests/runtime/diagnostic_capture_test.cpp)
target_include_directories(diagnostic_capture_tests PRIVATE src/gba src/runtime src/armv4t)
target_link_libraries(diagnostic_capture_tests PRIVATE gbarecomp_gba)
add_test(NAME diagnostic_capture_tests_disabled COMMAND diagnostic_capture_tests --disabled)
add_test(NAME diagnostic_capture_tests_enabled COMMAND diagnostic_capture_tests --enabled)

# Self-heal proposal clustering — pure logic, links only the standalone
# clustering TU (no runtime/bus/overlay dependencies).
add_executable(selfheal_cluster_test
    tests/selfheal/cluster_test.cpp
    src/runtime/self_heal_cluster.cpp)
target_include_directories(selfheal_cluster_test PRIVATE src/runtime)
add_test(NAME selfheal_cluster_test COMMAND selfheal_cluster_test)

add_executable(interpreter_smoke tests/armv4t/interpreter_smoke.cpp)
target_link_libraries(interpreter_smoke PRIVATE gbarecomp_armv4t)
add_test(NAME interpreter_smoke COMMAND interpreter_smoke)

add_executable(armv4t_tests tests/armv4t/test_main.cpp)
target_link_libraries(armv4t_tests PRIVATE gbarecomp_armv4t)
add_test(NAME armv4t_tests COMMAND armv4t_tests)

add_executable(thumb_tests tests/thumb/test_main.cpp)
target_link_libraries(thumb_tests PRIVATE gbarecomp_armv4t)
add_test(NAME thumb_tests COMMAND thumb_tests)

add_gbarecomp_runtime_stack_test(bus_tests tests/bus/test_main.cpp)
add_gbarecomp_runtime_stack_test(dma_tests tests/dma/test_main.cpp)
add_gbarecomp_runtime_stack_test(timer_tests tests/timers/test_main.cpp)

add_executable(gpio_tests tests/gpio/test_main.cpp)
target_link_libraries(gpio_tests PRIVATE gbarecomp_gba)
add_test(NAME gpio_tests COMMAND gpio_tests)

add_executable(rtc_tests tests/rtc/test_main.cpp)
target_link_libraries(rtc_tests PRIVATE gbarecomp_gba)
add_test(NAME rtc_tests COMMAND rtc_tests)

add_executable(irq_tests tests/irq/test_main.cpp)
target_link_libraries(irq_tests PRIVATE gbarecomp_gba)
add_test(NAME irq_tests COMMAND irq_tests)

add_executable(ppu_smoke_tests tests/ppu_smoke/test_main.cpp)
target_include_directories(ppu_smoke_tests PRIVATE src/runtime src/debug)
target_link_libraries(ppu_smoke_tests PRIVATE gbarecomp_gba)
gbarecomp_target_link_host_stack(ppu_smoke_tests PRIVATE)
add_test(NAME ppu_smoke_tests COMMAND ppu_smoke_tests)

add_executable(save_config_tests
    tests/runtime/save_config_test.cpp
    src/runtime/save_config.cpp)
target_include_directories(save_config_tests PRIVATE src/runtime)
target_link_libraries(save_config_tests PRIVATE gbarecomp_gba)
add_test(NAME save_config_tests COMMAND save_config_tests)

add_executable(presentation_layout_tests tests/presentation_layout/test_main.cpp)
target_include_directories(presentation_layout_tests PRIVATE src/runtime)
add_test(NAME presentation_layout_tests COMMAND presentation_layout_tests)

# Touch input: gesture recognizer, hub rings/scripts, key synthesis, JSON.
add_gbarecomp_runtime_stack_test(touch_input_tests tests/touch_input/test_main.cpp)
add_gbarecomp_runtime_stack_test(multiplayer_session_tests tests/link/session_test.cpp)
add_gbarecomp_runtime_stack_test(wireless_tests tests/link/wireless_test.cpp)
add_subdirectory(tests/link)
add_subdirectory(tests/timing)
include(cmake/GbaNetplay.cmake)

# P6 differential-gate primitives (offline; full-RAM snapshot/restore/diff).
add_executable(heal_gate_tests
    tests/heal_gate/heal_gate_test.cpp
    tests/heal_gate/runtime_stubs.cpp)
target_link_libraries(heal_gate_tests PRIVATE gbarecomp_heal_gate)
add_test(NAME heal_gate_tests COMMAND heal_gate_tests)

# ─────────────────────────────────────────────────────────────────────
# codegen_tests — L1 per-IrOp diff harness.
#
# Architecture:
#   1. tests/codegen/test_cases.cpp lists per-IrOp test cases.
#   2. gen_codegen_tests.exe reads that list, invokes the same
#      ArmCodegen used by tools/gba_recompile, and emits one C
#      function per case into ${CMAKE_BINARY_DIR}/codegen_tests_generated/
#      test_funcs.cpp.
#   3. codegen_tests.exe links test_main.cpp + test_cases.cpp +
#      stubs.cpp + the generated test_funcs.cpp against
#      gbarecomp_armv4t. It runs both the interpreter and the
#      generated function per case and diffs the resulting state.
#
# Per PRINCIPLES.md "Interpreter is informative, never load-bearing":
# the interpreter is the diff-oracle, never a runtime fallback. Any
# divergence is a real codegen bug to fix in arm_codegen.cpp.
# ─────────────────────────────────────────────────────────────────────
add_executable(gen_codegen_tests
    tests/codegen/gen_codegen_tests.cpp
    tests/codegen/test_cases.cpp
)
target_include_directories(gen_codegen_tests PRIVATE
    tests/codegen
    src/armv4t)
target_link_libraries(gen_codegen_tests PRIVATE gbarecomp_armv4t)

# NOTE: must not collide with the codegen_tests TARGET name. On Windows the
# executable gains a .exe suffix so a same-named directory is harmless, but on
# macOS/Linux the linker output path and this directory are the same string and
# the link fails with "Is a directory" (errno 21).
set(CODEGEN_TESTS_OUTDIR
    ${CMAKE_CURRENT_BINARY_DIR}/codegen_tests_generated)
set(CODEGEN_TESTS_GENFILE
    ${CODEGEN_TESTS_OUTDIR}/test_funcs.cpp)

file(MAKE_DIRECTORY ${CODEGEN_TESTS_OUTDIR})

add_custom_command(
    OUTPUT  ${CODEGEN_TESTS_GENFILE}
    COMMAND gen_codegen_tests ${CODEGEN_TESTS_GENFILE}
    DEPENDS gen_codegen_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/test_cases.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/codegen/test_cases.h
    COMMENT "Emitting L1 codegen test functions"
    VERBATIM
)

add_executable(codegen_tests
    tests/codegen/test_main.cpp
    tests/codegen/test_cases.cpp
    tests/codegen/overlay_runtime_compile_smoke.cpp
    tests/codegen/stubs.cpp
    src/gba/gba_waitstates.cpp
    ${CODEGEN_TESTS_GENFILE}
)
target_include_directories(codegen_tests PRIVATE
    tests/codegen
    src/runtime
    src/armv4t
    src/gba)
target_link_libraries(codegen_tests PRIVATE gbarecomp_armv4t)
add_test(NAME codegen_tests COMMAND codegen_tests)

# ─────────────────────────────────────────────────────────────────────────────
# selfheal_stage2_bios_identical — Stage-2 GFP1 byte-identity gate. Proves a
# healed-to-native BIOS function (0xBA4 exclude fixture) executes instruction-
# for-instruction identically to the fully-static corpus (static == bridge ==
# heal_cold == heal_warm). Heavy + needs the gitignored ROM/BIOS + a built
# MinishCapRecomp, so it is OPT-IN: pass -DGBARECOMP_SELFHEAL_MC_DIR=<MC dir>.
# Skips (ctest "Skipped") if the assets are absent.
# ─────────────────────────────────────────────────────────────────────────────
if(GBARECOMP_SELFHEAL_MC_DIR)
    find_program(BASH_EXE bash HINTS C:/msys64/usr/bin /usr/bin)
    if(BASH_EXE)
        add_test(NAME selfheal_stage2_bios_identical
            COMMAND ${BASH_EXE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/selfheal/stage2_bios_identical.sh)
        set_tests_properties(selfheal_stage2_bios_identical PROPERTIES
            ENVIRONMENT "GBARECOMP_ROOT=${CMAKE_CURRENT_SOURCE_DIR};MC_DIR=${GBARECOMP_SELFHEAL_MC_DIR}"
            SKIP_RETURN_CODE 77
            TIMEOUT 2400)
        # selfheal_coverage_sweep — drives the built MC from every savestate with
        # the self-heal recompiler on and asserts FULLY_STATIC for each scene
        # (the doctrine's "done"). Surfaces remaining finder gaps; doubles as a
        # coverage regression guard. Same opt-in / SKIP-77 contract.
        add_test(NAME selfheal_coverage_sweep
            COMMAND ${BASH_EXE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/selfheal/coverage_sweep.sh)
        set_tests_properties(selfheal_coverage_sweep PROPERTIES
            ENVIRONMENT "GBARECOMP_ROOT=${CMAKE_CURRENT_SOURCE_DIR};MC_DIR=${GBARECOMP_SELFHEAL_MC_DIR}"
            SKIP_RETURN_CODE 77
            TIMEOUT 2400)
    else()
        message(STATUS "gbarecomp: bash not found; skipping "
                       "selfheal_stage2_bios_identical + selfheal_coverage_sweep")
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Oracle binary — opt-in (-DGBARECOMP_BUILD_ORACLE=ON). Embeds libmgba
# (MPL-2.0). Per PRINCIPLES.md the native build has zero copyleft deps;
# the oracle binary is the ONLY place libmgba may be linked, and it
# runs as a SEPARATE PROCESS over TCP.
# ─────────────────────────────────────────────────────────────────────────────
option(GBARECOMP_BUILD_ORACLE
       "Build the mGBA-backed oracle (requires third_party/mgba/build/libmgba.a)"
       OFF)
if(GBARECOMP_BUILD_ORACLE)
    add_subdirectory(oracle)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# runtime.cmake — game projects (e.g. MinishCapRecomp) include this to add
# a runnable target that links generated C against the runtime libs.
# ─────────────────────────────────────────────────────────────────────────────
configure_file(
    cmake/runtime.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/runtime.cmake
    @ONLY
)
