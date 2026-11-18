idf_component_register(SRCS "main.cpp" "crash.cpp" "vga.c" "wifi.cpp" INCLUDE_DIRS "." "../../common" REQUIRES core app_update bootloader_support joltwallet__littlefs espressif__mdns espressif__elf_loader esp_driver_ledc esp_driver_uart esp_lcd esp_psram spi_flash esp_timer esp_wifi esp_netif esp_event nvs_flash)
# Every panic goes through __wrap_esp_panic_handler (crash.cpp) first, which
# writes the crash record and then calls ESP-IDF's handler.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# What programs loaded from files may ask the system for by name (backlog
# item 27): a table written from the core library this build has compiled.
set(exports_c "${CMAKE_CURRENT_BINARY_DIR}/exports.c")
idf_build_get_property(python PYTHON)
add_custom_command(
    OUTPUT ${exports_c}
    COMMAND ${python} ${CMAKE_CURRENT_LIST_DIR}/../../../tools/gen_exports.py --nm ${CMAKE_NM} --out ${exports_c}
            $<TARGET_FILE:__idf_core>
    DEPENDS __idf_core ${CMAKE_CURRENT_LIST_DIR}/../../../tools/gen_exports.py
    COMMENT "Exports for loaded programs")
target_sources(${COMPONENT_LIB} PRIVATE ${exports_c})

# The programs that come with the system (backlog item 27; the owner's
# choice, 2026-11-17): programs/* built into .app files with this build's
# compiler, checked against the table of exports above, and carried in the
# firmware as data. The system puts them in C:\Programs at start-up.
set(programs_dir "${CMAKE_BINARY_DIR}/programs")
set(bundle_c "${CMAKE_CURRENT_BINARY_DIR}/bundle.c")
set(repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB program_folders LIST_DIRECTORIES true "${repo}/programs/*")
set(program_files "")
foreach(folder ${program_folders})
    if(IS_DIRECTORY ${folder})
        get_filename_component(program ${folder} NAME)
        list(APPEND program_files "${programs_dir}/${program}.app")
    endif()
endforeach()
file(GLOB_RECURSE program_sources "${repo}/programs/*.cpp" "${repo}/programs/*.hpp"
     "${repo}/core/include/core/app/*.hpp" "${repo}/core/include/core/os/*.hpp")
add_custom_command(
    OUTPUT ${bundle_c} ${program_files}
    COMMAND ${CMAKE_COMMAND} -E env PROGRAMS_OUT=${programs_dir} CXX=${CMAKE_CXX_COMPILER} NM=${CMAKE_NM}
            STRIP=${CMAKE_STRIP} OBJCOPY=${CMAKE_OBJCOPY} READELF=${CMAKE_READELF}
            ${repo}/tools/build_programs.sh ${CMAKE_BINARY_DIR}
    COMMAND ${python} ${repo}/tools/gen_bundle.py --out ${bundle_c} ${program_files}
    DEPENDS __idf_core ${program_sources} ${repo}/tools/build_programs.sh ${repo}/tools/gen_bundle.py
            ${repo}/tools/pack_app.sh ${repo}/tools/program.ld ${repo}/tools/check_program_elf.py
            ${repo}/tools/gen_exports.py
    COMMENT "Programs that come with the system")
target_sources(${COMPONENT_LIB} PRIVATE ${bundle_c})
