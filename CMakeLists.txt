cmake_minimum_required(VERSION 3.20)
project(monkey_dust_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE ENGINE_SRC src/*.cpp)
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
    # Raylib headers; external/ accessed as "external/glad.h" via this base path
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/raylib/src
    # NOTE: do NOT add raylib/src/external directly — it contains a Windows-only
    # dirent.h stub that shadows the Linux system <dirent.h>
)

# Raylib provides the OpenGL/glad headers
target_link_libraries(monkey_dust_engine PUBLIC raylib)
target_link_libraries(monkey_dust_engine PRIVATE
    Recast Detour DetourCrowd
    lua5.4
)

if(USE_SDL3)
    target_link_libraries(monkey_dust_engine PUBLIC SDL3::SDL3)
    target_compile_definitions(monkey_dust_engine PUBLIC USE_SDL3)
    target_include_directories(monkey_dust_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/imgui/backends)
else()
    target_include_directories(monkey_dust_engine PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/rlImGui)
endif()

if(USE_GLM)
    target_compile_definitions(monkey_dust_engine PUBLIC USE_GLM)
endif()
if(MD_OPENGL43)
    target_compile_definitions(monkey_dust_engine PUBLIC
        GRAPHICS_API_OPENGL_43 MD_OPENGL43_ENABLED)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR MONKEY_DUST_EDITOR)
    target_compile_definitions(monkey_dust_engine PUBLIC DEBUG)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(monkey_dust_engine PRIVATE -O2 -march=native)
endif()
