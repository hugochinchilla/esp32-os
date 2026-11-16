# PC build: unit tests for the core, picture tests and a simulated board for
# the debug tool.
#   cmake -S host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(esp32_os_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti)
# Address and undefined-behaviour checks on everything built for the PC, so a
# bad read in a decoder fails the tests instead of passing by luck.
option(HOST_SANITIZE "Build the PC tests with sanitizers" ON)
if(HOST_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(../core core)

add_executable(sim sim.cpp)
target_link_libraries(sim PRIVATE core)

add_executable(test_dbg tests/test_dbg.cpp)
target_link_libraries(test_dbg PRIVATE core)

add_executable(test_wm tests/test_wm.cpp)
target_link_libraries(test_wm PRIVATE core)

add_executable(test_edit tests/test_edit.cpp)
target_link_libraries(test_edit PRIVATE core)

add_executable(test_shell tests/test_shell.cpp)
target_link_libraries(test_shell PRIVATE core)

add_executable(test_image tests/test_image.cpp)
target_link_libraries(test_image PRIVATE core)

add_executable(test_fs tests/test_fs.cpp)
target_link_libraries(test_fs PRIVATE core)

add_executable(render_scenes tests/render_scenes.cpp)
target_link_libraries(render_scenes PRIVATE core)

enable_testing()
add_test(NAME dbg COMMAND test_dbg)
add_test(NAME wm COMMAND test_wm)
add_test(NAME fs COMMAND test_fs)
add_test(NAME image_pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_pictures.py tests ${CMAKE_CURRENT_BINARY_DIR}/pictures)
set_tests_properties(image_pictures PROPERTIES FIXTURES_SETUP pictures)
add_test(NAME image COMMAND test_image ${CMAKE_CURRENT_BINARY_DIR}/pictures)
set_tests_properties(image PROPERTIES FIXTURES_REQUIRED pictures)
add_test(NAME edit COMMAND test_edit)
add_test(NAME shell COMMAND test_shell)

add_executable(test_boot tests/test_boot.cpp)
target_link_libraries(test_boot PRIVATE core)
add_test(NAME boot COMMAND test_boot ${CMAKE_CURRENT_SOURCE_DIR}/../docs/look/boot)
add_test(NAME pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:render_scenes>)
add_test(NAME dbg_tool
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_dbg_sim.py $<TARGET_FILE:sim>)

add_executable(test_net tests/test_net.cpp)
target_link_libraries(test_net PRIVATE core)
add_test(NAME net COMMAND test_net)
add_executable(render_net_scenes tests/render_net_scenes.cpp)
target_link_libraries(render_net_scenes PRIVATE core)
add_test(NAME net_pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:render_net_scenes>)
set_tests_properties(net net_pictures PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

add_executable(dither_eval dither_eval.cpp)
target_link_libraries(dither_eval PRIVATE core)

add_executable(test_saver tests/test_saver.cpp)
target_link_libraries(test_saver PRIVATE core)
add_test(NAME saver COMMAND test_saver)

# Memory-port buffers live as long as the program, as on the board, so the
# leak checker would report them; applied after every add_test above.
get_property(all_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${all_tests} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
