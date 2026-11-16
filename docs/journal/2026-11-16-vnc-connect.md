# VNC connect times on main, and a correction

## What was asked

The message-queue thread (PR 18) asked for five VNC connects by address on
main, to see whether the half second measured once on its branch was the new
50 ms look at the network while waiting for a connection
(`kListenPollUs`), or the network. If the connects were consistently above
about 300 ms, a build with the look every 10 ms was to be tried.

## What was found

Main at 894c4f5 (with the message queue), measured from the laptop by
address, one connect at a time with a pause between:

| | Times |
| --- | --- |
| TCP connect alone | 280, 56, 24, 105, 42 ms |
| TCP connect and the RFB handshake | 577, 206, 260, 272, 308 ms (median 272) |
| Ping, 10 packets | 10 % lost; round trip 7.6 to 54.7 ms, 25.5 on average |

Not consistently above 300 ms, so the 10 ms experiment was not run. The
spread follows the network: the TCP connect alone varies by more than 250 ms
tonight, and a packet in ten is lost. The single 668 ms seen on the branch
fits that spread.

## A correction

The PR 18 board results (in `2026-11-16-message-queue.md`) say the raw VNC
screens moved at "about 30 KB/s against 160 KB/s measured earlier in the
day". That was wrong: the test client (`tools/rfb_client.py`) asks for 32-bit
pixels, so a full raw screen is 1,228,800 bytes, not 307,200. A full raw
screen took 10.7 s just now: about 112 KB/s. The 160 KB/s from the morning was
measured with the board's own 8-bit pixels. So the network was not slow
tonight, and the conclusion there that the slowness was the network's still
stands only in that both builds measured the same.

## What is left

Nothing for this. A faster raw transfer would come from asking for 8-bit
pixels or ZRLE, as the browser viewer does.
