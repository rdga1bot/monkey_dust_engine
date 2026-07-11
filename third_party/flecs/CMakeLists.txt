# flecs — vendored amalgamated distribution (flecs.c + flecs.h), v4.1.6.
# Task #8 B2: added as a standalone dependency, parallel to EnTT, not wired
# into monkey_dust_engine's own sources/includes. Only the isolated
# tests/test_md_flecs_standalone.cpp target links against this — nothing
# in engine/, game/, or tools/ references flecs yet. See B3 for the (future,
# separate, most-reviewed) step that would swap MdRegistry's internal
# implementation onto this.
add_library(flecs STATIC flecs.c)
target_include_directories(flecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(flecs PUBLIC cxx_std_17)
if(NOT WIN32)
    target_link_libraries(flecs PUBLIC pthread)
endif()
