# Dialog scripting

Bitsy dialog is a small programming language, not just displayed text. citsy evaluates `DLG` and `END` source at runtime (`src/dialog/script.cpp`) against the engine's variables and inventory.

The sections below follow the Bitsy 7+ script language as described in the [dialog tool](https://make.bitsy.org/docs/tools/dialog/) docs: [basic dialog](https://make.bitsy.org/docs/tools/dialog/basicDialog/), [lists](https://make.bitsy.org/docs/tools/dialog/lists/), [room actions](https://make.bitsy.org/docs/tools/dialog/roomActions/), [sound actions](https://make.bitsy.org/docs/tools/dialog/soundActions/), and [item and variable actions](https://make.bitsy.org/docs/tools/dialog/itemVariableActions/). The wording here describes the runtime.

---

## Pages

| Source | Result |
|---|---|
| `"Hello!"` then `"Bye."` on the next line | Two pages |
| `{p}` / `{pg}` | New page |
| `{br}` | Newline on the current page |
| Blank line inside a list item or `"""` block | New page |
| Text that does not fit the 104×32 box | Extra **screens** (`Ok` advances) |

Unquoted text (common in lists) is one page unless a blank line or `{p}` splits it. A line that is too long **wraps whole words** onto the next row of the same box. Words are never split. If the box is full, leftover words start the next screen. `{br}` is an explicit newline.

Adjacent quoted lines with no break are one page. The editor often merges them into a single string. A page break (`{p}`, `{pg}`, or a blank line inside a list or `"""` block) waits for `Ok` before anything after the break runs, including a later `{exit}` or `{end}`.

`{exit}` and `{end}` themselves apply when the dialog closes, after the player has stepped through every page.

---

## Textbox look

The dialog box is always a black rectangle (`kTextboxBlack`). Glyphs are white (`kTextboxWhite`) unless a colour tag or `{rbw}` is active. While dialog is open the engine installs those colours (and 16 rainbow hues at `kTextboxRainbow0`) into the palette passed to `present()`, so hosts can index the textbox buffer without a special case. Rainbow slots use Bitsy's three out-of-phase sines (`sin(phase)*127+128`).

Letters type in one by one (50 ms per printable glyph; spaces and newlines are free). Word wrap is computed against the full page so later characters do not reflow as they appear. The continue arrow shows only after the page is fully revealed; `Ok` (or any action button) skips remaining typing, then a second press advances.

## Text effects

Tags combine: `{wvy}{rbw}hello` is both wavy and rainbow. A close tag (`{/wvy}`) clears only that bit.

| Tag | Effect |
|---|---|
| `{wvy}` / `{/wvy}` | Vertical sine offset (animated) |
| `{shk}` / `{/shk}` | Jitter offset (animated) |
| `{rbw}` / `{/rbw}` | Rainbow ink per character (Bitsy sine RGB) |
| `{clr}` / `{clr1}` | Tile colour (palette index 1) |
| `{clr2}` | Sprite colour (palette index 2) |
| `{clr3}` | Palette index 3 |
| `{clr n}` | Palette index `n`. `{clr 0}` is the room background |

Rainbow wins over `{clr}` when both are on. `{rbw}` picks one hue per character from Bitsy's `(time_ms / 100) - col * 0.5`.

---

## Values

Variables and expressions are **numbers** or **strings**. A missing variable is an empty string, which counts as `0` in arithmetic and comparisons. `true` / `false` are `1` / `0`.

```
VAR score
0

VAR name
ada
```

Inspect at runtime with `Engine::variable_value("score")`.

---

## Functions

| Tag | Effect |
|---|---|
| `{print expr}` / `{say expr}` | Write the value into the current page. `{say}` is the older name |
| `{name}` | Write the variable. Missing names print nothing |
| `{name = expr}` | Assign a variable. `+ - * /` are numeric |
| `{item "id-or-name"}` | Item count (id or `NAME`) |
| `{item "id-or-name" n}` | Set that count to `n` (clamped at 0) |
| `{end}` | Stop the game after this dialog closes |
| `{exit "room" x y}` | Warp after this dialog closes. Optional fourth argument is a [transition](gameplay.md#transition-effects) name |

The editor's item buttons are these same tags:

| Editor action | Script |
|---|---|
| Set count | `{item "key" 1}` |
| Add one | `{item "key" {item "key"} + 1}` |
| Remove one | `{item "key" {item "key"} - 1}` |
| Say the count | `{item "key"}` |
| Set a variable | `{score = 5}` |
| Change a variable | `{score = score + 1}` |
| Say a variable | `{score}` or `{print score}` |

Walking onto a room item increments inventory immediately, then plays the item dialog, then removes it from the room.

Comparisons: `== != < > <= >=`. `true` / `false` are `1` / `0`. A number is true when it is not `0`. A string is true when it is not empty, `"0"`, or `"false"`. A string used with `+ - * /` is parsed as a number, or `0` when it does not parse.

---

## Lists

```
{sequence
  - first visit
  - later visits (sticks on last)
}

{cycle
  - a
  - b
}

{shuffle
  - one
  - two
}

{
  - {item "key"} > 0 ?
    "You have the key."
  - else ?
    "The door is locked."
}
```

- **sequence** — the next arm each time this dialog runs, then stays on the last arm forever
- **cycle** — the next arm each time, then wraps to the first
- **shuffle** — a random arm, with no repeats until every arm has been used
- **branch** (bare `{`) — the first arm whose condition is true

A branching list is an if / else-if / else chain. Arms are tested from top to bottom. The first true condition wins. An `else` arm has no condition and runs only when every earlier arm failed. One opening condition, any number of later conditions, and an optional `else` is the shape the editor produces.

Each arm can contain more than text: another list, a palette change, an item change, and so on. Lists nest. A blank line inside an arm is a page break. In the file, every arm is a line that starts with `-`.

## Room actions

These tags change the world. They correspond to the dialog tool's room actions.

| Tag | When | Effect |
|---|---|---|
| `{exit "room" x y}` | After the dialog closes | Warp to tile `(x, y)`. A fourth argument names a transition |
| `{end}` | After the dialog closes | Stop the game. Later pages of this dialog still play first |
| `{lock}` | Immediately | Set `locked` on the exit or ending that opened this dialog |
| `{property "locked" 1}` | Immediately | Set that lock flag. `0` clears it. With one argument, the tag reads the flag |
| `{pal "id"}` | Immediately | Switch the active palette |
| `{ava "id"}` | Immediately | Draw the avatar as that sprite. The sprite's dialog is not copied |

`{exit}` and `{end}` wait for the box to close so a page break in front of them can show text first.

## Sound actions

| Tag | Effect |
|---|---|
| `{blip "id"}` | Play that blip now (by id or name) |
| `{tune "id"}` | Start that tune. `"0"` or an empty id stops the music |

Walking into a sprite or item also plays the blip stored on that drawing, before its dialog. See [Sound](sound.md).

## Drawings in the text

| Tag | Also written | Draws |
|---|---|---|
| `{printSprite "A"}` | `{drws "A"}` | A sprite, including the avatar |
| `{printTile "a"}` | `{drwt "a"}` | A tile |
| `{printItem "key"}` | `{drwi "key"}` | An item |

The quoted argument is an id (`"A"`) or a `NAME` (`"tea"`). The drawing uses the current animation frame and that drawing's colour index.

---

## Endings

Room sub-key `END <id> x,y` is a trigger tile. Stepping on it plays the matching top-level `END` text, then `is_running()` becomes false.

`{end}` inside a sprite or item dialog does the same after the box closes, without showing a named ending.

## Locked doors

A locked exit is an exit whose dialog sets `locked` unless the player meets a condition. The [locked door](https://make.bitsy.org/docs/faq/lockedDoor/) guide in the Bitsy docs is this pattern: an item stands in for the key, and the exit on the doorway checks the count.

```
{
  - {item "card"} >= 1 ?
    {property "locked" 0}
    {item "card" {item "card"} - 1}
  - else ?
    "You need a membership card."
    {lock}
}
```

The first arm clears the lock, so the warp runs after the dialog. It also spends one card. The `else` arm locks the exit and shows the refusal. The same `locked` flag works on an ending tile: if it is set when the ending dialog finishes, the game does not end.

---

## Engine inspectors

```cpp
engine.item_count("key");        // by id or NAME
engine.variable_value("score");  // current value as text
engine.is_running();             // false after an ending
```
