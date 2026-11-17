# The OS calls

What a program may ask of Chinchilla. The calls are declared in
`core/include/core/os/os.hpp`, in the namespace `os`. A program is a window
class with its widgets (`core/wm`) plus these calls. It does not name the
window manager, the drives, the process table or the hardware ports itself.

Why one place: here the system knows who is asking (the current process),
so this is where counting, refusing and limits can be added without
touching any program. It is also what a program built apart from the
system (backlog item 27) would need to be given.

The journal entry with the reasons is
[The OS calls](journal/2026-11-17-os-calls.md).

## The calls

| Group | Call | What it does |
| --- | --- | --- |
| Process | `os::self()` | The process that is asking |
| | `os::spawn(name)` | A new process, child of the one asking; it ends when its last window closes |
| | `os::find(name)` | The process with that name |
| | `os::As as(pid);` | Until the end of the block, what is opened or asked for is that process's |
| | `os::process_count()`, `os::process_at(i)`, `os::process(pid)`, `os::alive(pid)` | The process list, for the tools that show it |
| | `os::responding(pid)` | False while a program on a task of its own is in a call that has taken longer than the system waits |
| | `os::refresh_figures()` | Time and memory of running jobs brought up to now |
| | `os::signal(pid, signal)` | Asks a process to close, or ends it; the answer says if it may be |
| Time | `os::now_us()` | Microseconds since start-up |
| | `os::start_timer(delay, period, fn, context)` | A timer of the asking process; the system stops it when the process ends |
| | `os::stop_timer(id)` | |
| Memory | `os::allocate(bytes)` | Zeroed memory, counted as the asking process's (Task Manager, `top`) |
| | `os::release(block)` | Gives it back; taken off the count of whoever asked for it |
| | `os::memory()` | The same two as a Memory port, for code that takes one |
| | `os::blocks_out()`, `os::bytes_out()` | What is out over all processes (the tests use it to find leaks) |
| Windows | `os::show(window)` | Opens the window as the asking process's, or brings it to the front |
| | `os::activate(window)`, `os::move(window, frame)` | |
| | `os::work_area()`, `os::centred(w, h)` | The screen without the taskbar; a frame in the middle of it |
| | `os::menu_open()` | A menu is open (a game pauses) |
| | `os::open_menu(menu, x, y)` | A free-standing menu |
| | `os::blocker(window)`, `os::in_front(window)` | The dialog in a window's way; whether it is the one in use |
| | `os::main_window(pid)`, `os::window_title(id)`, `os::window_icon(id)`, `os::bring_forward(id)` | Another process's window, as a number (`os::WindowId`) and never as its object: its title and icon, and bringing it or its dialog to the front. A closed window's number answers nothing |
| | `os::caret_moved()` | The text cursor of the window in use has moved: shown at once (the Terminal) |
| Windows by number | `os::ui` (`core/os/ui.hpp`): windows, controls and their events | A program asks for a window and controls and gets numbers; written through the thin classes of `core/app/app.hpp` (item 37) |
| | `os::ui::icon_view`, `set_items`, `item_at`, `reset_view`; events Drag, DragOver, Dropped | The view of icons with names (File manager), and files dragged out of it and dropped on a window |
| | `os::ui::draw::fill`, `text`, `blit`, `part`, ... | Drawing while a Paint event is answered, on a number good only until it returns (`app::Canvas`) |
| | `os::begin_drag(path)` | A file starts being dragged |
| | `os::Busy busy;` | The hourglass until the end of the block |
| Desktop | `os::set_backdrop(backdrop)` | The desktop's colour and pattern (Display) |
| | `os::set_screen_saver(animation, seconds)` | Which animation and after how long; -1 for none (Screen Saver) |
| | `os::preview_screen_saver(animation)`, `os::last_saver_run()` | Shows one now; how the last run went |
| | `os::shut_down()` | Asks every program to close and shows the power-off screen; false if one called it off |
| Network | `os::network()` | The Wi-Fi service, or nothing on a board with no radio; asked for each time, not kept |
| Sessions | `os::open_session(device, why)` | A command shell on a TTY of its own, as a process `sh`, child of the one asking, which is its device (the Terminal). `nullptr` and the reason if it cannot start |
| Files | `os::file::stat`, `entry`, `make_folder`, `remove`, `rename` | Folders and names |
| | `os::file::open`, `read`, `write`, `seek`, `close` | A file a piece at a time, by its handle: a number. The system knows which process holds it and closes it if the process ends. Two per process, six in all |
| | `os::file::begin_save`, `finish_save` | Saving without putting the old file at risk, by a handle too; thrown away if the process ends first |
| | `os::file::open_files(pid)` | How many a process holds |
| | `os::file::read_file`, `write_file` | Whole files |
| | `os::file::has_drive`, `drives`, `drive_info` | The drives |
| Settings | `os::load_settings(program, ini)`, `os::save_settings(program, ini)` | `C:\.config\<program>.ini` |
| Machine | `os::restart()` | |
| | `os::stats(out)`, `os::fact(which, ...)`, `os::busy_us()` | Figures and facts for the tools that show them |

Dialogs that any program may open (message box, questions, the file
dialogs) stay where they are, in `core/shell/shell.hpp`: they are programs
of their own, not calls into the system.

All calls are for the UI task. The file calls may also be made from a
background job: the drives have their own lock.

## Which programs have moved

`tools/check_os_calls.py` holds the list and is one of the PC tests
(`os_calls`). It fails if a program on the list names the window manager,
the drives, the System port or the process table again, and prints how far
the move has got.

Moved (17 of 17): Cactus Patch, the colour chooser, Culebra, Defrag, the
file dialogs and the message box, Display, the Editor, the File manager,
Network, the Picture viewer, Run, Screen Saver, the settings files, Shut
Down, the Task Manager, the Terminal, the Trash.

Display, Screen Saver, Network and Shut Down were each half a window and
half something the system keeps going. The window is the program; the
other half is the system's: the screensaver's runner in `core/os`, the
network's tick and tray icon in `network_service.cpp`, the closing of
every program in `power.cpp`
([journal](journal/2026-11-17-os-calls-settings-tools.md)).

The Terminal was the last. It moved when it stopped being two things: the
window is a terminal emulator and asks for a session with
`os::open_session`; the TTY and the shell are the session, a process of
its own, which the system makes (`sessions.cpp`,
[journal](journal/2026-11-17-terminal-split.md)). The shell's commands are
not programs in this sense: they are handed what they may reach in
`sh::Env`, by the system.

## To move a program

1. Replace what it asks of `manager()`, `storage()`, `system_port()` and
   `proc::table()` with the calls above. If a call is missing, add it to
   `os.hpp` with a line in the table here; do not go around.
2. Add its file to `MOVED` in `tools/check_os_calls.py`.
3. Run the PC tests.
