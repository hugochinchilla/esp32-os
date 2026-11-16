# PC build: unit tests for the core, picture tests and a simulated board for
# the debug tool.
#   cmake -S host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(esp32_os_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti)

add_subdirectory(../core core)

add_executable(sim sim.cpp)
target_link_libraries(sim PRIVATE core)

add_executable(test_dbg tests/test_dbg.cpp)
target_link_libraries(test_dbg PRIVATE core)

add_executable(test_wm tests/test_wm.cpp)
target_link_libraries(test_wm PRIVATE core)

add_executable(test_fs tests/test_fs.cpp)
target_link_libraries(test_fs PRIVATE core)

add_executable(render_scenes tests/render_scenes.cpp)
target_link_libraries(render_scenes PRIVATE core)

enable_testing()
add_test(NAME dbg COMMAND test_dbg)
add_test(NAME wm COMMAND test_wm)
add_test(NAME fs COMMAND test_fs)
add_test(NAME pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:render_scenes>)
add_test(NAME dbg_tool
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_dbg_sim.py $<TARGET_FILE:sim>)
