---
title: "Program settings"
date: 2026-11-16
by: owner
status: decided
area: storage
summary: "One INI file per program in C:\\.config; no central registry"
---

# Program settings

Decided by the owner. Each program keeps its settings in its own plain text
file in a hidden folder, Linux style: `C:\.config\<program>.ini`. The format
is INI (`[section]`, `key = value`, `;` and `#` comments), chosen over TOML and
YAML. Names that start with a dot are hidden in the File manager unless hidden
files are shown. There is no central registry.
