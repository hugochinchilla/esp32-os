---
title: "Change notices from the file system"
date: 2026-11-18
by: owner+agent
status: decided
area: storage
summary: "Storage says which folder changed; the desktop reads its folder only then, in a job, not every 2 s (backlog 66)"
---

# Change notices from the file system

Chosen by the owner after the 60 fps benchmark, from his own question (a
background listing, or something like inotify): both. Backlog item 66,
`docs/journal/2026-11-18-change-notices.md`. The smaller choices below are
the agent's, each easy to change.

- `fs::Storage` tells one listener which folder's list of files changed
  (`Storage::set_changed`), after every call that made, removed, renamed
  or finished writing something. The folder only, not the file or what
  happened. It is called on the task that made the change, with no lock
  held; the listener may only set a flag or post a message.
- The shell is the listener. The desktop reads `C:\Documents\Desktop`
  and looks into the Trash only when told that one of them changed, in a
  background job, and sets the icons when the job is back. The File
  manager's windows on a changed folder, or inside one, read theirs again.
- Nothing calls "files changed" by hand any more (`files_changed()` is
  gone). Code that moves or deletes files just does it.
- A 2 s timer remains as a net and reads no folder: it settles a notice
  that was lost to a full queue or a job that could not start.
- After settling, 250 ms pass before the next when a queue is there, so a
  long paste reads the folders at most four times a second.
- Not taken: reading less often (the stall stays); reading every 2 s on a
  job (the drive read 30 times a minute for nothing); a watcher list behind
  an OS call for programs (no program needs one yet).
