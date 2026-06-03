cmake_minimum_required(VERSION 3.20)
project(monkey_dust_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Lua 5.4: prefer static build (via FetchContent in root CMakeLists, MD_STATIC_DEPS=ON).
# Fall back to system dynamic library when MD_STATIC_DEPS=OFF.
if(NOT MD_STATIC_DEPS)
    # Cross-platform system Lua discovery (fallback only).
    find_library(MD_LUA54_LIB NAMES lua5.4 lua-5.4 QUIET)
    find_path   (MD_LUA54_INC lua5.4/lua.h QUIET)
    if(MD_LUA54_LIB AND MD_LUA54_INC)
        set(LUA_LIBRARIES   "${MD_LUA54_LIB}")
        set(LUA_INCLUDE_DIR "${MD_LUA54_INC}")
        message(STATUS "[monkey_dust] Lua 5.4 (system dynamic): ${MD_LUA54_LIB}")
    else()
        find_package(Lua 5.4 REQUIRED)
        message(STATUS "[monkey_dust] Lua 5.4 (find_package): ${LUA_LIBRARIES}")
    endif()
    # LUA_INCLUDE_DIR already set; LUA_LIBRARIES points to .so
endif()
# When MD_STATIC_DEPS=ON: LUA_LIBRARIES=lua54_static, LUA_INCLUDE_DIR set in root CMakeLists.

file(GLOB_RECURSE ENGINE_SRC src/*.cpp)
# stb_image_impl, glad_impl, cgltf_impl must NOT be in the engine static lib —
# each binary (game, editor, demo) compiles them directly to avoid duplicate symbols
# when the same impl appears in both the engine lib and the binary.
list(FILTER ENGINE_SRC EXCLUDE REGEX "stb_image_impl\\.cpp$")
list(FILTER ENGINE_SRC EXCLUDE REGEX "glad_impl\\.cpp$")
list(FILTER ENGINE_SRC EXCLUDE REGEX "cgltf_impl\\.cpp$")
# MaskedOcclusionCulling files need specific per-file compile flags (see below).
list(FILTER ENGINE_SRC EXCLUDE REGEX "vendor/moc/.*\\.cpp$")
add_library(monkey_dust_engine STATIC ${ENGINE_SRC})

# ── Unity Build — 110 engine .cpp → ~14 batched TU ──────────────────────────
# Clean build: 110 compiles → ~14. Incremental: файл потрапляє в один batch,
# перебудовується тільки той batch (не всі 110).
# BATCH_SIZE=8: збалансовано між compile parallelism та batch size.
# Виключення: MaskedOcclusionCulling (різні ISA flags — вже виключено вище).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    set_target_properties(monkey_dust_engine PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 4
    )
    # Exclude files with ODR conflicts or duplicate static helpers.
    # flare/: item_loader/enemy_loader/power_loader define static ChainOpen().
    # combat/: engine + game both implement PowerManager — unity batch collides with game .o.
    # NOTE: explicit list (not GLOB) to avoid CMake re-configure on every ninja run.
    set(_UNITY_EXCLUDE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/flare/item_loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/flare/enemy_loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/flare/power_loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/flare/map_loader.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/combat/power_manager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/combat/damage_calc.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/combat/hit_zones.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/combat/power_def.cpp"
        # hot/: multiple files define static s_mtime() — each must be its own TU.
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hot/gameplay_module.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/hot/editor_module.cpp"
    )
    # Filter to only existing files (safe if some are absent)
    foreach(_f ${_UNITY_EXCLUDE_FILES})
        if(EXISTS "${_f}")
            set_source_files_properties("${_f}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
        endif()
    endforeach()
    message(STATUS "[monkey_dust] engine Unity Build: ENABLED (batch=4, flare/+combat/ excluded)")
endif()

# ── MaskedOcclusionCulling (Intel, Apache 2.0) ────────────────────────────────
# Main .cpp must be compiled WITHOUT AVX2/SSE4 — it's a dispatcher that
# detects CPU features at runtime and creates the appropriate implementation.
# AVX2 .cpp must be compiled WITH -mavx2 for the AVX2 implementation.
set(MOC_BASE_CPP   src/vendor/moc/MaskedOcclusionCulling.cpp)
set(MOC_AVX2_CPP   src/vendor/moc/MaskedOcclusionCullingAVX2.cpp)
set(MOC_AVX512_CPP src/vendor/moc/MaskedOcclusionCullingAVX512.cpp)
target_sources(monkey_dust_engine PRIVATE
    ${MOC_BASE_CPP} ${MOC_AVX2_CPP} ${MOC_AVX512_CPP})
if(NOT MSVC)
    # GCC 13: always_inline intrinsics require matching ISA flags even in Debug.
    # Base: needs -mxsave (_xgetbv) + -msse4.1 (_mm_mullo_epi32); no AVX.
    set_source_files_properties(${MOC_BASE_CPP} PROPERTIES
        COMPILE_FLAGS "-msse4.1 -mxsave -mno-avx -mno-avx2")
    # AVX2: needs -mfma (FMA instructions used alongside AVX2 in MOC).
    set_source_files_properties(${MOC_AVX2_CPP} PROPERTIES
        COMPILE_FLAGS "-mavx2 -mfma")
    # AVX512 stub: same as base.
    set_source_files_properties(${MOC_AVX512_CPP} PROPERTIES
        COMPILE_FLAGS "-msse4.1 -mxsave -mno-avx -mno-avx2")
else()
    set_source_files_properties(${MOC_BASE_CPP} PROPERTIES
        COMPILE_FLAGS "/arch:SSE2")
    set_source_files_properties(${MOC_AVX2_CPP} PROPERTIES
        COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(${MOC_AVX512_CPP} PROPERTIES
        COMPILE_FLAGS "/arch:SSE2")
endif()
add_library(monkey_dust::engine ALIAS monkey_dust_engine)

target_include_directories(monkey_dust_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/ozz-animation/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Third-party (PUBLIC so consumers also see them)
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/entt/single_include
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/glm
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/Recast/Include
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/Detour/Include
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/DetourCrowd/Include
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/JoltPhysics
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/imgui
    PRIVATE ${LUA_INCLUDE_DIR}
    # Vendor: glad, stb headers — PUBLIC so game/ and tools/ can find them too
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
)

if(NOT USE_SDL3)
    message(FATAL_ERROR
        "monkey_dust requires -DUSE_SDL3=ON. "
        "The Raylib/OpenGL backend has been removed (see CLAUDE.md П.1-П.3).")
endif()

# SDL3 + SDL_GPU is the only rendering backend.
# GLM provides math types; MD_LOG_NO_RAYLIB replaces TraceLog with fprintf.
target_compile_definitions(monkey_dust_engine PUBLIC USE_SDL3 USE_GLM MD_LOG_NO_RAYLIB MD_SDL_GPU)
if(MD_STATIC_DEPS)
    target_link_libraries(monkey_dust_engine PUBLIC SDL3::SDL3-static)
else()
    target_link_libraries(monkey_dust_engine PUBLIC SDL3::SDL3)
endif()
target_include_directories(monkey_dust_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/imgui/backends
    # MOC vendor: .cpp files include "MaskedOcclusionCulling.h" relatively
    ${CMAKE_CURRENT_SOURCE_DIR}/include/vendor/moc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/moc)
target_include_directories(monkey_dust_engine PUBLIC
    # miniaudio.h copied from raylib/src/external/ → engine/src/audio/external/miniaudio.h
    # Raylib submodule no longer needed.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio)

if(MD_STATIC_DEPS)
    set(_LUA_LINK lua54_static)
else()
    set(_LUA_LINK ${LUA_LIBRARIES})
endif()
target_link_libraries(monkey_dust_engine PRIVATE
    Recast Detour DetourCrowd
    Jolt
    ${_LUA_LINK}
    ozz_animation
    ozz_animation_offline
    ozz_base
)
# gameplay_module.cpp uses dlopen/dlclose (POSIX dl)
if(UNIX AND NOT APPLE)
    target_link_libraries(monkey_dust_engine PRIVATE dl)
endif()

if(USE_GLM AND NOT USE_SDL3)
    # In SDL3 path USE_GLM is already set above; this handles explicit -DUSE_GLM
    # without USE_SDL3 (uncommon but supported).
    target_compile_definitions(monkey_dust_engine PUBLIC USE_GLM)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR MONKEY_DUST_EDITOR)
    target_compile_definitions(monkey_dust_engine PUBLIC DEBUG)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(monkey_dust_engine PRIVATE /O2 /arch:AVX2)
    else()
        target_compile_options(monkey_dust_engine PRIVATE -O2 -march=native)
    endif()
endif()

# ── Precompiled Header (PCH) ─────────────────────────────────────────────────
# Vulkan perf pattern: avoid redundant preprocessing of heavy stdlib/SDL/entt
# headers in every TU. PCH compiles them once → 40-60% faster incremental builds.
# Note: PCH is NOT applied to MaskedOcclusionCulling (different ISA flags).
# Not applied to vendor impl files (already excluded from ENGINE_SRC above).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(monkey_dust_engine PRIVATE
        <cstdint> <cstdlib> <cstring> <cstdio> <cmath> <cstddef>
    )
    message(STATUS "[monkey_dust] PCH: ENABLED (stdlib only, ~2MB)")
endif()
