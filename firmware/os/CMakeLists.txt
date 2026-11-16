cmake_minimum_required(VERSION 3.16)

# The portable core lives outside this project and is shared with the PC build.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-os)
