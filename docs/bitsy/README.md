# Bitsy engine reference

The documentation website is at **https://teriyakigod.github.io/citsy/bitsy/**.

Documentation for the Bitsy game format and how citsy represents it. These guides describe the **data model** and file format that the engine parses and simulates — not the Bitsy editor UI.

| Document | Contents |
|---|---|
| [Data model](data-model.md) | All entity types, properties, relationships, and C++ struct mapping |
| [Dialog scripting](dialog.md) | Pages, lists, room and sound actions, inventory, `{end}` / `{exit}` |
| [Gameplay](gameplay.md) | Movement, sprites, items, exits, endings, locks, text settings |
| [Sound](sound.md) | Blips, tunes, pulse waves, and host channel units |

### External references

- [Bitsy](https://bitsy.org) — official editor and website
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host layer contract used by bitsybox and the web runtime
- [Bitsy documentation](https://make.bitsy.org/docs/) — editor and engine reference these pages restate
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust parser for cross-checking format behavior
- [Bitsy Wiki / FAQ](https://bitsy.fandom.com/wiki/FAQ) — community documentation on variables, colors, and scripting
