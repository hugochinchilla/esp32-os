# Network shares: what the board could carry

## What was asked

The owner, 2026-11-19: "inicia una tarea de investigacion para los ficheros
en red". It is backlog item 80, written down the same day as an idea for
later: shared folders over the network "con un protocolo tipo samba, si se
puede, o algo mas ligero si no". Research only, no code: compare the
protocols against the board and say how a share would show in the File
manager.

## What was found

The whole comparison is in [`docs/network-shares.md`](../network-shares.md).
The parts worth telling:

- **The repository already has the right shape for it.** A drive is a
  `ports::Volume` mounted on a letter. A share written as one more volume
  shows in My Board and works in every program that opens files, with no
  change to them.
- **The repository also has the wrong assumption for it.** `fs::Storage`
  holds one lock for all drives for the whole of each call, which is right
  when a call takes milliseconds. A share that has gone away would hold
  that lock for seconds, and C: with it. This, the listing by index, the
  Trash and the 63-character names are most of the work, and none of it
  depends on which protocol is chosen.
- **SMB fits in the flash.** The backlog said to measure first. ESP-IDF
  was installed in the cloud container (`tools/setup-cloud-idf.sh`) and
  the firmware built twice from `main` at b303cb7: as it is, and with
  libsmb2 (commit 70e7a9b) and a throwaway function that connects, lists,
  reads, writes, renames and removes. The image went from 1,949,264 to
  2,069,552 bytes: **120,288 bytes more**, 62.0 % to 65.8 % of the 3 MB
  slot. Static internal RAM grew by 296 bytes.
- **The RAM that matters could not be measured here.** libsmb2 asks for
  memory in 231 places, nearly all small blocks, and on this board small
  blocks come from internal RAM, of which 49 to 131 KB is free depending
  on the moment. Only the board can say what a connected share costs.

## The options considered

As a client: SMB 2/3 with libsmb2; WebDAV with our own client on the
browser's HTTP code; NFS v3 (libnfs or our own); FTP; a protocol of our
own with a tool on the PC. As a server: FTP, WebDAV, SMB. SMB1, SFTP, TFTP
and 9P were turned down in a line each. The table, with what each needs on
the other computer, its code, flash, RAM, licence and logging in, is in the
document.

## What was chosen

Nothing: the choice is the owner's. The recommendation put to him is the
board as an SMB client with libsmb2, shown as a drive letter, begun with
one board turn that measures the internal RAM of a connected share; WebDAV
as the fallback if that figure is bad; a server on the board left for
later, as FTP if at all. Six questions for him are at the end of the
document, and the first is a row in
[Still open](../decisions/still-open.md).

## What was built and how it was tested

Documentation only. The measurement was a compile and link in the cloud
container; the probe and the copy of libsmb2 were removed afterwards and
are not in the repository. Nothing ran on the board, and nothing with
libsmb2 in it has ever run anywhere.

## What was learned

- libsmb2's build file at today's tip does not configure under ESP-IDF
  v5.5: `add_definitions command is not scriptable`, and then a macro
  `_U_` that only its other build files define. Eight lines of our own
  build file (the list of sources, five `NEED_` defines, `HAVE_CONFIG_H`
  and `_U_`) were enough. Whoever builds this starts there.
- 10 KB of the 120 is one table that turns Windows error numbers into
  words.
- Speed will not tell the protocols apart: copying to C: is held to the
  flash's 76 to 90 KB/s and reading from a share to the radio's 270 to
  400 KB/s, whatever carries the bytes.

## What is left

Backlog item 80 stays `todo` until the owner chooses. The next step, if
he takes the recommendation, is the board measurement.
