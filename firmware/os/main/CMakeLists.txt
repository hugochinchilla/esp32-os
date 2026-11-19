idf_component_register(SRCS "main.cpp" "crash.cpp" "vga.c" "wifi.cpp" "bluetooth.cpp" INCLUDE_DIRS "." "../../common" REQUIRES core app_update bootloader_support joltwallet__littlefs espressif__mdns espressif__elf_loader esp_driver_uart esp_lcd esp_psram spi_flash esp_timer esp_wifi esp_netif esp_event nvs_flash bt)
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

# The programs kept in files (backlog item 27): programs/* built into .app
# files with this build's compiler and checked against the table of exports
# above. The firmware does not carry them (backlog item 77): tools/flash.py
# copies build/programs/*.app to C:\Programs after it has written the
# firmware.
set(programs_dir "${CMAKE_BINARY_DIR}/programs")
set(programs_stamp "${programs_dir}/built.stamp")
set(repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB_RECURSE program_sources "${repo}/programs/*.cpp" "${repo}/programs/*.hpp" "${repo}/programs/*.c"
     "${repo}/programs/*.h" "${repo}/programs/*.txt" "${repo}/third_party/doomgeneric/*.[ch]"
     "${repo}/core/include/core/app/*.hpp" "${repo}/core/include/core/os/*.hpp")
add_custom_command(
    OUTPUT ${programs_stamp}
    COMMAND ${CMAKE_COMMAND} -E env PROGRAMS_OUT=${programs_dir} CXX=${CMAKE_CXX_COMPILER} CC=${CMAKE_C_COMPILER} NM=${CMAKE_NM}
            STRIP=${CMAKE_STRIP} OBJCOPY=${CMAKE_OBJCOPY} READELF=${CMAKE_READELF}
            ${repo}/tools/build_programs.sh ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${programs_stamp}
    DEPENDS __idf_core ${program_sources} ${repo}/tools/build_programs.sh
            ${repo}/tools/pack_app.sh ${repo}/tools/program.ld ${repo}/tools/check_program_elf.py
            ${repo}/tools/gen_exports.py
    COMMENT "Programs kept in files")
add_custom_target(programs ALL DEPENDS ${programs_stamp})
