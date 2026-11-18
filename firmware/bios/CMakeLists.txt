cmake_minimum_required(VERSION 3.16)

# The BIOS: a small firmware of its own in the "bios" partition. It shows
# the self-test on its text screen and on serial, then restarts into the OS
# (docs/journal/2026-11-17-bios-second-stage.md).
#
# Flash it with tools/flash-bios.sh, never with "idf.py flash" from here:
# that would write this app where the OS lives and Espressif's bootloader
# over ours. The bootloader that starts the BIOS is built and flashed with
# the OS (firmware/os).

# The portable core lives outside this project and is shared with the OS
# and the PC build.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chinchilla-bios)
