# The programs of programs/ compiled into a binary instead of into files
# (core/app/linked.hpp): for a machine that cannot load code (the web page)
# and for the PC's tests, which look inside the programs. Each program is
# its folder's sources with APP_LINKED set to the name of its file.
#
#   include(../programs/linked.cmake)
#   target_link_libraries(<target> PRIVATE core programs_linked)
#
# Not a static library: nothing names a linked program, it puts itself on
# the list before main(), so the linker would leave all of it out.
set(PROGRAMS_DIR ${CMAKE_CURRENT_LIST_DIR})
add_library(programs_linked INTERFACE)
file(GLOB program_dirs LIST_DIRECTORIES true ${PROGRAMS_DIR}/*)
foreach(dir ${program_dirs})
    if(NOT IS_DIRECTORY ${dir})
        continue()
    endif()
    get_filename_component(name ${dir} NAME)
    file(GLOB sources CONFIGURE_DEPENDS ${dir}/*.cpp)
    add_library(linked_${name} OBJECT ${sources})
    target_compile_definitions(linked_${name} PRIVATE APP_LINKED="${name}")
    target_include_directories(linked_${name} PRIVATE ${PROGRAMS_DIR}/../core/include ${PROGRAMS_DIR}/../core/src/shell)
    target_compile_options(linked_${name} PRIVATE -fno-exceptions -fno-rtti)
    target_sources(programs_linked INTERFACE $<TARGET_OBJECTS:linked_${name}>)
endforeach()
# The chess engine's tests look at millions of positions: optimised even in
# the PC's checked build, where everything else is not.
set_source_files_properties(${PROGRAMS_DIR}/chess/chess_game.cpp PROPERTIES COMPILE_OPTIONS "-O2")
