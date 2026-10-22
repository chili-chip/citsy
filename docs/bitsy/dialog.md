# Dialog scripting

Bitsy dialog is a small programming language, not just displayed text. citsy runs `DLG` scripts with a port of Bitsy 8.15's own script parser and interpreter (`src/dialog/script.cpp`, from `script.js`), so a script gives the same text and the same results as in Bitsy. The parity tests check this against the real Bitsy engine ([testing](../testing.md)).

The sections below follow the [dialog tool](https://make.bitsy.org/docs/tools/dialog/) docs: [basic dialog](https://make.bitsy.org/docs/tools/dialog/basicDialog/), [lists](https://make.bitsy.org/docs/tools/dialog/lists/), [room actions](https://make.bitsy.org/docs/tools/dialog/roomActions/), [sound actions](https://make.bitsy.org/docs/tools/dialog/soundActions/), and [item and variable actions](https://make.bitsy.org/docs/tools/dialog/itemVariableActions/). The wording here describes the runtime.

---

## In the file

A `DLG` is one line of script, or a block between two `"""` lines. An optional `NAME` line follows.

```
DLG hello
Hi there!
NAME greeting

DLG gardener
"""
I tend the moss.

{sequence
  - would you help me?
  - thanks!
}
"""
```

The first line of a `.bitsy` file is the title dialog, in the same form, and Bitsy skips the line after it. Text is printed as written, quotes included: `"Hello"` shows the quote marks.

## Pages

| Source | Result |
|---|---|
| A new line inside a `"""` block | New line |
| `{br}` | New line |
| An empty line, or a line of spaces | New page |
| `{pg}` | New page |
| Text that does not fit the 104×32 box | Extra **screens** (`Ok` advances) |

A line that is too long **wraps whole words** onto the next row of the same box. Words are never split. If the box is full, leftover words start the next screen.

citsy keeps its own box: three rows of 104 pixels, which fits the 32blit and 128×128 screens. Bitsy's box has two rows, so Bitsy also turns the page wherever its two rows are full. The text, the line breaks and the page breaks you write are the same.

### Sentences stay on one screen

citsy does not cut a sentence in half at the end of a screen. When a sentence would start on one screen and finish on the next, the whole sentence moves to the next screen. The previous screen ends after the last full sentence.

A sentence ends at `.`, `!` or `?` followed by a space, a newline or the end of the text. A closing quote or bracket right after the mark (`."`, `!)`) still ends it. A dot inside a word, like `3.5`, does not.

Example, 104×32 box (16 letters per row, 3 rows per screen):

```
HELLO THERE. THE MOSS NEEDS WATER EACH DAY OR IT WILL DIE.
```

| | Screen 1 | Screen 2 |
|---|---|---|
| Breaking on fit only | `HELLO THERE. THE` / `MOSS NEEDS WATER` / `EACH DAY OR IT` | `WILL DIE.` |
| citsy | `HELLO THERE.` | `THE MOSS NEEDS` / `WATER EACH DAY` / `OR IT WILL DIE.` |

A sentence that is longer than one screen starts on a new screen and then breaks on fit. To page on fit only, set `keep_sentences = false` on the `TextboxLayout` that `src/engine/engine.cpp` builds.

### When a script runs

A script runs a page at a time: when the box opens, citsy runs the script up to the first `{pg}` (or the end) and shows that text. `Ok` at the end of the page runs the next part. Bitsy runs the script while the letters type out, so in Bitsy a change halfway through a page (an item, a variable, `{exit}`) happens when the typing reaches it; in citsy it happens when the page opens. Once the box closes, both have done the same things.

---

## Textbox look

The dialog box is always a black rectangle (`kTextboxBlack`). Glyphs are white (`kTextboxWhite`) unless a colour tag or `{rbw}` is active. While dialog is open the engine installs those colours (and 16 rainbow hues at `kTextboxRainbow0`) into the palette passed to `present()`, so hosts can index the textbox buffer without a special case. Rainbow slots use Bitsy's three out-of-phase sines (`sin(phase)*127+128`).

Letters type in one by one (50 ms per printable glyph; spaces and newlines are free). Word wrap is computed against the full page so later characters do not reflow as they appear. The continue arrow shows only after the page is fully revealed; `Ok` (or any action button) skips remaining typing, then a second press advances. While a blip plays, the box waits: typing pauses and `Ok` does nothing, as in Bitsy.

## Text effects

`{wvy}`, `{shk}`, `{rbw}` and `{clr1}`..`{clr3}` switch an effect on, and the same tag again switches it off. Effects combine: `{wvy}{rbw}hello` is both wavy and rainbow.

| Tag | Effect |
|---|---|
| `{wvy}` | Vertical sine offset (animated). `{/wvy}` also switches it off |
| `{shk}` | Jitter offset (animated). `{/shk}` also switches it off |
| `{rbw}` | Rainbow ink per character. `{/rbw}` also switches it off |
| `{clr1}` `{clr2}` `{clr3}` | The room palette's colours 0, 1 and 2 |
| `{clr n}` ... `{/clr}` | Palette colour `n` |

There is no `{/clr1}`; Bitsy does not know that name, so it does nothing. When several colours are on, the last one switched on wins.

---

## Values

Values work as in JavaScript, because Bitsy's engine is JavaScript: numbers, strings, `true`/`false`, and `null` for a variable that was never set.

```
VAR score
0

VAR name
ada
```

A `VAR` value is `true` or `false`, else a number when it starts with one (`3.5`), else text.

| Expression | Result |
|---|---|
| `{a = 10 - 3 + 2}` | `5`: Bitsy splits at the first operator in the order `== >= <= > < - + / *`, so this is `10 - (3 + 2)` |
| `{b = "a" + "b"}` | `"ab"`: `+` joins when either side is text |
| `{c = 1 == "1"}` | `false`: `==` compares type and value |
| `{d = 5 / 2}` | `2.5` |
| `{say 0.1 + 0.2}` | prints `0.1`: function arguments are separated by spaces |
| `{say {0.1 + 0.2}}` | prints `0.30000000000000004` |

Numbers print the way JavaScript prints them: `2.5`, `1e+21`, `true`.

Inspect at runtime with `Engine::variable_value("score")` (as text) or `Engine::variable_json("score")`.

---

## Functions

| Tag | Effect |
|---|---|
| `{say x}` / `{print x}` | Print the value. A variable that was never set prints nothing |
| `{x}` | Nothing is printed: a bare value is evaluated and dropped |
| `{x = expr}` | Assign a variable |
| `{cond ? a : b}` | Prints the chosen side as dialog text |
| `{item "id-or-name"}` | Item count |
| `{item "id-or-name" n}` | Set that count to `n` (never below 0) |
| `{end}` | Ends the game now; the rest of the dialog still shows, centred |
| `{exit "room" x y}` | Warp now. An optional fourth argument is a [transition](gameplay.md#transition-effects) |

The editor's item buttons are these same tags:

| Editor action | Script |
|---|---|
| Set count | `{item "key" 1}` |
| Add one | `{item "key" {{item "key"} + 1}}` |
| Remove one | `{item "key" {{item "key"} - 1}}` |
| Say the count | `{say {item "key"}}` |
| Set a variable | `{score = 5}` |
| Change a variable | `{score = score + 1}` |
| Say a variable | `{say score}` |

Walking onto a room item first plays the item's dialog, then takes the item when the dialog closes. So an item's dialog still sees the count from before the pick-up.

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
    You have the key.
  - else ?
    The door is locked.
}
```

- **sequence**: the next option each time this dialog runs, then stays on the last option
- **cycle**: the next option each time, then wraps to the first
- **shuffle**: a random option, with no repeats until every option has been used
- **branch** (bare `{`): the first option whose condition is true; `else` runs when none is

Each option can hold more than text: another list, a palette change, an item change. Lists nest. A list remembers its position per dialog until the game restarts. In the file, every option is a line that starts with `-`.

## Room actions

| Tag | Effect |
|---|---|
| `{exit "room" x y}` | Warp to tile `(x, y)` now. A fourth argument names a transition. Without x and y it only returns the current room |
| `{end}` | End the game: the screen clears, the rest of the dialog shows, and the next press after it restarts the game |
| `{property locked true}` | Lock the exit or ending that opened this dialog. `{property locked false}` unlocks it, and `{property locked}` reads it |
| `{pal "id"}` | Switch the palette. Without an argument, returns the current one |
| `{ava "id"}` | Draw the avatar as that sprite. Without an argument, returns the current one |

`property` takes the name as a bare word, as in Bitsy: `{property "locked" true}` does nothing.

## Sound actions

| Tag | Effect |
|---|---|
| `{blip "id"}` | Play that blip now (by id or name) |
| `{tune "id"}` | Play that tune, unless it is already the current tune. `"0"` stops the music |

Walking into a sprite or onto an item also plays the blip stored on that drawing. Only one blip plays per step. See [Sound](sound.md).

## Drawings in the text

| Tag | Also written | Draws |
|---|---|---|
| `{printSprite "A"}` | `{drws "A"}` | A sprite, including the avatar |
| `{printTile "a"}` | `{drwt "a"}` | A tile |
| `{printItem "key"}` | `{drwi "key"}` | An item |

The argument is an id (`"A"`) or a `NAME` (`"tea"`). The drawing uses its first frame, in the text colour.

---

## Endings

Room sub-key `END <id> x,y` is a trigger tile. Stepping on it clears the screen and shows the `DLG` with that id. When the text is over, the next button press restarts the game from its title, as in Bitsy. Releasing `Menu` also restarts the game.

## Locked doors

A locked exit is an exit whose dialog locks it unless the player meets a condition. The [locked door](https://make.bitsy.org/docs/faq/lockedDoor/) guide in the Bitsy docs is this pattern: an item stands in for the key, and the exit on the doorway checks the count.

```
"""
{
  - {item "card"} >= 1 ?
    {item "card" {{item "card"} - 1}}
  - else ?
    You need a membership card.{property locked true}
}
"""
```

The first option leaves the exit unlocked, so the warp runs after the dialog, and spends one card. The `else` option locks the exit and shows the refusal. Only `true` locks; `1` does not.

A lock lasts until the player leaves the room: every time a room is entered, its exits and endings start unlocked. The same `locked` property works on an ending tile: if it is locked when the ending's dialog finishes, the game goes on.

---

## Engine inspectors

```cpp
engine.item_count("key");          // by id or NAME
engine.variable_value("score");    // current value as text
engine.variable_json("score");     // current value as JSON: 1, true, "text", null
engine.ending_active();            // true after an ending, until the restart
```
