# Debug protocol

How a computer talks to the board over the serial port: screenshots, injected
keyboard and mouse events, and status. The board side is in `core/src/dbg/`,
the computer side is `tools/dbg.py`.

Version 1. Working on the board since 2026-11-16.

## Link

- UART0, through the USB-C socket marked UART. 921,600 baud, 8 data bits, no
  parity, 1 stop bit. 115,200 baud is the fallback (see
  `firmware/os/sdkconfig.defaults`).
- Log text and commands share the port. After start-up, everything the board
  sends is a frame, log lines included, so a log line can never land in the
  middle of a screenshot. Text the chip prints before the OS starts (boot
  messages) is not framed; the tool shows it as plain text.

## Frames

On the wire, a frame is a zero byte, the encoded frame, and another zero byte:

```
00  <COBS-encoded frame>  00
```

COBS encoding removes every zero from the frame, so a zero only ever appears
between frames. Anything between two zeros that does not decode to a valid
frame is dropped (the board) or shown as text (the tool). Because of that, a
damaged or lost byte costs one frame and the link recovers on the next.

Before encoding, a frame is:

| Bytes | Field |
| --- | --- |
| 1 | Type |
| 1 | Sequence number, chosen by the computer and repeated in the reply |
| 0 to 1024 | Payload (at most 256 towards the board) |
| 2 | CRC-16/CCITT-FALSE of everything before it, low byte first |

All numbers are little-endian.

## Commands

The computer sends a request; the board replies with the same type plus 0x80
and the same sequence number. Every reply payload starts with a status byte:
0 ok, 1 unknown command, 2 bad arguments.

| Type | Name | Request payload | Reply payload after the status byte |
| --- | --- | --- | --- |
| 0x01 | Ping | Any bytes | The same bytes |
| 0x02 | Info | None | Protocol version (1 byte), width (2), height (2), pixel format (1), firmware name (text) |
| 0x03 | Screenshot | None, or flags (1): bit 0 leaves out the mouse pointer, bit 1 says the computer understands encoding 2 | Width (2), height (2), pixel format (1), encoding (1), picture size in bytes (4). Followed by picture frames, see below |
| 0x04 | Key | Key code (2), pressed (1: 1 down, 0 up) | None |
| 0x05 | Mouse | Mode (1: 0 movement, 1 position), x (2, signed), y (2, signed), buttons (1), wheel (1, signed) | None |
| 0x06 | Stats | None | 4-byte numbers: uptime in ms, free internal RAM, free external RAM, frames shown, frames received, receive errors, key events, mouse events, time spent filling video lines in µs (wraps; the first firmware sent only the first eight) |
| 0x07 | Reboot | None | None. The board restarts after replying |
| 0x08 | Windows | None | The windows as JSON text, see below |
| 0x09 | Repaint | None | Time taken in µs (4). The board paints the whole screen again |

- Key codes are USB HID keyboard usage codes (0x04 is A, 0x28 is Enter), the
  same codes a USB keyboard will produce later.
- Mouse buttons: bit 0 left, bit 1 right, bit 2 middle. Each mouse message
  carries the full button state, so a click is one message with the bit set
  and one with it clear.
- Pixel format 1 is one byte per pixel, RRRGGGBB.

## Frames the board sends on its own

| Type | Name | Payload |
| --- | --- | --- |
| 0x70 | Log | Log text |
| 0x71 | Hello | Same as the Info reply, without the status byte. Sent once at start-up |
| 0x72 | Picture data | Chunk number (2), then encoded picture bytes |
| 0x73 | Picture end | Number of chunks (2), CRC-32 of the decoded picture (4) |

## Screenshots

The mouse pointer is not part of the picture in memory; the board mixes it
into each line on the way to the monitor. A screenshot mixes it in the same
way, so it shows what the monitor shows. With flag bit 0 set
(`tools/dbg.py screenshot --no-pointer`) it is left out, which gives the
picture in memory exactly. An empty request means "with pointer", as before.

After the Screenshot reply, the board sends the picture as numbered Picture
data frames and finishes with Picture end. They carry the request's sequence
number. The tool checks the chunk numbers, the size and the CRC-32, and asks
again if anything is wrong.

Encoding 1 is a run-length code. Each token starts with a control byte:

| Control byte | Meaning |
| --- | --- |
| 0 to 127 | Copy the next (n + 1) bytes as they are |
| 128 to 255 | Repeat the next byte (n - 125) times, so 3 to 130 times |

A token never straddles two chunks. A plain desktop shrinks to a few
kilobytes; the worst case (nothing repeats) is 0.8 % larger than the raw
307,200 bytes, about 3.5 seconds at 921,600 baud.

Encoding 2 is sent only when the request sets flag bit 1 (`tools/dbg.py`
always does). It adds copies from a few fixed distances back, which suit the
dithered desktop colour, where nothing repeats three times in a row but
everything repeats every 4 pixels and every 4 rows:

| Control byte | Meaning |
| --- | --- |
| 0 to 127 | Copy the next (n + 1) bytes as they are |
| 128 to 255 | One more byte d follows. Repeat (n - 125) bytes, 3 to 130, starting d' bytes back in the decoded picture, where d' is 1, 2, 4, one row, two rows or four rows for d = 0 to 5. The bytes may overlap what is being written, so copy them one at a time (or in pieces of at most d') |

On the PC tests the dithered desktop takes 308 KB with encoding 1 and 4.7 KB
with encoding 2; a flat colour takes 4.7 KB with either; noise costs 0.8 %
more than the raw picture with either. On the board a screenshot of the
desktop takes 0.4 seconds with encoding 2 (2.2 seconds with encoding 1),
including opening the port.

## Windows

The Windows reply lists the windows from the bottom of the stack to the top:

```json
{"windows":[{"id":1,"z":0,"title":"Welcome to Chinchilla","x":40,"y":30,"w":400,"h":280,"state":"normal","focused":false}, ...],
 "update_us":6246,"event_us":6639}
```

`state` is `normal`, `minimized` or `maximized`. `update_us` is how long the
last repaint took and `event_us` how long the last input event took to
handle, painting included. If the list does not fit in one reply the board
sends `{"error":"too many windows"}`. `tools/dbg.py windows` prints the list;
`tools/dbg.py repaint` repaints and prints the time, and comparing a
screenshot before and after it shows whether anything was left stale.

## Reset lines

The board's reset and boot pins are wired to the serial port's DTR and RTS
lines. `tools/dbg.py` opens the port in a way meant to leave the board
running, so a screenshot shows the current state and not a fresh start.
Confirmed on the bench: uptime and the pointer position survive repeated
connections. `--reset` restarts the board on purpose.

## Planned, not yet in the protocol

A list of windows and widgets as structured data, waiting until the screen is
idle, file transfer, and carrying network traffic for dial-up (see
`docs/DECISIONS.md`).
