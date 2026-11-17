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
# Emscripten's compiler is Clang, which reports three leftovers that GCC (the
# board's and the PC's compiler) does not: an unused constant in boot.cpp, a
# variable in desktop.cpp that is set and never read, and an unused field in
# widgets.hpp. They stay warnings here until they are tidied in core/.
target_compile_options(core PRIVATE -Wno-error=unused-const-variable -Wno-error=unused-but-set-global
                                    -Wno-error=unused-private-field)

add_executable(chinchilla ../host/desktop/main.cpp)
target_compile_options(chinchilla PRIVATE -Wall -Wextra -Werror)
target_link_libraries(chinchilla PRIVATE core idbfs.js)
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
