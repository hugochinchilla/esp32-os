---
title: "Window themes"
date: 2026-11-17
by: owner
status: decided
area: desktop
summary: "Themes chosen in Display: Classic, Sky, Glacier, Midnight; a theme changes only what is painted (backlog 47)"
---

# Window themes

Heading in the single-file log: "Window themes (the owner's decision, 2026-11-17)".


The desktop has themes, chosen in Display: Classic (the look it always
had), Sky, Glacier and Midnight. The owner picked the three new ones from
five that were drawn; the one-bit and workbench looks were not taken.

- A theme changes only what is painted: frames, title bars, buttons, fields,
  scroll bars, menus, the taskbar. Sizes and places are the same in every
  theme, so no layout and no hit test depends on it.
- The table is `core/gfx/theme.hpp`. What draws a part of the user
  interface asks it (`gfx::face()`, `ink()`, `paper()`, `dim()`,
  `gfx::theme()`); `gfx::kGrey`, `kBlack` and `kWhite` are for drawings that
  keep their colours (icons, games, boot screens).
- Every flat colour of a theme is one the board has exactly.
- Kept in `desktop.ini`, `[look] theme`. The desktop's colour is not part
  of a theme.

The agent's, to confirm: the table as rows in the code (not files on C:),
the kinds of edge, title bar and title button as fields of a row. Not
decided: icon themes, themes that move the title buttons. Rejected for now:
rounded corners (a window is a rectangle to the window manager).

Journal: [first round](journal/2026-11-17-window-themes.md),
[second round](journal/2026-11-17-window-themes-modern.md),
[built](journal/2026-11-17-window-themes-built.md).
