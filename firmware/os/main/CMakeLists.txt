idf_component_register(SRCS "main.cpp" "crash.cpp" "vga.c" "wifi.cpp" INCLUDE_DIRS "." REQUIRES core app_update joltwallet__littlefs espressif__mdns espressif__elf_loader esp_driver_uart esp_lcd esp_psram spi_flash esp_timer esp_wifi esp_netif esp_event nvs_flash)
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
