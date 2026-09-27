# Bitsy engine reference

Documentation for the Bitsy game format and how citsy represents it. These guides describe the **data model** and file format that the engine parses and simulates — not the Bitsy editor UI.

<div class="grid cards" markdown>

-   :material-database: **[Data model](data-model.md)**

    ---

    Entity types, properties, relationships, file syntax, and C++ struct mapping.

-   :material-script-text: **[Dialog scripting](dialog.md)**

    ---

    Pages, lists, room and sound actions, inventory, and `{end}` / `{exit}`.

-   :material-gamepad-variant: **[Gameplay](gameplay.md)**

    ---

    Movement, sprites, items, exits, endings, locks, and text settings.

-   :material-volume-high: **[Sound](sound.md)**

    ---

    Two pulse channels, blips, tunes, tempo, and the units passed to the host.

</div>

A Bitsy game is a collection of typed segments in a plain-text `.bitsy` file:

| Entity | Description |
|---|---|
| **Palette (`PAL`)** | Named color sets (background, tile, sprite, plus optional extended colors) |
| **Tile (`TIL`)** | 8×8 pixel art; optional wall flag, animation frames, color index |
| **Sprite (`SPR`)** | Animated 8×8 character or object; avatar is always id `A` |
| **Item (`ITM`)** | Collectible 8×8 object with inventory semantics |
| **Room (`ROOM`)** | 16×16 grid of tile IDs, plus placed items, exits, and endings |
| **Exit (`EXT`)** | Warp tile: target room, position, optional dialog, transition effect |
| **Dialogue (`DLG`)** | Script attached to sprites, items, or exits |
| **Variable (`VAR`)** | Global number or string state |
| **Ending (`END`)** | End-game message (top-level) or tile trigger (room sub-key) |

Rooms are 16×16 tiles. The logical screen is 128×128 pixels. See [Architecture](../architecture.md) for how those numbers become memory blocks.

## External references

- [Bitsy](https://bitsy.org) — official editor and website
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host layer contract used by bitsybox and the web runtime
- [Bitsy documentation](https://make.bitsy.org/docs/) — editor and engine reference these pages restate
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust parser for cross-checking format behavior
- [Bitsy Wiki / FAQ](https://bitsy.fandom.com/wiki/FAQ) — community documentation on variables, colors, and scripting
