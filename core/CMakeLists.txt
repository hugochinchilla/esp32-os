# The portable core. Builds as an ESP-IDF component and as a plain CMake
# library for the PC. Nothing in here may include ESP-IDF or OS headers.
set(CORE_SRCS
    src/dbg/cobs.cpp
    src/dbg/crc.cpp
    src/dbg/protocol.cpp
    src/demo/demo_screen.cpp
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS "include")
    return()
endif()

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC include)
target_compile_features(core PUBLIC cxx_std_17)
target_compile_options(core PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
