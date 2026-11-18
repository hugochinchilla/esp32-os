# PC build: unit tests for the core, picture tests and a simulated board for
# the debug tool.
#   cmake -S host -B build/host && cmake --build build/host && ctest --test-dir build/host
cmake_minimum_required(VERSION 3.16)
project(esp32_os_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Woverloaded-virtual -Werror -fno-exceptions -fno-rtti)
# Address and undefined-behaviour checks on everything built for the PC, so a
# bad read in a decoder fails the tests instead of passing by luck.
option(HOST_SANITIZE "Build the PC tests with sanitizers" ON)
# The thread checker instead (it cannot be combined with the other two), for
# the tests with more than one thread:
#   cmake -S host -B build/tsan -DHOST_SANITIZE=OFF -DHOST_TSAN=ON
option(HOST_TSAN "Build the PC tests with the thread sanitizer" OFF)
if(HOST_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()
if(HOST_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(../core core)

find_package(Threads REQUIRED) # the simulated board's services thread
add_executable(sim sim.cpp)
target_link_libraries(sim PRIVATE core Threads::Threads)

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

# The mock-up of the chess game's look (backlog item 50): built only when
# asked for (--target chess_mockup). Its pieces are drawn by a script.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/chess_pieces.inc
                   COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mockup/chess_pieces.py header
                           ${CMAKE_CURRENT_BINARY_DIR}/chess_pieces.inc
                   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mockup/chess_pieces.py)
add_executable(chess_mockup EXCLUDE_FROM_ALL ../tools/mockup/chess_mockup.cpp
               ${CMAKE_CURRENT_BINARY_DIR}/chess_pieces.inc)
target_include_directories(chess_mockup PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(chess_mockup PRIVATE core)

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
add_executable(test_bios tests/test_bios.cpp)
target_link_libraries(test_bios PRIVATE core)
add_test(NAME bios COMMAND test_bios ${CMAKE_CURRENT_SOURCE_DIR}/../docs/look/bios-text)
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
target_link_libraries(test_snake PRIVATE core Threads::Threads)
add_test(NAME snake COMMAND test_snake)
# Chess: the rules against the published move counts, and the engine.
add_executable(test_chess tests/test_chess.cpp)
target_link_libraries(test_chess PRIVATE core)
target_compile_options(test_chess PRIVATE -O2)
add_test(NAME chess COMMAND test_chess)
# The chess window: a game played with the mouse, the engine in a job on a
# real thread, and the game's reference pictures.
add_executable(test_chess_window tests/test_chess_window.cpp)
target_link_libraries(test_chess_window PRIVATE core Threads::Threads)
add_test(NAME chess_window COMMAND test_chess_window)
add_test(NAME chess_window_pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:test_chess_window>)
add_executable(test_saver tests/test_saver.cpp)
target_link_libraries(test_saver PRIVATE core)
add_test(NAME saver COMMAND test_saver)
add_executable(test_proc tests/test_proc.cpp)
target_link_libraries(test_proc PRIVATE core)
add_test(NAME proc COMMAND test_proc)
# init: PID 1, the start-up file, the brake on restarts (backlog 18).
add_executable(test_init tests/test_init.cpp)
target_link_libraries(test_init PRIVATE core)
add_test(NAME init COMMAND test_init)
# The OS calls, and the check that the programs moved to them stay moved
# (backlog 20).
add_executable(test_os tests/test_os.cpp)
target_link_libraries(test_os PRIVATE core Threads::Threads)
add_test(NAME os COMMAND test_os)
add_test(NAME os_calls
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_os_calls.py ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The list in docs/DECISIONS.md is written from the files in docs/decisions/.
add_test(NAME decisions_index
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/decisions_index.py --check ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Programs loaded from files (backlog 27). The sample program is built as a
# shared library of the PC and loaded by the PC's own loader; the test
# program lets it find the OS calls by name, as the firmware does.
function(add_program name source)
    add_library(${name}_app MODULE ${source})
    target_include_directories(${name}_app PRIVATE ../core/include ../core/src/shell)
    target_compile_options(${name}_app PRIVATE -fvisibility=hidden -fno-exceptions -fno-rtti)
    set_target_properties(${name}_app PROPERTIES PREFIX "" OUTPUT_NAME "${name}" SUFFIX ".so")
    # The program file: its header, then the code (tools/pack_app.sh).
    add_custom_command(TARGET ${name}_app POST_BUILD
                       COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../tools/pack_app.sh ${CMAKE_OBJCOPY}
                               $<TARGET_FILE:${name}_app> ${CMAKE_CURRENT_BINARY_DIR}/${name}.app)
endfunction()
add_program(hello ../programs/hello/hello.cpp)
# The games in the form the board has them in (a file each); the PC's
# system also has them built in.
add_program(culebra ../programs/culebra/culebra.cpp)
add_program(cactus_patch ../programs/cactus_patch/cactus_patch.cpp)
add_program(chess ../programs/chess/chess.cpp)
# With the engine in it: optimised, as the engine is in the core, and
# without the checkers, which would make the file larger than the megabyte
# a program may be (the same source is checked as part of the core).
target_compile_options(chess_app PRIVATE -O2 -fno-sanitize=all)
# Not a program of the system: the chess engine's measuring program for
# board checks (tools/bench/chess_bench), built here so that it stays whole.
add_program(chess_bench ../tools/bench/chess_bench/chess_bench.cpp)
add_executable(test_loaded tests/test_loaded.cpp)
target_link_libraries(test_loaded PRIVATE core ${CMAKE_DL_LIBS} Threads::Threads)
set_target_properties(test_loaded PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(test_loaded hello_app culebra_app cactus_patch_app chess_app)
add_test(NAME loaded COMMAND test_loaded ${CMAKE_CURRENT_BINARY_DIR}/hello.app ${CMAKE_CURRENT_BINARY_DIR}/culebra.app
         ${CMAKE_CURRENT_BINARY_DIR}/cactus_patch.app ${CMAKE_CURRENT_BINARY_DIR}/chess.app)
# The serial console on the debug link's wire (backlog 14).
add_executable(test_console tests/test_console.cpp)
target_link_libraries(test_console PRIVATE core)
add_test(NAME console COMMAND test_console)
# Updates over the network (backlog 25).
add_executable(test_update tests/test_update.cpp)
target_link_libraries(test_update PRIVATE core)
add_test(NAME update COMMAND test_update)
add_test(NAME ota_tool
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_ota_sim.py $<TARGET_FILE:sim>)
add_test(NAME serial_console
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_serial_console.py $<TARGET_FILE:sim>)
add_test(NAME dbg_reader
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_dbg_reader.py)
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

add_executable(test_prog tests/test_prog.cpp)
target_link_libraries(test_prog PRIVATE core Threads::Threads)
add_test(NAME prog COMMAND test_prog)

add_executable(test_sh tests/test_sh.cpp)
target_link_libraries(test_sh PRIVATE core Threads::Threads)
add_test(NAME sh COMMAND test_sh)
add_executable(chinchilla-console console/main.cpp)
target_link_libraries(chinchilla-console PRIVATE core Threads::Threads)
add_test(NAME console_program
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_console_program.py $<TARGET_FILE:chinchilla-console>)

# The Terminal window (backlog item 15).
add_executable(test_terminal tests/test_terminal.cpp)
target_link_libraries(test_terminal PRIVATE core Threads::Threads)
add_test(NAME terminal COMMAND test_terminal)

# The services loop: its rounds and rests, input as messages, and a viewer on
# a real socket while one thread paints and another serves.
add_executable(test_services tests/test_services.cpp)
target_link_libraries(test_services PRIVATE core Threads::Threads)
add_test(NAME services COMMAND test_services)
add_executable(test_telnet tests/test_telnet.cpp)
target_link_libraries(test_telnet PRIVATE core Threads::Threads)
add_test(NAME telnet COMMAND test_telnet)

# The watchdog's bookkeeping, the crash record and the error screen.
add_executable(test_crash tests/test_crash.cpp)
target_link_libraries(test_crash PRIVATE core)
add_test(NAME crash COMMAND test_crash)
add_executable(test_missed_refills tests/test_missed_refills.cpp)
target_link_libraries(test_missed_refills PRIVATE core)
add_test(NAME missed_refills COMMAND test_missed_refills)
# The desktop against the video frame: frames over budget and late timers.
add_executable(test_frames tests/test_frames.cpp)
target_link_libraries(test_frames PRIVATE core)
add_test(NAME frames COMMAND test_frames)

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

# The browser's portable parts: addresses, HTML, layout, HTTP.
add_executable(test_www tests/test_www.cpp)
target_link_libraries(test_www PRIVATE core)
add_test(NAME www COMMAND test_www)
add_executable(test_dialer tests/test_dialer.cpp)
target_link_libraries(test_dialer PRIVATE core Threads::Threads)
add_test(NAME dialer COMMAND test_dialer)

# The browser on the desktop, against a web kept in the test: its screens
# compared with the reference pictures.
add_executable(render_browser_scenes tests/render_browser_scenes.cpp)
target_link_libraries(render_browser_scenes PRIVATE core)
add_test(NAME browser_pictures
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_pictures.py $<TARGET_FILE:render_browser_scenes>)

# Memory-port buffers live as long as the program, as on the board, so the
# leak checker would report them; applied after every add_test above.
get_property(all_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${all_tests} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
