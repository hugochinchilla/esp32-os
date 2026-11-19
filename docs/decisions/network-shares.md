---
title: "Shared folders over the network"
date: 2026-11-19
by: owner
status: open
area: storage
summary: "An SMB client and a server, two optional services, in the firmware now behind a mount call and program files later; details open (backlog 80)"
---

# Shared folders over the network

Backlog item 80. The comparison is in
[`docs/network-shares.md`](../network-shares.md); journal:
`docs/journal/2026-11-19-network-shares-research.md`. Nothing is built.

Said by the owner on 2026-11-19, after reading the first findings:

- **Both directions**: the board reaches shared folders on other
  computers (a client) and other computers reach the board's files (a
  server). "Quiero que sea cliente y servidor, pueden ser dos programas."
- **Two programs**, one for each.
- **Optional services**, and outside the firmware if that can be done:
  "puede ser un programa externo o ha de ser parte del firmware? Estaría
  bien que fuera un servicio opcional."
- The first idea was "un protocolo tipo samba, si se puede, o algo mas
  ligero si no".

- **In the firmware now, program files later** (his choice on a card,
  2026-11-19: "Firmware ahora, ficheros después"). Both are built inside
  the firmware, as optional services, behind the same `mount` call a
  program file would use, and are moved out to files when item 77 or the
  store (item 78) lets files reach C: without the firmware carrying them.
  Not chosen: program files from the start with a board trial first (the
  agent's recommendation; three pieces the system lacks, and a loader
  that has never taken a large C library); in the firmware for good.

Recommended by the agent and not yet answered:

- SMB both ways with libsmb2 (LGPL 2.1 or later): +120 KB of flash for
  the client, +88 KB for the server, +140 KB for both, measured by
  compiling. The internal RAM of a connected share is not measured.
- Each a line of the start-up file, off by default, as `telnetd` and
  `vncd` are. Suggested names: `netdrive` and `smbd`.
- Moving them to program files later needs three things the system
  lacks: a program file that is a service with no window, the OS call
  that lets a program provide a drive, and sockets and `malloc` exported
  to program files (a change to "programs ask through the OS calls
  only"). The second is built now; the other two wait.
- A share is a `ports::Volume` on a drive letter; the file layer first
  learns that a drive can be slow (a lock per drive, timeouts).

Not chosen so far, and why, is in the comparison: WebDAV (the fallback if
SMB's RAM is too much), NFS, FTP (a program, not a drive), a protocol of
our own, SMB1, SFTP.
