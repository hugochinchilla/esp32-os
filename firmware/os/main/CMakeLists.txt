idf_component_register(SRCS "main.cpp" "crash.cpp" "vga.c" "wifi.cpp" INCLUDE_DIRS "." REQUIRES core joltwallet__littlefs espressif__mdns esp_driver_uart esp_lcd esp_psram spi_flash esp_timer esp_wifi esp_netif esp_event nvs_flash)
# Every panic goes through __wrap_esp_panic_handler (crash.cpp) first, which
# writes the crash record and then calls ESP-IDF's handler.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
