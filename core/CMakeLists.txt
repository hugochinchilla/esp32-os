# The portable core. Builds as an ESP-IDF component and as a plain CMake
# library for the PC. Nothing in here may include ESP-IDF or OS headers.
set(CORE_SRCS
    src/dbg/cobs.cpp
    src/dbg/crc.cpp
    src/dbg/protocol.cpp
    src/fs/fs.cpp
    src/fs/memory_volume.cpp
    src/fs/sample_pictures.cpp
    src/image/blue_noise.cpp
    src/image/bmp.cpp
    src/image/convert.cpp
    src/image/decoder.cpp
    src/image/inflate.cpp
    src/image/jpeg.cpp
    src/image/png.cpp
    src/gfx/art.cpp
    src/gfx/canvas.cpp
    src/gfx/font_spleen6x12.cpp
    src/gfx/rect.cpp
    src/shell/apps.cpp
    src/shell/boot.cpp
    src/shell/desktop.cpp
    src/shell/dialogs.cpp
    src/shell/editor.cpp
    src/shell/file_manager.cpp
    src/shell/listing.cpp
    src/shell/viewer.cpp
    src/wm/text_edit.cpp
    src/wm/widgets.cpp
    src/wm/window.cpp
    src/wm/wm.cpp
    src/video/pointer.cpp
    src/video/pointer_shapes.cpp
)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS "include" LDFRAGMENTS "linker.lf")
    return()
endif()

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC include)
target_compile_features(core PUBLIC cxx_std_17)
target_compile_options(core PRIVATE -Wall -Wextra -Werror -fno-exceptions -fno-rtti)
