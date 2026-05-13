cmake_minimum_required(VERSION 3.20)
project(monkey_dust_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    PRIVATE /usr/include/lua5.4
    # Vendor: glad, stb headers — PUBLIC so game/ and tools/ can find them too
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor
)

if(USE_SDL3)
    # SDL3 is the primary platform. SDL_GPU is always enabled with SDL3.
    # GLM provides math types; MD_LOG_NO_RAYLIB replaces TraceLog with fprintf.
    target_compile_definitions(monkey_dust_engine PUBLIC USE_SDL3 USE_GLM MD_LOG_NO_RAYLIB MD_SDL_GPU)
    target_link_libraries(monkey_dust_engine PUBLIC SDL3::SDL3)
    target_include_directories(monkey_dust_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends)
    target_include_directories(monkey_dust_engine PUBLIC
        # miniaudio.h lives in raylib/src/external/ — header-only, Raylib not linked.
        ${CMAKE_SOURCE_DIR}/third_party/raylib/src)
else()
    # Legacy Raylib path: engine links Raylib for math types + TraceLog.
    target_link_libraries(monkey_dust_engine PUBLIC raylib)
    target_include_directories(monkey_dust_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/raylib/src
        ${CMAKE_SOURCE_DIR}/third_party/rlImGui)
endif()

target_link_libraries(monkey_dust_engine PRIVATE
    Recast Detour DetourCrowd
    lua5.4
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
    target_compile_options(monkey_dust_engine PRIVATE -O2 -march=native)
endif()
