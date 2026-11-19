# Shared folders over the network: what the board could carry

Research for backlog item 80 (the owner's idea, 2026-11-19: "carpetas
compartidas en red con un protocolo tipo samba, si se puede, o algo mas
ligero si no"). Nothing is built and nothing is decided here: this is the
comparison the owner chooses from. The story of how it was made is in
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
- **Recommended:** the board as an SMB client with libsmb2, started with a
  one-day board measurement of the RAM. If that measurement is bad, the
  fallback is WebDAV with our own small client on the HTTP code the browser
  already has. A server on the board (so that a PC sees C:) is a separate,
  later question; if it is wanted, FTP is the cheap way and SMB the
  expensive one.

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

The other direction: a PC opens the board's C:. It answers a different
need (putting files on the board, the DOOM1.WAD problem) that item 77
(files copied when flashing) and `dbg.py put` already cover in part.

| | What a PC needs | Cost on the board | Notes |
| --- | --- | --- | --- |
| FTP server | Any FTP client; Windows Explorer and FileZilla | About 600 lines of ours as one more `inetd` service, as `telnetd` is | No password, or one in the clear, like telnet here. The cheap, period answer |
| WebDAV server | Finder, GNOME Files, Windows (deprecated) mount it as a drive | About 1,000 lines on the web server the remote screen has (`core/net/web.cpp`) | The clients are picky; each needs testing |
| SMB server | Nothing: the board shows up as `\\chinchilla` | libsmb2 has a server side too (an example in its tree, password logins only). Much more code in use, long-lived connections, and Windows 11 insists on signing and a password | The real thing and by far the most work; not for a first step |

All of them need the same care as any second writer of C:: the one lock,
and files that programs have open.

## Recommendation

1. **SMB client with libsmb2, shown as a drive letter.** It is what the
   owner asked for, it needs nothing installed on a Windows PC, a Mac or a
   NAS, the flash cost is known and small (120 KB of 3 MB), and the
   library is made for boards like this one.
2. **Measure before building.** One board turn with a throwaway command
   (`dbg.py` or the Terminal) that connects to a share, lists a folder and
   reads a 1 MB file: free internal RAM and largest block before, during
   and after; speed; `dbg.py misses`. It also needs a share to connect to
   (see the questions). If the internal RAM is under about 30 KB, go on;
   if not, try the library's memory in external RAM; if that fails, fall
   back to WebDAV.
3. **Then the file layer** (a lock per drive, timeouts, the listing kept,
   delete without the Trash), tested on the PC with a volume that is slow
   and fails on purpose. This is most of the work and none of it depends
   on the protocol.
4. **Then the volume and "Map Network Drive"**, tested on the PC against a
   Samba server, then on the board.
5. **A server on the board is left for later**, as FTP if at all.

Size: the backlog says L, and that still looks right: about a day for the
measurement, two to three for the file layer, two to three for the volume
and the program.

## Questions for the owner

1. SMB with libsmb2 as recommended, or one of the lighter ones?
2. What would the board connect to: a Windows PC, a Linux laptop with
   Samba, a NAS? It decides what the measurement and the tests run
   against, and whether guests or a password come first.
3. Is LGPL code in the firmware acceptable?
4. Where is a share's password kept: NVS beside the Wi-Fi ones, or plain
   text in `C:\.config\shares.ini`?
5. Names longer than 63 characters: leave them out of listings, or widen
   names and paths everywhere?
6. Is the other direction (a PC opening the board's C:) wanted too, and
   is FTP enough for it?

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
- This repository: `core/include/core/ports.hpp`, `core/include/core/fs/fs.hpp`,
  `core/src/fs/fs.cpp`, `core/src/shell/file_ops.cpp`,
  `core/src/shell/listing.cpp`, `core/include/core/www/http.hpp`,
  `core/include/core/os/tcp.hpp`, `firmware/os/sdkconfig.defaults`.
