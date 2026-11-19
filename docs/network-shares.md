# Shared folders over the network: what the board could carry

Research for backlog item 80 (the owner's idea, 2026-11-19: "carpetas
compartidas en red con un protocolo tipo samba, si se puede, o algo mas
ligero si no"). Nothing is built here: this is the comparison the owner
chooses from. What he has said since is in
[the decision](decisions/network-shares.md): he wants a client and a
server, as two programs, each an optional service, outside the firmware if
that can be done; and, on a card, in the firmware now and as program
files later. The story of how it was made is in
[the journal](journal/2026-11-19-network-shares-research.md).

## The short answer

- **SMB (what Samba and Windows speak) fits.** The library libsmb2 has an
  ESP-IDF build, does the logging in and the signing with its own code (no
  TLS library needed), and linked into today's firmware it adds **120 KB**
  to the image (measured here, compiled only): the firmware goes from 62 %
  to 66 % of its 3 MB slot. What it costs in internal RAM while a share is
  connected is the one figure still missing, and only the board can give it.
- **A share would be a drive letter.** The file layer already mounts drives
  behind one small interface (`ports::Volume`), so a share mounted as, say,
  `N:` shows in My Board and works in the File manager, the Open and Save
  boxes, the Terminal and Paste without those programs changing.
- **The work is mostly not the protocol.** The file layer assumes a drive
  answers in milliseconds. A network drive can take seconds or never
  answer, and today that would stop every program's file access, C:
  included. That has to be changed first whatever protocol is chosen
  (see "What the file layer needs").
- **A server fits too.** libsmb2 has a server side. Linked alone it adds
  88 KB; client and server together add 140 KB, because they share most
  of the library.
- **Both can be program files instead of parts of the firmware**, which
  is what the owner asked. Three things the system does not have yet are
  needed for that, all of them general (see "In the firmware or as
  program files").
- **Recommended:** an SMB client and an SMB server with libsmb2, two
  programs, each started by a line of the start-up file as `telnetd` and
  `vncd` are, the client first. Begin with one board turn that loads the
  client as a file and measures its RAM. If that goes badly, the
  fallbacks are the same code inside the firmware, and then WebDAV with
  our own small client.

## What the board has to give

All measured on the board on earlier days, from the journal entries named.

| | Figure | From |
| --- | --- | --- |
| Free internal RAM, desktop idle, Wi-Fi on, Bluetooth on | 131 KB (largest block 57 KB) | [Bluetooth](journal/2026-11-19-bluetooth-keyboard-mouse.md) |
| The same after the benchmark | 90 KB, lowest 49 KB, largest block 32 KB | the same |
| Free external RAM | about 5.9 MB | [App slots](journal/2026-11-19-app-slots-3-mb.md) |
| Firmware image | 1,949,264 bytes of a 3,145,728 byte slot (62 %) | built here from `main` at b303cb7 |
| Wi-Fi speed (the remote screen's raw picture) | 270 KB/s at 2 dBm, 370 to 408 KB/s at full power | [Found on the bench](decisions/found-on-the-bench.md) |
| Writing C: | 76 to 90 KB/s (a 293 KB save in 3.3 s; an update at 76 KB/s) | [LittleFS](journal/2026-11-16-littlefs-and-settings.md), [Updates](journal/2026-11-17-updates.md) |

Three things follow:

- **Internal RAM is the scarce thing**, not flash and not external RAM.
  `malloc` on the board gives blocks under 16 KB from internal RAM first
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`), and a library such as
  libsmb2 asks for many small blocks. Sockets' packet buffers already go
  to external RAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`).
- **Speed is not set by the protocol.** A file copied to C: is held to the
  flash's 76 to 90 KB/s whatever brings it (DOOM1.WAD, 4.2 MB, would take
  about 50 s). A file opened straight from the share is held to the
  radio's 270 to 400 KB/s, less while Bluetooth has its turns. SMB, WebDAV,
  NFS and FTP all move file contents as plain bytes over TCP; the
  differences between them are in the listing and in the round trips per
  small file, a few milliseconds each on a home network.
- **TLS stays out.** The browser (item 53) turned mbedTLS down for its
  internal RAM and certificates. None of the options below needs it on a
  home network; the ones that could use it (WebDAV over https, FTPS, SFTP)
  are judged without it, and SFTP, which is nothing without it, is out.

## How a share would appear

A share is a `ports::Volume` (`core/include/core/ports.hpp`) mounted on a
letter with `fs::Storage::mount`, exactly as `LittleFsVolume` is C: on the
board and a PC folder is C: in the simulator. From there up nothing knows
the difference:

- **My Board** lists it beside C: with a label ("docs on nas (N:)"), its
  size and free space (`os::file::drives`, `drive_info`). It wants a
  network-drive icon and a kind other than "Drive" and "RAM disk".
- **File manager, Open and Save boxes, Terminal** (`dir N:\`, `copy`) and
  **Paste** between C: and N: work as they are: Paste already copies piece
  by piece on a job of its own, and a Cut to another drive already copies
  and then removes (`core/src/shell/file_ops.cpp`).
- **Connecting** is a program of its own, as every settings tool
  ([dialogs are programs](decisions/dialogs-are-programs.md)): "Map
  Network Drive" asks for the server, the share, a name and a password and
  a letter, and keeps them in `C:\.config`. A `net use N: \\nas\docs`
  command in the Terminal would do the same.
- **Finding servers.** Typing `\\server\share` always works, by name
  (`nas.local`, the board already has mDNS) or by address. A list of
  servers to pick from is possible with what the firmware has, because
  Samba, macOS and most NAS boxes announce `_smb._tcp` over mDNS; listing
  the shares of a server is one more call in libsmb2. Windows PCs do not
  announce themselves that way (they use WS-Discovery), so a Windows PC is
  typed by name. A "Network Neighborhood" window is a later nicety, not a
  need.
- **In the simulator and the PC tests** the same volume works: the code
  that speaks the protocol is portable and can be tested against a real
  server on the PC. **In the web page** (item 33) a browser cannot open
  plain TCP connections, so there is no network drive there, except with
  WebDAV, which could go through the relay as the browser's pages do.

### What the file layer needs, whatever the protocol

Found by reading `core/src/fs/fs.cpp`, `ports.hpp` and the shell; none of
it is a problem on a flash drive, all of it is on a network one.

| What | Today | Why it matters | What to do |
| --- | --- | --- | --- |
| One lock for all drives | `fs::Storage` holds one lock for the whole of each call ([decision](decisions/file-system-lock.md)) | A share that has gone away makes a call wait for the timeout (seconds). Meanwhile no task can touch C: either: saves, settings, the desktop's icons | A lock per drive, or the network volume answering "busy" at once and working on a task of its own. The largest piece of the work |
| Calls that wait | A `Volume` call returns when it is done. The File manager reads a folder on its own task (`listing.cpp`) | A slow listing makes the File manager "not responding" for as long as it takes; the desktop goes on (item 26) | A timeout of a few seconds in the volume; the hourglass while a network folder is read; later the listing as a job |
| Entries asked by index | `entry(folder, index)` and `entries(folder, first, ...)` | Every protocol gives a folder as one stream from the start. Asking for entry 200 must not fetch the folder 200 times | The volume keeps the last folder it listed (in external RAM, 80 bytes an entry) |
| Names cut at 63 characters, paths at 159 | `DirEntry::name[64]`, `fs::kMaxPath = 160` | A PC's folders have longer names. A name that was cut cannot be opened again | Decide: leave out what does not fit (simplest), or widen both |
| The Trash | Delete moves to `C:\.trash` | From a share that is a download of the whole file into the flash | Delete on a network drive removes at once, after asking (as Windows 98 did) |
| Safe saves | `begin_save` writes `.~name.tmp` beside the file and `replace` swaps it in one step | The server must do the swap in one step | SMB, WebDAV and NFS can. FTP depends on the server |
| Change notices | Told only for changes made through `Storage` | What another computer changes is not seen | F5, or reading again when the window gets the focus. SMB can tell the client of changes; later |
| Four drives, six open files | `Storage::kMaxDrives = 4`, six handles in the machine | Enough for a start (C: and three shares) | Nothing now |
| A password to keep | The machine has no logins ([rejected](decisions/rejected.md)) | That was about logging in to Chinchilla. A share's password is like a Wi-Fi password | Decide where it is kept: in NVS beside the Wi-Fi ones, or in plain text in `C:\.config\shares.ini` |
| The radio going away | Wi-Fi off, out of range, the server asleep | Open files on the share die; a save half made is lost (the old file stays, by the safe save) | The drive stays in My Board as "not connected" and is connected again at the next use |

## The board as a client: the options

"Server needed" is what must run on the other computer for the board to
have something to connect to.

| | SMB 2/3 with libsmb2 | WebDAV, our own client | NFS v3 | FTP, our own client | Our own protocol |
| --- | --- | --- | --- | --- | --- |
| What it is | What Windows, Samba, macOS and every NAS call a shared folder | Files over HTTP: `PROPFIND` lists, `GET` and `PUT` read and write, `MOVE`, `DELETE`, `MKCOL` | The Unix way to share a folder | The 1990s way to fetch files | A small program on the PC that serves a folder in the `Volume` port's own terms |
| Server needed | None to install on Windows, macOS or a NAS (turn sharing on); Samba on Linux | One to install or switch on: a NAS's WebDAV, `rclone serve webdav`, `dufs`, Apache, GNOME's "Personal File Sharing" | Linux or a NAS, with an export line. Windows Home cannot serve it | One to install (`vsftpd`, FileZilla Server, a NAS's FTP) | Ours, always (`tools/share.py`), on a PC that has Python |
| Code | libsmb2: 37,500 lines of C, a ready ESP-IDF component | About 1,000 to 1,500 lines of ours on `core/www/http.cpp` (396 lines today), plus reading the XML of a listing | libnfs: 27,800 lines, no ESP32 build of its own; or about 1,500 lines of ours for the dozen calls needed | About 600 lines of ours | About 400 lines each side |
| Flash | **+120 KB measured** (see below) | 10 to 20 KB, estimated | libnfs not measured, likely near libsmb2's; ours 15 to 20 KB, estimated | 5 to 10 KB, estimated | under 5 KB, estimated |
| Internal RAM | Not known. Static: 0.3 KB measured. While connected: many small blocks, plus a 4 to 8 KB stack if it gets a task. **To measure on the board** | Small and ours to place: one connection, a buffer in external RAM | As SMB for libnfs; small for ours | Small; two connections at a time (commands and data) | Smallest |
| Licence | LGPL 2.1 or later | Ours | libnfs: LGPL 2.1 or later. Ours: ours | Ours | Ours |
| Logging in | Name and password (NTLMv2), signed packets; all in the library, no TLS | None, or a name and password sent almost in the clear (Basic). Digest is more code | None: the server trusts the address and a user number | Name and password in the clear | Whatever we write; none to start |
| Listing a folder | One exchange, binary, with sizes and dates | One exchange, XML, about 400 bytes an entry on the wire | One exchange, binary | A second connection and text whose layout differs by server (`MLSD` where the server has it) | One exchange |
| Reading part of a file (seek) | Yes | Yes (`Range`) | Yes | Restart from an offset only: each seek is a new transfer | Yes |
| Writing part of a file | Yes | No: a file is put whole. Enough for safe saves, which write whole files | Yes | Append and restart only | Yes |
| Swap a file in one step | Yes | Yes (`MOVE` with overwrite) | Yes | Depends on the server | Yes |
| In the web page (item 33) | No | Possible through the relay | No | No | No |
| How 1998 it feels | Network Neighborhood itself | Web Folders arrived with Internet Explorer 5 in 1999; near enough | Unix workstations | Very, but as a program (an FTP client), not as a drive | Not at all |

### SMB with libsmb2

[libsmb2](https://github.com/sahlberg/libsmb2) is a client for SMB 2.02 to
3.1.1 written for small machines: its own README lists the ESP32 beside the
PlayStation 2 and 3, the Dreamcast, the Amiga and the Raspberry Pi Pico W.
It brings its own MD4, MD5, SHA and AES, so logging in (NTLMv2) and signing
need nothing else in the firmware. It has calls that wait and calls that do
not (a file descriptor to `select` on, which would fit the `inetd` task),
and a timeout.

**Measured here** (ESP-IDF v5.5, `main` at b303cb7, libsmb2 at 70e7a9b,
compiled and linked in the cloud, never run): a throwaway function that
connects to a share with signing on, lists a folder, and opens, reads,
writes, seeks, renames, removes and makes a folder, called from `app_main`
so that the linker keeps it.

| | Before | With libsmb2 | More |
| --- | ---: | ---: | ---: |
| Image (`esp32-os.bin`) | 1,949,264 | 2,069,552 | 120,288 bytes |
| Code in flash | 1,279,852 | 1,362,576 | 82,724 |
| Constants in flash | 539,388 | 576,740 | 37,352 |
| Internal RAM, static | 152,099 | 152,395 | 296 |
| Share of the 3 MB slot | 62.0 % | 65.8 % | |

The library's own archive is 82.5 KB of that; the rest is what it draws in
from the C library and lwIP. 10 KB of it is the table that turns Windows
error numbers into words, which could be cut.

What was learned doing it:

- The library's build file at today's tip does not configure under ESP-IDF
  v5.5 (`add_definitions command is not scriptable`, then a macro `_U_`
  that only its other build files define). A build file of ours, eight
  lines, fixed both. The component would be pinned to a commit and kept
  in `firmware/os/components/` or fetched as LittleFS is.
- Code and constants run from external RAM on this board
  (`CONFIG_SPIRAM_XIP_FROM_PSRAM`), so the 120 KB of flash is also 120 KB
  of the 8 MB of external RAM. That is not a worry.

What is not known:

- **The internal RAM while connected.** The library has 231 places that
  ask for memory, nearly all small, and small blocks come from internal
  RAM here. A guess from reading it is 10 to 30 KB; a guess is not a
  measurement, and the largest free internal block after the benchmark is
  32 KB. If it is too much, the library's memory can be pointed at
  external RAM in our build of it, at some cost in speed.
- **Whether its work disturbs the picture.** AES and SHA over every packet
  when signing is on is processor work on the task that asks, not a lock
  or a masked interrupt, so it should not cause missed refills; `dbg.py
  misses` after a large copy is the check.

About the servers: Windows 11 since version 24H2 requires signing on every
SMB connection and no longer lets a guest in without a password
([Microsoft's note](https://techcommunity.microsoft.com/blog/filecab/accessing-a-third-party-nas-with-smb-in-windows-11-24h2-may-fail/4154300)).
libsmb2 signs, so that is met with a real account's name and password; it
means a share on a Windows PC cannot be reached "as guest". Samba and NAS
boxes allow guests if set so.

About the licence: LGPL lets the library be linked into firmware as long as
whoever receives the firmware can replace the library, which publishing
the whole source (as this repository does) satisfies. The repository has no
licence file of its own yet; LGPL code in it does not force one, but the
owner should know the firmware would carry an LGPL part. It is the first
third-party code with a condition stronger than "keep the notice".

### WebDAV with our own client

The lightest thing that is still a standard a PC or NAS can serve. The
browser's HTTP code (`core/www/http.cpp`) already makes requests that
never wait, through `os::tcp`, and is tested on the PC against scripted
answers; it speaks `GET` only and would learn `PROPFIND`, `PUT`, `MOVE`,
`DELETE` and `MKCOL`, sending a body, and a small reader for the XML of a
listing. ESP-IDF's own `esp_http_client` knows those methods too, but it
is not portable code and draws in its TLS layer; ours is the better base.

Against it: somebody has to run a WebDAV server, which no home computer
does by default. Windows can still connect to one but Microsoft deprecated
its WebDAV client in November 2023 and no longer starts it by default, so
the "it is a standard" argument is weaker every year. Files are written
whole, never in part: fine for the Editor and Paste, which write whole
files, wrong for a program that would update a large file in place (none
does today). And a password, if used, crosses the network almost in the
clear.

### NFS

Light on the wire and exact in meaning (it was made to be a drive), with a
handle per file that survives a reconnect. But only Linux and NAS boxes
serve it; version 3 needs a second protocol (mount) and a port mapper to find both;
the server trusts whatever user number the client claims. libnfs (the
same author as libsmb2) has no ESP32 build. It would be chosen only if the
owner's files live on a Linux machine or a NAS and he would rather add an
export line than a Samba share.

### FTP

Easy to write and of the period, but it is not a drive: no reading from
the middle of a file without a new transfer, listings that are text for
people, two connections per transfer. It would make a good **program**
("FTP", two panes, 1990s to the bone) and a poor drive letter.

### Our own protocol

A `tools/share.py` on the PC that answers the `Volume` port's calls one
for one. The least code, the least RAM, tests itself on the PC, and can
tell the board of changes. But it is not a shared folder in the sense the
owner meant: nothing but our tool can serve it. It is the fallback if
internal RAM turns out to rule everything else out, and it is close to
what `dbg.py` already does over the debug link (it lists, reads and
writes files).

### Turned down without a table row

- **SMB1/CIFS** (what Windows 98 really spoke): switched off in Windows
  and Samba for years; libsmb2 does not speak it.
- **SFTP**: needs an SSH library and its cryptography; heavier than SMB
  with no gain at home.
- **TFTP**: no listing.
- **9P** (Plan 9's): simple and exact, but no home computer serves it.

## The board as a server

The other direction: a PC opens the board's C: as `\\chinchilla\c`. The
owner wants it (2026-11-19: "quiero que sea cliente y servidor, pueden
ser dos programas"). It also answers the DOOM1.WAD problem: a file is
dragged to the board from the PC's own file manager.

| | What a PC needs | Cost on the board | Notes |
| --- | --- | --- | --- |
| SMB server with libsmb2 | Nothing: Windows, macOS and Linux open `\\chinchilla` | **+88 KB of flash measured** alone, +19 KB beside the client; about 1,000 to 1,500 lines of ours that answer each request from our files | The real thing. See below |
| FTP server | An FTP client; Windows Explorer and FileZilla | About 600 lines of ours | No password, or one in the clear, like telnet here. Cheap and of the period, but a program on the PC, not a drive |
| WebDAV server | Finder, GNOME Files, Windows (deprecated) mount it as a drive | About 1,000 lines on the web server the remote screen has (`core/net/web.cpp`) | The clients are picky; each needs testing |

### An SMB server with libsmb2

The library takes the connections, the logging in and the signing, and
calls a table of our functions, one per request: connect to a share, open,
close, read, write, list a folder, ask about a file, rename, remove. Its
own example (`examples/smb2-server-sync.c`, 550 lines) serves a pretend
disk; ours would answer from `fs::Storage`.

**Measured here**, as the client was (compiled and linked, never run):

| | Image | More than today |
| --- | ---: | ---: |
| Today | 1,949,264 | |
| Client only | 2,069,552 | 120,288 |
| Server only (an empty table of functions) | 2,037,504 | 88,240 |
| Client and server | 2,088,848 | 139,584 |

Static internal RAM: 1.6 KB more with the server. Our own functions that
answer the requests are not in these figures (some 10 to 15 KB more).

What to know before choosing it:

- **It runs its own loop** (`smb2_serve_port` waits on its sockets and
  returns when told to stop), so it is a task of its own with its own
  stack, not one more service inside `inetd`. That fits "a program of its
  own" and "only real tasks are listed".
- **It did not link under ESP-IDF as it is**: `gethostname` is missing
  there. One function of ours fixed it. Nobody seems to have run this
  server on an ESP32 before us; the client is the well-trodden half.
- **Windows 11 will want a name and a password.** Since 24H2 it signs
  every connection and refuses guests, so the server needs one account
  (a name and a password in its settings file). Linux and macOS can
  connect as guests if the server allows it. The machine still has no
  logins of its own; this is the share's password.
- **Each kind of PC asks in its own way.** Windows Explorer, Finder and
  GNOME Files each send requests the others do not (what a volume is
  called, how much is free, alternate streams, change notices). Expect a
  round of testing and fixing per kind of PC; start with the owner's.
- **It is a second writer of C:.** The one lock already makes single
  calls safe. A file a program has open while the PC replaces it, and the
  six open files in the whole machine, need rules.
- **Each connected PC costs RAM** for as long as it stays connected: a
  context, its buffers, a socket. Two connections at most to start.
- **Finding the board**: it already answers to `chinchilla.local`;
  announcing `_smb._tcp` over mDNS puts it in Finder's and GNOME's
  network lists. Windows' list needs WS-Discovery, which is more code;
  typing `\\chinchilla` works without it.

## In the firmware or as program files

The owner's question, 2026-11-19: "puede ser un programa externo o ha de
ser parte del firmware? Estaría bien que fuera un servicio opcional".

**Optional is free either way.** libsmb2 keeps almost nothing while it is
not used (0.3 KB of static RAM for the client, 1.6 KB for the server).
What costs internal RAM is a connected share or a listening server, so a
service that is off costs nothing that matters, built in or not. Each
would be a line of the start-up file, off by default, with a tick box in
Services and `service NAME start` and `stop`, exactly as `telnetd` and
`vncd` ([decision](decisions/services-start-up-file.md)). Suggested
names: `smbd` for the server, `netdrive` for the client.

**It can be a program file**, as Chess and Culebra are (`.app`, loaded
from `C:\Programs` into external RAM, item 27). What that buys: the
firmware does not carry it, somebody who does not want it deletes a file,
it can come from the store (item 78), and the LGPL library is a file of
its own that can be replaced, which is the cleanest way to meet that
licence. Three things are missing today, and none is special to SMB:

| Missing | Today | What is needed |
| --- | --- | --- |
| A program with no window | A loaded program is started from the Start menu, has a window, and is unloaded when its last window closes | A kind of program file that init starts from a line of the start-up file and that stays until stopped: a service in a file. Screensavers as files (item 79) ask for nearly the same |
| A program that provides a drive | Drives are mounted by the firmware at start-up; programs only use them | An OS call, `os::file::mount(letter, the drive's functions)` and `unmount`. The file layer then calls into the loaded file from other tasks, so stopping the service must first close what is open on the drive |
| Sockets for a loaded program | A loaded program reaches only what the firmware exports by name: the OS calls and a short list of libc. Its network is `os::tcp`, four outgoing connections that never wait | libsmb2 asks for 50 names from outside itself (listed here by linking it): `malloc`, `calloc`, `realloc`, `free`, `strdup`, the BSD sockets (`socket`, `connect`, `bind`, `listen`, `accept`, `select`, `poll`, `read`, `write`, `close`, `getaddrinfo`, `setsockopt`...), `time`, a random number, and string functions that are mostly exported already. Either those are exported to program files (a new rule: today programs get OS calls only), or libsmb2's one socket file is rewritten on `os::tcp`, which the server cannot use because `os::tcp` does not listen |

Three more things to know:

- **Two files carry the library twice.** The loader has no libraries
  shared between programs. Client and server as two files are about 120
  and 90 KB on C: (and in external RAM while both run) against 140 KB as
  one. That is cheap on a 9 MB drive and 8 MB of external RAM. One file
  that does both is the other way; so is a shared library file, which
  would be a new thing for the loader to learn.
- **The firmware does not shrink until item 77 or 78.** Today the
  programs that come with the system are carried inside the firmware and
  copied to `C:\Programs` at start-up, so a bundled `.app` saves no
  flash. Once files reach C: by the flash script (item 77, being built)
  or the store (item 78), it does.
- **Not known: whether the loader takes it.** The largest program file
  today is Chess, 52 KB of our own C++. libsmb2 is 120 KB of somebody
  else's C, built as position-independent code for the first time. The
  loader's limits (1 MB a file) are far away, but only a try says
  whether it loads and runs.

The work that stays in the firmware in every case is the file layer's
(a lock per drive, timeouts, the listing kept): a drive that can be slow
is the system's business, not the driver's.

| | In the firmware, a service line each | Program files |
| --- | --- | --- |
| Optional | Yes: off costs 0.3 to 1.6 KB of RAM, and 140 KB of flash always | Yes: off costs nothing; deleted costs nothing |
| Works with what exists | Yes | After the three missing pieces above |
| Extra work | None | About three days, all of it reusable (items 78 and 79 need the first piece, any later driver needs the second and third) |
| Risk | Low | The loader and a large C library have not met |
| Licence | LGPL part inside the firmware | LGPL part in a file of its own |

## Recommendation

1. **SMB both ways with libsmb2**: `netdrive`, the client, which shows a
   share as a drive letter, and `smbd`, the server, which shows C: to the
   PCs. Two programs, each a line of the start-up file, off until ticked
   in Services.
2. **In the firmware now, program files later** (the owner's choice,
   2026-11-19; the agent had recommended program files from the start
   with a board trial first). Both are built inside the firmware behind
   the `mount` call a program file would use, and move out to files when
   item 77 or the store lets files reach C: without the firmware
   carrying them.
3. **Measure first.** One board turn with a throwaway command that
   connects to a share, lists a folder and reads 1 MB: free internal RAM
   and largest block before, during and after; speed; `dbg.py misses`.
   It needs a share to connect to. **If the RAM is too much** (over
   about 30 KB), try the library's memory in external RAM; if that
   fails, WebDAV with our own client.
4. **Then the file layer** (a lock per drive, timeouts, the listing kept,
   delete without the Trash), tested on the PC with a volume that is slow
   and fails on purpose. Most of the client's work, and none of it
   depends on the protocol.
5. **Then the client** with "Map Network Drive", tested on the PC against
   a Samba server, then on the board.
6. **Then the server**, tested from the owner's own PC first.

Size: the backlog says L for the idea; with a server it is two L items.
Roughly: one day for the measurement, two to three for the file layer, two to three
for the client and its program, four to five for the server with a round
of testing against each kind of PC.

## Questions for the owner

Answered on 2026-11-19: client and server, two programs, optional
services, in the firmware now and program files later.

1. (Answered: in the firmware now, program files later.)
2. For the later move: may program files be given the sockets and
   `malloc` by name? It is a change to the rule that programs ask
   through the OS calls only. Not needed yet.
3. Is the server SMB too, as recommended, or is FTP enough for that
   direction?
4. What would the board connect to, and what would connect to it: a
   Windows PC, a Linux laptop with Samba, a NAS? It decides what the
   measurement and the tests run against.
5. Is LGPL code acceptable, in the firmware or in a file beside it?
6. Where are passwords kept (a share's, and the server's own account):
   NVS beside the Wi-Fi ones, or plain text in `C:\.config`?
7. Names longer than 63 characters: leave them out of listings, or widen
   names and paths everywhere?

## Sources

- libsmb2, <https://github.com/sahlberg/libsmb2>, read at commit 70e7a9b
  (2026-11-19): `README`, `COPYING`, `lib/`, `include/esp/config.h`.
- libnfs, <https://github.com/sahlberg/libnfs>, read at commit 5c48d27:
  `README`, `COPYING`.
- ESP-IDF v5.5, `components/esp_http_client/include/esp_http_client.h`.
- Microsoft, "Accessing a third-party NAS with SMB in Windows 11 24H2 may
  fail",
  <https://techcommunity.microsoft.com/blog/filecab/accessing-a-third-party-nas-with-smb-in-windows-11-24h2-may-fail/4154300>.
- Microsoft's deprecation of the WebClient (WebDAV) service, November
  2023, as reported at
  <https://www.neowin.net/news/microsoft-deprecates-three-more-windows-features>.
- An ESP-IDF WebDAV client that shows the method set is enough in
  practice, <https://github.com/nopnop2002/esp-idf-webdav-client>.
- libsmb2's server side: `include/smb2/libsmb2.h` (`struct smb2_server`,
  `smb2_serve_port`) and `examples/smb2-server-sync.c`.
- This repository: `tools/gen_exports.py`, `tools/build_programs.sh`,
  `docs/decisions/programs-loaded-from-files.md`,
  `core/include/core/ports.hpp`, `core/include/core/fs/fs.hpp`,
  `core/src/fs/fs.cpp`, `core/src/shell/file_ops.cpp`,
  `core/src/shell/listing.cpp`, `core/include/core/www/http.hpp`,
  `core/include/core/os/tcp.hpp`, `firmware/os/sdkconfig.defaults`.
