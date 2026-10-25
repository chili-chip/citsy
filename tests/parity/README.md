# Parity tests against Bitsy 8.15

These tests check that citsy plays a game the way Bitsy 8.15 does. The same
steps run in two engines:

- `bitsy_reference.mjs` runs the Bitsy 8.15 engine itself (`reference/bitsy-8.15`,
  the files Bitsy's HTML export uses) in Node, with a headless stand-in for the
  Bitsy System API, and writes a `.golden` trace for each case.
- `tests/unit/test_parity.cpp` runs the same case in citsy with `MockHost` and
  compares its trace with the golden.

```sh
node tests/parity/bitsy_reference.mjs           # write every golden again
node tests/parity/bitsy_reference.mjs flow      # only cases/flow.case
node tests/parity/bitsy_reference.mjs --check   # fail if a golden is out of date (CI)
```

CI runs `--check`, so a changed case or game needs its golden written again in
the same pull request. Never edit a golden by hand.

## Case files

A case names a game and lists steps, one per line. `#` starts a comment.

| Line | What it does |
|---|---|
| `game <path>` | The `.bitsy` file, relative to the case file |
| `press <button> ...` | For each button (`up` `down` `left` `right` `ok` `menu`): hold it one frame, release it one frame, then run 6 s of frames |
| `read` | Press `ok` until no dialog is open |
| `wait [ms]` | Run frames for `ms` (default 6000) |
| `trace output` | Also trace the sound channels and the graphics mode (below) |

Frames are 16 ms. Both engines use the same seeded random numbers
(mulberry32, seed `0xC15A9`), so `{shuffle}` picks the same order.

## Trace

After the start and after each step the trace has:

```
> press right            # the step
text "Hello there."      # text dialogs showed, printed once the dialog has closed
room 0 at 5,4            # room and avatar position
mode playing             # playing, narrating (title or ending text) or ending
dialog -                 # or "dialog open"; then nothing else is printed
inventory 0=1            # items held, by id
var n = 1                # each variable as JSON
```

With `trace output`, each frame where a sound channel or the graphics mode
changed adds a line before the text:

```
output @6128 s1 262hz v5 p2 | s2 131hz v5 p1 | map
```

That is the time in ms since the case started, each channel as frequency,
volume (0 to 15) and pulse (`0` 1/8, `1` 1/4, `2` 1/2) or `off`, and `video`
while a room transition is on screen. The reference plays Bitsy's
`bitsy.sound()`, `frequency()` and `volume()` calls on two virtual channels; a
note lasts its duration in real time.

Text, inventory and variables wait for the dialog to close because Bitsy runs a
script while its text types out, and citsy runs it a page at a time. Text is
compared without row and page breaks, because citsy keeps its own text box
layout (three rows of 104 pixels, sentences kept on one screen).

## Reference files

Bitsy itself is not stored in this repository. `scripts/fetch_bitsy.cmake`
downloads Bitsy 8.15's engine scripts and fonts, unchanged, from the `v8.15`
commit on [Codeberg](https://codeberg.org/adamledoux/bitsy) into
`reference/bitsy-8.15` (ignored by git) and checks each file's SHA-256.
`bitsy_reference.mjs`, `scripts/gen_builtin_fonts.py` and a CMake configure
with tests on all run it first, so there is nothing to do by hand; it needs
network access the first time. The files are under Bitsy's MIT license
(`LICENSE.md`, `CREDITS.md` in the download) and are only used by the tests.
