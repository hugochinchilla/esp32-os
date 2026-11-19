# Ideas for later: a store, screensavers, network shares

## What was asked

The owner, 2026-11-19, in Spanish, asked for these ideas to be written down for
later:

- A software store: "nosotros compilamos las apps y las publicamos como
  paquetes en github, y una app integrada permite descargar apps desde los
  repos y actualizarlas, asi podemos aligerar el tamaño del firmware no
  incluyendo todas las apps dentro".
- Screensavers as separate `.scr` files that can be deleted from the disk.
- Rethinking the first install: the board's flash script could copy the
  programs to C: itself, without embedding them in the firmware first.
- That last point could also solve the DOOM problem: Doom needs the owner to
  copy `DOOM1.WAD` to C: by hand.
- Later, shared folders over the network with a Samba-like protocol (if it can
  be done), or something lighter if not.

For now only the flash-script point is to be built.

## What was done

Nothing is built in this change; it only writes the ideas down.

- Backlog items 78 (software store), 79 (screensavers as `.scr` files) and 80
  (shared folders over the network), all `todo`, none started.
- The flash-script idea is not duplicated: it is being built in another thread
  and takes number 77, held in the backlog's note on the next free number. Items
  78 and 79 depend on it, because both need programs that live as files on C:
  and not inside the firmware.

## Notes for whoever starts them

- The store (78) and the screensavers (79) share one question: what a program
  file on C: is, and how the system finds and starts it. Chess, Culebra and
  Cactus Patch are already kept as program files (items 50 and the earlier
  games), which is the starting point.
- The store needs a way to check what it downloads (a hash from the package's
  listing, at least) and a version to compare for updates. The owner has not
  chosen any of this; present options when it starts.
- Network shares (80) should begin with a measurement: an SMB client is large
  next to what is left of the internal RAM after Wi-Fi and Bluetooth (see
  item 51's 55 KB). A lighter protocol may be the realistic choice.

Not tested: documentation only.
