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
add_executable(test_paint tests/test_paint.cpp)
target_link_libraries(test_paint PRIVATE core)
target_link_libraries(test_shell PRIVATE core)

add_executable(test_image tests/test_image.cpp)
target_link_libraries(test_image PRIVATE core)

add_executable(test_fs tests/test_fs.cpp)
find_package(Threads REQUIRED) # the two-task test of the file system's lock
target_link_libraries(test_fs PRIVATE core Threads::Threads)

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
add_test(NAME paint COMMAND test_paint)

add_executable(test_boot tests/test_boot.cpp)
target_link_libraries(test_boot PRIVATE core)
add_test(NAME boot COMMAND test_boot ${CMAKE_CURRENT_SOURCE_DIR}/../docs/look/boot)
add_executable(test_system tests/test_system.cpp)
target_link_libraries(test_system PRIVATE core)
add_test(NAME system COMMAND test_system)
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
add_executable(test_rfb tests/test_rfb.cpp)
target_link_libraries(test_rfb PRIVATE core)
add_test(NAME rfb COMMAND test_rfb)
add_test(NAME vnc_tool
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_vnc_sim.py $<TARGET_FILE:sim> $<TARGET_FILE:test_rfb>)
set_tests_properties(net net_pictures rfb vnc_tool PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

add_executable(dither_eval dither_eval.cpp)
target_link_libraries(dither_eval PRIVATE core)

# Cactus Patch: rules and window tests, and its own reference pictures (the
# same program writes them when given a folder).
add_executable(test_cactus tests/test_cactus.cpp)
target_link_libraries(test_cactus PRIVATE core)
add_test(NAME cactus COMMAND test_cactus)
add_test(NAME cactus_pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:test_cactus>)

add_executable(test_snake tests/test_snake.cpp)
target_link_libraries(test_snake PRIVATE core)
add_test(NAME snake COMMAND test_snake)
add_executable(test_saver tests/test_saver.cpp)
target_link_libraries(test_saver PRIVATE core)
add_test(NAME saver COMMAND test_saver)
add_executable(test_proc tests/test_proc.cpp)
target_link_libraries(test_proc PRIVATE core)
add_test(NAME proc COMMAND test_proc)
# Programs made when opened and destroyed when closed (backlog 19 and 34).
add_executable(test_lifecycle tests/test_lifecycle.cpp)
target_link_libraries(test_lifecycle PRIVATE core)
add_test(NAME lifecycle COMMAND test_lifecycle)
# The message queue into the UI task, posted to from real threads.
find_package(Threads REQUIRED)
add_executable(test_msg tests/test_msg.cpp)
target_link_libraries(test_msg PRIVATE core Threads::Threads)
add_test(NAME msg COMMAND test_msg)

# The TTY (backlog item 12), with a session on a thread of its own.
add_executable(test_tty tests/test_tty.cpp)
target_link_libraries(test_tty PRIVATE core Threads::Threads)
add_test(NAME tty COMMAND test_tty)

# The command shell (backlog item 13): its tests, and the shell itself in a
# terminal of the PC (build/host/chinchilla-console [FOLDER]).
add_executable(test_job tests/test_job.cpp)
target_link_libraries(test_job PRIVATE core Threads::Threads)
add_test(NAME job COMMAND test_job)

add_executable(test_sh tests/test_sh.cpp)
target_link_libraries(test_sh PRIVATE core Threads::Threads)
add_test(NAME sh COMMAND test_sh)
add_executable(chinchilla-console console/main.cpp)
target_link_libraries(chinchilla-console PRIVATE core Threads::Threads)
add_test(NAME console_program
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_console_program.py $<TARGET_FILE:chinchilla-console>)

# The Terminal window (backlog item 15).
add_executable(test_terminal tests/test_terminal.cpp)
target_link_libraries(test_terminal PRIVATE core)
add_test(NAME terminal COMMAND test_terminal)

# Chinchilla as a desktop program (backlog item 32): the same core in an SDL2
# window. Its parts that need no window are tested everywhere; the program
# itself is built only where SDL2 is installed, so the tests above need
# nothing new. To hand to others, build without the checks:
#   cmake -S host -B build/desktop -DHOST_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/desktop --target chinchilla
add_executable(test_desktop tests/test_desktop.cpp)
target_link_libraries(test_desktop PRIVATE core)
add_test(NAME desktop COMMAND test_desktop)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    add_executable(chinchilla desktop/main.cpp)
    if(TARGET SDL2::SDL2)
        target_link_libraries(chinchilla PRIVATE core SDL2::SDL2)
    else()
        target_include_directories(chinchilla PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(chinchilla PRIVATE core ${SDL2_LIBRARIES})
    endif()
    # It starts with no screen at all (SDL's "dummy" video), runs for a
    # moment on a new, empty C: and saves what its window shows.
    add_test(NAME desktop_program
             COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_desktop_program.py $<TARGET_FILE:chinchilla>)
else()
    message(STATUS "SDL2 not found: the desktop program (chinchilla) is not built")
endif()

# Memory-port buffers live as long as the program, as on the board, so the
# leak checker would report them; applied after every add_test above.
get_property(all_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${all_tests} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
