# Chinchilla in a web browser (backlog item 33): the desktop program
# (host/desktop/main.cpp) and the same core/, built to WebAssembly with
# Emscripten. The result is a folder to put on any web server.
#
#   emcmake cmake -S web -B build/web
#   cmake --build build/web
#   python3 -m http.server -d build/web/site 8080     # then http://localhost:8080/
cmake_minimum_required(VERSION 3.16)
project(chinchilla_web CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This builds for the browser: configure it with 'emcmake cmake -S web -B build/web'.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# -Os: the page is downloaded, and the PC that opens it is many times faster
# than the board either way.
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
add_compile_options(-fno-exceptions -fno-rtti -sUSE_SDL=2)

add_subdirectory(../core core)
# Emscripten's compiler is Clang. core/ is written and checked with GCC (the
# board's and the PC's compiler), with warnings as errors, and Clang warns
# about things GCC accepts. Nobody but this build runs Clang, so here they
# stay warnings: a new one in core/ must not stop the page from being
# published. They show in the build's output, to be tidied in core/.
target_compile_options(core PRIVATE -Wno-error)

# The system's sounds, compiled in (the board does not get them).
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/system_sounds.cpp
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/sounds/embed_sounds.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../docs/sounds ${CMAKE_CURRENT_BINARY_DIR}/system_sounds.cpp
    DEPENDS ../tools/sounds/embed_sounds.py ../docs/sounds/boot.pcm ../docs/sounds/close.pcm
            ../docs/sounds/alert.pcm ../docs/sounds/error.pcm ../docs/sounds/bell.pcm
            ../docs/sounds/chess_move.pcm ../docs/sounds/chess_check.pcm ../docs/sounds/chess_mate.pcm
            ../docs/sounds/snake_eat.pcm ../docs/sounds/snake_over.pcm ../docs/sounds/crumple.pcm)
add_executable(chinchilla ../host/desktop/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/system_sounds.cpp)
target_compile_options(chinchilla PRIVATE -Wall -Wextra -Werror)
target_link_libraries(chinchilla PRIVATE core idbfs.js)
# How the page reaches the web (host/desktop/web_dialer.hpp): the address of
# the relay, web/relay/worker.js put on Cloudflare. Empty: programs are told
# there is no network, unless the page is opened with "?relay=https://...".
#   emcmake cmake -S web -B build/web -DCHINCHILLA_RELAY=https://chinchilla-relay.<you>.workers.dev/
set(CHINCHILLA_RELAY "" CACHE STRING "Address of the relay the page fetches web pages through")
target_compile_definitions(chinchilla PRIVATE CHINCHILLA_RELAY="${CHINCHILLA_RELAY}")
set_target_properties(chinchilla PROPERTIES
    OUTPUT_NAME index
    SUFFIX .html
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/site)
target_link_options(chinchilla PRIVATE
    -Os
    -sUSE_SDL=2
    # The start-up screens wait in a loop and the drive is read back before
    # anything starts; a browser only allows waiting if the program can be
    # put aside and picked up again, which is what Asyncify adds.
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=65536
    -sALLOW_MEMORY_GROWTH
    -sINITIAL_MEMORY=33554432
    -sENVIRONMENT=web
    # Everything in one file, index.html: nothing to forget when copying it,
    # and it can be sent to someone as it is.
    -sSINGLE_FILE
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
set_property(TARGET chinchilla APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
