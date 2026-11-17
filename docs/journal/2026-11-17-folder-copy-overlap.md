# A folder name copied into itself

## What was asked

Found when building the Linux desktop program (PR 21) with optimisation:
GCC 16 stopped the Release build with `-Werror=restrict` in
`core/src/shell/file_manager.cpp`, `FolderWindow::go_to()`. The coordinator
asked the board thread to fix it on main and to look for the same pattern
elsewhere.

## What was found

`go_to(folder, remember)` copied `folder` into the window's `folder_` with
`snprintf`. Callers can pass `folder_` itself (going to the folder already
shown, as a refresh) or a history entry, and `snprintf` from a buffer into
itself is undefined behaviour, on the PC and on the board alike. The normal
PC build does not optimise, so GCC never warned there. Once that copy went
through a temporary, GCC flagged the next line too: the history entry was
copied from `folder_` with an index it cannot prove stays inside the history
array (a false alarm, but the same cure is cheap).

A Release build of every host target with `-Werror` found no other place.

## What was chosen

Copy both names out first: the target into a local `to`, the current folder
into a local `from`, then write the history entry and `folder_` from those.
The behaviour is the same: a `go_to` to the current folder still reads the
listing again.

## What was built and tested

`core/src/shell/file_manager.cpp`. PC tests 24 of 24; the Release build of
all host targets, `chinchilla` included, is clean with `-Werror`; the firmware
builds. On the board (fast start): Documents opened, into Desktop, Up to
Documents, Back to Desktop, F5 refreshed, all as before.

## What is left

Nothing.
