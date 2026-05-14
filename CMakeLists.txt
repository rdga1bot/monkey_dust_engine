cmake_minimum_required(VERSION 3.20)
project(monkey_dust_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-platform Lua 5.4 discovery.
# Ubuntu/Arch: search lua5.4 explicitly so liblua.so → 5.5 symlink is bypassed.
# Windows/vcpkg: find_library misses vcpkg names → fall through to find_package.
find_library(MD_LUA54_LIB NAMES lua5.4 lua-5.4 QUIET)
find_path   (MD_LUA54_INC lua5.4/lua.h QUIET)
if(MD_LUA54_LIB AND MD_LUA54_INC)
    set(LUA_LIBRARIES   "${MD_LUA54_LIB}")
    set(LUA_INCLUDE_DIR "${MD_LUA54_INC}")
    message(STATUS "[monkey_dust] Lua 5.4 (explicit): ${MD_LUA54_LIB}")
else()
    find_package(Lua 5.4 REQUIRED)
    message(STATUS "[monkey_dust] Lua 5.4 (find_package): ${LUA_LIBRARIES}")
endif()

file(GLOB_RECURSE ENGINE_SRC src/*.cpp)
# stb_image_impl, glad_impl, cgltf_impl must NOT be in the engine static lib —
# each binary (game, editor, demo) compiles them directly to avoid duplicate symbols
# when the same impl appears in both the engine lib and the binary.
list(FILTER ENGINE_SRC EXCLUDE REGEX "stb_image_impl\\.cpp$")
list(FILTER ENGINE_SRC EXCLUDE REGEX "glad_impl\\.cpp$")
list(FILTER ENGINE_SRC EXCLUDE REGEX "cgltf_impl\\.cpp$")
add_library(monkey_dust_engine STATIC ${ENGINE_SRC})
add_library(monkey_dust::engine ALIAS monkey_dust_engine)

target_include_directories(monkey_dust_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Third-party (PUBLIC so consumers also see them)
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/entt/single_include
    PUBLIC  ${CMAKE_SOURCE_DIR}/third_party/glm
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/Recast/Include
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/Detour/Include
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/recastnavigation/DetourCrowd/Include
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
target_link_libraries(monkey_dust_engine PUBLIC SDL3::SDL3)
target_include_directories(monkey_dust_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/imgui/backends)
target_include_directories(monkey_dust_engine PUBLIC
    # miniaudio.h lives in raylib/src/external/ — header-only; Raylib itself is NOT linked.
    ${CMAKE_SOURCE_DIR}/third_party/raylib/src)

target_link_libraries(monkey_dust_engine PRIVATE
    Recast Detour DetourCrowd
    ${LUA_LIBRARIES}
)

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
