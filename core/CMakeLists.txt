# The portable core. Builds as an ESP-IDF component and as a plain CMake
# library for the PC. Nothing in here may include ESP-IDF or OS headers.
set(CORE_SRCS
    src/bios/post.cpp
    src/bios/text_screen.cpp
    src/dbg/cobs.cpp
    src/dbg/crc.cpp
    src/dbg/protocol.cpp
    src/fs/fs.cpp
    src/fs/ini.cpp
    src/fs/memory_volume.cpp
    src/fs/sample_pictures.cpp
    src/games/cactus.cpp
    src/games/chess.cpp
    src/games/snake.cpp
    src/image/blue_noise.cpp
    src/image/bmp.cpp
    src/image/convert.cpp
    src/image/decoder.cpp
    src/image/inflate.cpp
    src/image/jpeg.cpp
    src/image/png.cpp
    src/gfx/art.cpp
    src/gfx/canvas.cpp
    src/gfx/theme.cpp
    src/gfx/font_spleen12x24.cpp
    src/gfx/font_spleen32x64.cpp
    src/gfx/font_spleen6x12.cpp
    src/gfx/font_spleen8x16.cpp
    src/gfx/rect.cpp
    src/saver/chinchillas.cpp
    src/saver/curves.cpp
    src/saver/runner.cpp
    src/saver/starfield.cpp
    src/net/deflate.cpp
    src/net/rfb.cpp
    src/net/services.cpp
    src/net/telnet.cpp
    src/net/update.cpp
    src/net/viewer_page.cpp
    src/net/web.cpp
    src/net/wifi.cpp
    src/www/html.cpp
    src/www/http.cpp
    src/www/url.cpp
    src/init/init.cpp
    src/os/os.cpp
    src/os/tcp.cpp
    src/os/ui.cpp
    src/desk/icons.cpp
    src/desk/taskbar.cpp
    src/job/job.cpp
    src/prog/prog.cpp
    src/msg/input.cpp
    src/msg/queue.cpp
    src/proc/table.cpp
    src/sh/console.cpp
    src/sh/files.cpp
    src/sh/filters.cpp
    src/sh/path.cpp
    src/sh/sh.cpp
    src/sh/system.cpp
    src/sh/terminal_session.cpp
    src/sh/ted.cpp
    src/sys/crash.cpp
    src/sys/log.cpp
    src/sys/missed_refills.cpp
    src/shell/about.cpp
    src/shell/apps.cpp
    src/shell/gallery.cpp
    src/shell/welcome.cpp
    src/shell/boot.cpp
    src/shell/browser.cpp
    src/shell/cactus_patch.cpp
    src/shell/culebra.cpp
    src/shell/defrag.cpp
    src/shell/desktop.cpp
    src/shell/display.cpp
    src/shell/colour_dialog.cpp
    src/shell/dialogs.cpp
    src/shell/editor.cpp
    src/shell/file_manager.cpp
    src/shell/trash.cpp
    src/shell/listing.cpp
    src/shell/loaded.cpp
    src/shell/screensaver.cpp
    src/shell/network.cpp
    src/shell/network_service.cpp
    src/shell/power.cpp
    src/shell/run.cpp
    src/shell/settings.cpp
    src/shell/shutdown.cpp
    src/shell/task_manager.cpp
    src/shell/sessions.cpp
    src/shell/terminal.cpp
    src/shell/viewer.cpp
    src/tty/screen.cpp
    src/tty/tty.cpp
    src/wm/text_edit.cpp
    src/wm/widgets.cpp
    src/wm/window.cpp
    src/wm/wm.cpp
    src/video/pointer.cpp
    src/video/pointer_shapes.cpp
)

if(ESP_PLATFORM)
    # On the board the two games are programs kept in files (programs/,
    # backlog item 27; the owner's choice, 2026-11-17): built by themselves,
    # carried by the firmware as data and installed in C:\Programs. They
    # are not part of this library there; on the PC they are.
    list(REMOVE_ITEM CORE_SRCS src/games/cactus.cpp src/games/chess.cpp src/games/snake.cpp src/shell/cactus_patch.cpp
         src/shell/culebra.cpp)
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS "include" LDFRAGMENTS "linker.lf")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC CHINCHILLA_GAMES_IN_FILES=1)
    return()
endif()

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC include)
target_compile_features(core PUBLIC cxx_std_17)
target_compile_options(core PRIVATE -Wall -Wextra -Woverloaded-virtual -Werror -fno-exceptions -fno-rtti)
# The chess engine's tests look at millions of positions: optimised even in
# the PC's checked build, where everything else is not.
set_source_files_properties(src/games/chess.cpp PROPERTIES COMPILE_OPTIONS "-O2")
