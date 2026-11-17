# Our own start of Espressif's second-stage bootloader: it chooses between
# the BIOS and the OS (bootloader_start.c). Everything else is Espressif's.
idf_component_register(SRCS "bootloader_start.c"
                    INCLUDE_DIRS "../../../common"
                    REQUIRES bootloader bootloader_support)

# Use the default linker scripts
idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
