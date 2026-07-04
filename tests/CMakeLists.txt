cmake_minimum_required(VERSION 3.21)

# Flare unit tests — plain C++, no GTest.
# Run binaries from tests/flare_test/ so "fixtures/" relative paths resolve.
# test_flare_loaders + test_m7_integration require third_party/flare-game data
# which is not cloned in CI — excluded from md_tests target.

function(add_flare_test name src)
    add_executable(${name} flare_test/${src})
    target_link_libraries(${name} PRIVATE monkey_dust::engine)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
    )
endfunction()

add_flare_test(flare_ini_parser test_ini_parser.cpp)
add_flare_test(flare_tile_map   test_tile_map.cpp)

# md_tests: meta build-target — workflow does: ninja -C build md_tests
add_custom_target(md_tests DEPENDS flare_ini_parser flare_tile_map)
