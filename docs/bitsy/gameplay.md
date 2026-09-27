# Gameplay

How a Bitsy world behaves while it is running. citsy follows these rules when it simulates a `.bitsy` file. The [data model](data-model.md) covers how the same facts are stored; this page covers what the player (and the host) see.

The descriptions below are restated from the [Bitsy documentation](https://make.bitsy.org/docs/) — the [editor overview](https://make.bitsy.org/docs/introduction/overview/), [rooms](https://make.bitsy.org/docs/tools/room/), [exits and endings](https://make.bitsy.org/docs/tools/exitsandendings/), [inventory](https://make.bitsy.org/docs/tools/inventory/), [colors](https://make.bitsy.org/docs/tools/color/), and [game settings](https://make.bitsy.org/docs/tools/game/settings/). These are the engine rules.

---

## World

A game is a set of **rooms**. The **avatar** (always sprite id `A`) walks between them. Along the way it can talk to **sprites**, pick up **items**, and step on **exits** or **endings**. **Tiles** fill the room grid. Anything that is not a sprite, item, exit, or ending is decoration, except wall tiles, which block movement.

| Piece | Role |
|---|---|
| Avatar | The character the player moves |
| Sprite | Someone or something you talk to by walking into it |
| Item | Something you pick up; the count is kept in inventory |
| Tile | 8×8 art on the room grid |
| Drawing | Any of the above: avatar, sprite, tile, or item |
| Room | A 16×16 tile grid, plus the things placed in it |
| Dialog | The script that runs when you talk, pick something up, or use an exit |

Rooms are always 16×16 tiles. The logical screen is 128×128 pixels. Tile `(0, 0)` is the top-left cell; `(15, 15)` is the bottom-right. See [Coordinate system](data-model.md#coordinate-system).

The game title is a dialog shown in the centre of the screen when play starts, before the avatar can move.

---

## Moving

Directional buttons move the avatar one tile at a time. A press steps immediately. Holding the same direction waits 500 ms, then repeats every 150 ms.

A step is refused when the destination is outside the room, the tile is a wall (`WAL true`, or a legacy room `WAL` list), or another sprite is standing there. Bumping a sprite starts that sprite's dialog and leaves the avatar on its current tile. Sprites stay where they were placed.

`Ok` advances dialog (and skips the rest of the typewriter on the current page). `Menu` is the restart button in the [system API](https://make.bitsy.org/docs/technical/system/). citsy ends the session when `Menu` is released so the host can load the game again.

---

## Talking and picking things up

Walking into a sprite plays its dialog. If the sprite has a blip, that sound plays first. In games saved before Bitsy 7, a sprite with no `DLG` line can still open the dialog whose id matches the sprite id.

Walking onto an item adds one to that item's inventory count, removes the item from the room, plays its blip if it has one, then runs its dialog. A placement can override the item's default dialog (`ITM id x,y DLG …`).

Inventory counts start at the values stored on the avatar (the editor's inventory list). Unlisted items start at `0`. Counts are integers and never go below `0`. They reset when the game starts again. Dialog can read and change them; see [Functions](dialog.md#functions).

**Variables** are separate from item counts. Each one has a name and a starting value that is either a number or text. Names should not contain spaces. Dialog reads and writes them. They also reset when the game starts again.

---

## Rooms

Each room chooses:

| Setting | Effect |
|---|---|
| Palette | The three (or more) colours used while the avatar is in the room |
| Tune | Looping music. Empty or `"0"` is silence |
| Avatar appearance | Optional sprite whose **drawing** replaces the avatar in this room only. Dialog and the real avatar id stay unchanged |

Palette index `0` is the background, `1` is the default tile colour, and `2` is the default sprite and item colour. Further indices are extra colours selected with `COL n` on a drawing. Entering a room switches to that room's palette. Dialog can change the palette or the avatar's look without leaving the room (`{pal}`, `{ava}`).

Drawings animate by flipping frames every 400 ms. The editor paints two frames; the file can store more, separated by `>`.

---

## Exits

An exit is a tile that warps the avatar to another tile, in this room or another one. The editor offers three arrangements. The file stores all of them as `EXT` lines:

| Editor type | In the file |
|---|---|
| One-way exit | One `EXT` in the starting room |
| Two-way exit | A pair of one-way exits, one in each direction |
| Ending | An `END` tile on the room |

Stepping on the source tile runs the exit's dialog if it has one, then moves the avatar to the destination. Each side of a two-way exit can have its own dialog, lock, and transition.

### Transition effects

| Name | What the player sees |
|---|---|
| *(empty)* or `none` | Instant warp |
| `fade_w` | Fade through white |
| `fade_b` | Fade through black |
| `wave` | Horizontal wave wipe |
| `tunnel` | Tunnel wipe |
| `slide_u` `slide_d` `slide_l` `slide_r` | The room slides off in that direction |

Effects are drawn in video mode (a 128×128 colour-index buffer). The tune clock does not advance while an effect is playing. Unknown effect names warp instantly.

### Locks

An exit can require a condition before it warps. The usual pattern is a door that stays shut until the player is holding an item. The exit's dialog sets the `locked` property, often with `{lock}` or `{property "locked" 1}`, and the warp is skipped when that flag is set. The success branch can lower the count so the key is used up. See [Locked doors](dialog.md#locked-doors).

---

## Endings

An ending tile shows that ending's text and then stops the game (`is_running()` becomes false). Endings have no transition effect. `{end}` inside any other dialog stops the game after the box closes, with no separate ending message.

A locked ending cancels the finish. citsy leaves the avatar on the current tile and starts the room tune again. The [exits and endings](https://make.bitsy.org/docs/tools/exitsandendings/) page describes the editor sending the player back to the previous room when an ending's lock condition fails.

---

## Text on screen

Game settings choose how dialog is drawn:

| Setting | File | Meaning |
|---|---|---|
| Font | `DEFAULT_FONT` / `FONT` | Built-in `ascii_small`, or a `.bitsyfont` (including fonts for other writing systems) |
| Text direction | `TEXT_DIRECTION RTL` | Lines lay out from the right. Default is left to right |
| Text mode | `! TXT_MODE 0` or `1` | `0` is the default (hirez). `1` is the editor's "chunky" mode (lorez) |

Hirez draws text pixels at 2× and gives the textbox twice the resolution of the main display. Lorez draws them at 4×, so the textbox matches the main display and the letters look larger. The host applies that scale; see [Text modes](../host.md#text-modes).

---

## Sources

- [About Bitsy](https://make.bitsy.org/docs/)
- [Editor overview](https://make.bitsy.org/docs/introduction/overview/)
- [Room](https://make.bitsy.org/docs/tools/room/) and [room settings](https://make.bitsy.org/docs/tools/room/roomSettings/)
- [Exits & endings](https://make.bitsy.org/docs/tools/exitsandendings/)
- [Inventory](https://make.bitsy.org/docs/tools/inventory/)
- [Colors](https://make.bitsy.org/docs/tools/color/)
- [Game settings](https://make.bitsy.org/docs/tools/game/settings/)
- [How to make a locked door](https://make.bitsy.org/docs/faq/lockedDoor/)
