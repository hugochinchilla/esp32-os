---
title: "Chosen for the priority classes"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Low 3, Normal 4, High 5 as FreeRTOS priorities; desktop 5, video 10 (backlog 11)"
---

# Chosen for the priority classes

Heading in the single-file log: "Chosen for the priority classes (2026-11-17, to confirm)".


Backlog item 11. Journal: `docs/journal/2026-11-17-priorities.md`. Chosen by
the agent under the owner's "dejo a tu criterio" of 2026-11-16.

- **Low 3, Normal 4, High 5** as FreeRTOS priorities; the desktop's task has
  5, the video 10. Low and Normal never keep the desktop from an event;
  High takes turns with it. Rejected: High above the desktop (a busy job
  would freeze it).
- **A class per job** (`job::Spec::priority`, Normal by default), named in
  the portable code as `ports::Priority` and turned into a number by the
  adapter. The services' task (`inetd`) is Normal by the same function.
  Rejected: a priority per process set from the Task Manager (the windows
  share the desktop's task, so it would only reach the jobs).
- **A task does not delete itself.** When its function returns it sleeps,
  and the job runner deletes it once the result has reached the UI task. So
  a running job's processor time can be read safely, and the Task Manager,
  `ps` and `top` show it while the job runs
  (`proc::Table::bring_up_to_date()`).
