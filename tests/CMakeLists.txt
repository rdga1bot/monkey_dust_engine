cmake_minimum_required(VERSION 3.21)

# Flare unit tests — plain C++, no GTest.
# Run binaries from tests/flare_test/ so "fixtures/" relative paths resolve.
# test_flare_loaders + test_m7_integration require third_party/flare-game data
# which is not cloned in CI — excluded from md_tests target.

# stb_image_impl/glad_impl/cgltf_impl are deliberately excluded from the
# engine static lib (see root CMakeLists.txt) -- every final executable must
# supply its own single implementation TU, same as game/CMakeLists.txt does.
# Needed here because unity-build bundling can pull any engine .cpp (e.g.
# terrain_gen.cpp's stbi_load call) into whatever object a test needs linked,
# which then requires these symbols to resolve even for otherwise-unrelated
# tests (confirmed: flare tests failed to link once terrain_gen.cpp gained
# a stbi_load call in the same unity chunk).
set(MD_TEST_VENDOR_IMPL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vendor/glad_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vendor/stb_image_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/render/cgltf_impl.cpp
)

function(add_flare_test name src)
    add_executable(${name} flare_test/${src} ${MD_TEST_VENDOR_IMPL_SRCS})
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
