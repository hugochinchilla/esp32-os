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
| Jobs | `os::start_job(spec)` | Work on a task of its own for the asking process (`core/job/job.hpp`), while the desktop and the program's window go on: `work` runs on the new task, `done` afterwards where the program's events run. The process is not ended before its jobs have, but the program's object goes when its window closes: what the work reads must outlive the window. Where there are no tasks the work is done inside the call (Chess, the Editor's save) |
| | `os::job_rest(ms)` | From a job's own task: gives the processor away for about `ms`. Work that computes for seconds calls it a few times a second |
| Memory | `os::allocate(bytes)` | Zeroed memory, counted as the asking process's (Task Manager, `top`) |
| | `os::release(block)` | Gives it back; taken off the count of whoever asked for it |
| | `os::memory()` | The same two as a Memory port, for code that takes one |
| | `os::blocks_out()`, `os::bytes_out()` | What is out over all processes (the tests use it to find leaks) |
| Windows | `os::show(window)` | Opens the window as the asking process's, or brings it to the front |
| | `os::activate(window)`, `os::move(window, frame)` | |
| | `os::work_area()`, `os::centred(w, h)` | The screen without the taskbar; a frame in the middle of it |
| | `os::menu_open()` | A menu is open (a game pauses) |
| | `os::open_menu(menu, x, y)` | A free-standing menu |
| | `os::ui::menu_at(window, control, x, y, menu)` | The program's menu at a point of one of its controls: where the right button went down (the File manager) |
| | `os::blocker(window)`, `os::in_front(window)` | The dialog in a window's way; whether it is the one in use |
| | `os::main_window(pid)`, `os::window_title(id)`, `os::window_icon(id)`, `os::bring_forward(id)` | Another process's window, as a number (`os::WindowId`) and never as its object: its title and icon, and bringing it or its dialog to the front. A closed window's number answers nothing |
| | `os::caret_moved()` | The text cursor of the window in use has moved: shown at once (the Terminal) |
| Windows by number | `os::ui` (`core/os/ui.hpp`): windows, controls and their events | A program asks for a window and controls and gets numbers; written through the thin classes of `core/app/app.hpp` (item 37) |
| | `os::ui::icon_view`, `set_items`, `item_at`, `reset_view`; events Drag, DragOver, Dropped, RightClick | The view of icons with names (File manager), and files dragged out of it and dropped on a window |
| | `os::ui::selected_count`, `is_selected`, `highlight` | Several entries of an icon view picked at once (item 55): how many, whether this one is, and one more shown as picked without being it (the folder a dragged file would go into). `selected()` is the one picked last |
| | `os::ui::draw::fill`, `text`, `text_scaled`, `blit`, `part`, `dent`, ... | Drawing while a Paint event is answered, on a number good only until it returns (`app::Canvas`) |
| | `os::begin_drag(path)` | A file starts being dragged |
| | `os::add_to_drag(path)`, `os::drops_to_come()` | One more file carried by the drag just begun (item 55); and, while a Dropped event is answered, how many more of the same drag follow, so that the folder is read again and the error box shown once, after the last |
| | `os::Busy busy;` | The hourglass over the whole screen until the end of the block |
| | `os::ui::set_hourglass(window, on)`, `os::ui::hourglass(window)` | The hourglass on demand (item 49): over the window's client area while it is on, and the window answers as before. A switch the program turns off itself; it goes with the window |
| Desktop | `os::set_backdrop(backdrop)` | The desktop's colour and pattern (Display) |
| Desktop | `os::set_theme(index)` | The look of windows, buttons and the taskbar, a row of the table in `core/gfx/theme.hpp` (Display); the whole screen is painted again |
| | `os::video_mode_count()`, `os::video_mode_at(index)`, `os::video_mode()`, `os::set_video_mode(index)` | The screen's modes (item 68), each a size with its timing, and changing to another (Display): done once the event has been answered, then every window is brought onto the new screen and all is painted again |
| | `os::set_screen_saver(animation, seconds)` | Which animation and after how long; -1 for none (Screen Saver) |
| | `os::preview_screen_saver(animation)`, `os::last_saver_run()` | Shows one now; how the last run went |
| | `os::launch(program)` | Starts one of the system's own programs by name, or brings it forward |
| | `os::shut_down()` | Asks every program to close and shows the power-off screen; false if one called it off |
| Network | `os::network()` | The Wi-Fi service, or nothing on a board with no radio; asked for each time, not kept |
| Sessions | `os::open_session(device, why)` | A command shell on a TTY of its own, as a process `sh`, child of the one asking, which is its device (the Terminal). `nullptr` and the reason if it cannot start |
| Files | `os::file::stat`, `entry`, `entries` (a run of a folder in one pass), `make_folder`, `remove`, `rename` | Folders and names |
| | `os::file::open`, `read`, `write`, `seek`, `close` | A file a piece at a time, by its handle: a number. The system knows which process holds it and closes it if the process ends. Two per process, six in all |
| | `os::file::begin_save`, `finish_save` | Saving without putting the old file at risk, by a handle too; thrown away if the process ends first |
| | `os::file::open_files(pid)` | How many a process holds |
| | `os::file::read_file`, `write_file` | Whole files |
| | `os::file::has_drive`, `drives`, `drive_info` | The drives |
| Connections | `os::tcp::connect(host, port)`, `state`, `failure`, `read`, `write`, `close` (`core/os/tcp.hpp`) | An outgoing connection as a handle the system keeps and closes when the process ends. No call waits: the dialling is a background job |
| | `os::tcp::available()`, `open_connections(pid)` | Whether the machine has a way out; how many a process holds |
| Clipboard | `os::clipboard::set_files(folder, names, count, cut)` | Copy and Cut for files (item 54): their full paths go on the clipboard, which is the one the text uses and holds text or files, never both |
| | `os::clipboard::file_count()`, `file(index, out, capacity)`, `files_cut()`, `clear()` | What Paste asks: which files, and whether they were cut. `os::ui::clipboard_size()` is of the text, 0 while files are there |
| Settings | `os::load_settings(program, ini)`, `os::save_settings(program, ini)` | `C:\.config\<program>.ini` |
| Sound | `os::play_sound(which)` | One of the system's sounds (`core/sound/sound.hpp`: Boot, Close, Alert, Error, Bell, and the games' and the Trash's: ChessMove, ChessCheck, ChessMate, SnakeEat, SnakeOver, Crumple) from its start; it returns at once, and a new one takes the place of the one playing. Nothing on a machine with no sound output (the board, for now) |
| | `os::beep(steps, count)` | The beeper: steps of a pitch in Hz and a time in ms, 0 Hz for silence; a voice of its own beside the recordings |
| | `os::sound_playing()` | Whether anything is still to be heard |
| Machine | `os::restart()` | |
| | `os::stats(out)`, `os::fact(which, ...)`, `os::busy_us()` | Figures and facts for the tools that show them |

Dialogs that any program may open (message box, questions, the file
dialogs) stay where they are, in `core/shell/shell.hpp`: they are programs
of their own, not calls into the system.

All calls are for the UI task, but `os::job_rest`, which is for a job's.
The file calls may also be made from a background job: the drives have
their own lock. A job cannot ask the time (`os::now_us()` is the UI
task's): the program sets a timer and tells its job.

## Which programs have moved

`tools/check_os_calls.py` holds the list and is one of the PC tests
(`os_calls`). It fails if a program on the list names the window manager,
the drives, the System port or the process table again, and prints how far
the move has got.

Moved (21 of 21): About, Cactus Patch, the colour chooser, Culebra, Defrag,
the file dialogs and the message box, Display, the Editor, the File
manager, Network, the Picture viewer, Run, Screen Saver, the settings
files, Shut Down, the Task Manager, the Terminal, the Trash, Vizcacha (the
web browser), Welcome, the Widget gallery.

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
