# The video driver is the OS's (firmware/os/main/vga.c), compiled here too.
idf_component_register(SRCS "main.cpp" "../../os/main/vga.c"
                    INCLUDE_DIRS "." "../../os/main" "../../common"
                    REQUIRES core app_update bootloader_support esp_driver_ledc esp_driver_uart esp_lcd esp_psram
                             esp_partition spi_flash esp_timer nvs_flash)
