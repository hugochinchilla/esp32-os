idf_component_register(SRCS "main.cpp" "vga.c" INCLUDE_DIRS "." REQUIRES core joltwallet__littlefs esp_driver_uart esp_lcd esp_psram spi_flash esp_timer)
