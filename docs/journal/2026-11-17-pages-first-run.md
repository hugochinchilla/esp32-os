# The first run of the Pages workflow

2026-11-17, midday. A fix to the browser version's build
([Chinchilla in the browser](2026-11-17-chinchilla-in-the-browser.md)).
Nothing in `core/` or the firmware is changed.

## What was asked

The owner turned the publication on and said so:

> I committed and pushed the changes to enable workflow pages

## What was found

The first run of "Publish the browser version" (run 1, commit `bd0a289`)
failed after 50 seconds, in the step that builds the page. Three errors, all
the same, in `core/src/shell/apps.cpp`:

    core/src/shell/internal.hpp:71:5: error: destructor called on non-final
    'AboutWindow' that has virtual functions but non-virtual destructor
    [-Werror,-Wdelete-non-abstract-non-virtual-dtor]
       71 |     window->~T();

`internal.hpp` is new since the browser version was merged: it came
overnight with the work on program lifecycle, where a window is destroyed
when its program ends. The code is right (`T` is the window's exact type),
and GCC, which builds the board and the PC tests, says nothing. Clang warns,
and `core/` turns warnings into errors.

The browser version's own journal entry had the cause in it: "a second
compiler is a free review". It had let three named Clang warnings through
and kept every other one an error. So the first new Clang-only warning in
`core/`, written by a thread that has no Clang, stopped the publication.

## The options

1. Qualify the call in `internal.hpp` (`window->T::~T()`), as Clang
   suggests. Fixes today's three errors; the next Clang-only warning breaks
   the page again, and whoever writes it cannot see it coming.
2. Let every warning in `core/` be a warning in the browser build
   (`-Wno-error` for `core/` there). The page is published whatever Clang
   thinks of style; the warnings still show in the build's output.
3. Make Clang part of the PC tests, so every thread sees its warnings
   before merging.

## What was chosen

**2**, by the agent. The publication must not depend on a compiler nobody
else runs. `host/desktop/main.cpp`, the browser's own code, still builds
with warnings as errors. 1 was not done because it edits a file of work
that is still going on, for a warning that 2 already makes harmless. 3 is
the better long-term answer and is the owner's call: it adds a compiler to
every thread's checks.

## What was built and tested

`web/CMakeLists.txt`: one line. Built from today's `main` with Emscripten
7.0.0 (the page is now 1,937,215 bytes; `core/` has grown overnight) and
run through `tools/test_web_page.js` in Chromium: all six checks pass. The
build prints 2 warnings from `core/`.

Not tested: the workflow itself. It runs again when this reaches `main`.

## What is left

- The Clang warnings in `core/`, for whoever is next in those files:
  the destructor call in `internal.hpp`, and the three leftovers named in
  the browser version's entry.
- Option 3, if the owner wants it.
