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
| | `os::Busy busy;` | The hourglass until the end of the block |
| Files | `os::file::stat`, `entry`, `make_folder`, `remove`, `rename` | Folders and names |
| | `os::file::open`, `read`, `write`, `close` | A file a piece at a time |
| | `os::file::begin_save`, `finish_save` | Saving without putting the old file at risk |
| | `os::file::read_file`, `write_file` | Whole files |
| | `os::file::has_drive`, `drives`, `drive_info` | The drives |
| Settings | `os::load_settings(program, ini)`, `os::save_settings(program, ini)` | `C:\.config\<program>.ini` |
| Machine | `os::restart()` | |

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

Moved (7 of 16): Cactus Patch, Culebra, Defrag, the Editor, Run, the
settings files, the Trash.

Still to move (9): the dialogs, Display, the File manager, Network, Shut
Down and Ctrl+Alt+Del, Screen Saver, the Task Manager, the Terminal, the
Picture viewer. Several of these need calls that do not exist yet: reading
the process list and sending a signal (Task Manager, Terminal), the
background and the screensaver (Display, Screen Saver: these are the
desktop's own business and wait for item 21), a decoder that takes the
file calls instead of the drives (Picture viewer).

## To move a program

1. Replace what it asks of `manager()`, `storage()`, `system_port()` and
   `proc::table()` with the calls above. If a call is missing, add it to
   `os.hpp` with a line in the table here; do not go around.
2. Add its file to `MOVED` in `tools/check_os_calls.py`.
3. Run the PC tests.
