# Host API

The host implements `citsy::Host` — a C++ analogue of the `bitsy` global object from the [Bitsy System API](https://make.bitsy.org/docs/technical/system/). The engine calls into the host for **input** and **time**; the host reads **memory blocks** and **audio state** that the engine owns.

Headers: [`include/citsy/host.hpp`](https://github.com/TeriyakiGod/citsy/blob/main/include/citsy/host.hpp), [`include/citsy/types.hpp`](https://github.com/TeriyakiGod/citsy/blob/main/include/citsy/types.hpp).

## Constants

| Name | Value | Meaning |
|---|---|---|
| `kTileSize` | 8 | Pixels per tile edge |
| `kMapSize` | 16 | Room width/height in tiles |
| `kVideoSize` | 128 | Main framebuffer edge in pixels (16 × 8) |

| Enumerator | Meaning |
|---|---|
| `GraphicsMode::Video` | Direct per-pixel framebuffer |
| `GraphicsMode::Map` | Tilemap mode (normal gameplay) |
| `TextMode::HiRez` | Default text. Text pixels at 2×; textbox resolution is twice the main display |
| `TextMode::LoRez` | Editor "chunky" text. Text pixels at 4×; textbox resolution matches the main display |

## Buttons

| Code | Action |
|---|---|
| `Up` | Move avatar / menu up |
| `Down` | Move avatar / menu down |
| `Left` | Move avatar / menu left |
| `Right` | Move avatar / menu right |
| `Ok` | Interact, advance dialog |
| `Menu` | Restart (host reloads the game) |

The host normalizes keyboard, gamepad, and touch into these six logical buttons. The [system API](https://make.bitsy.org/docs/technical/system/) names the usual bindings:

| Code | System constant | Typical binding |
|---|---|---|
| `Up` | `BTN_UP` | Up arrow or W, swipe up, d-pad up |
| `Down` | `BTN_DOWN` | Down arrow or S, swipe down, d-pad down |
| `Left` | `BTN_LEFT` | Left arrow or A, swipe left, d-pad left |
| `Right` | `BTN_RIGHT` | Right arrow or D, swipe right, d-pad right |
| `Ok` | `BTN_OK` | Any other key, a tap, or a face button. Advances dialog |
| `Menu` | `BTN_MENU` | Ctrl+R, or Start on a gamepad. Restarts the game |

`button()` is the held state, the same as `bitsy.button(code)`. citsy ends the session when `Menu` is released; the host reloads the game if it wants a restart.

## Memory blocks

The engine maintains fixed logical buffers. The host reads them after each update; the engine may resize the textbox buffer when dialog layout changes.

| Block | Size (typical) | Purpose |
|---|---|---|
| `Video` | 128 × 128 | Per-pixel color indices in `GraphicsMode::Video` |
| `Textbox` | w × h (dynamic) | Dialog text rendered as color indices |
| `Map1` | 16 × 16 | Primary tilemap (tile IDs) |
| `Map2` | 16 × 16 | Overlay tilemap (sprites / items) |
| `Sound1`, `Sound2` | — | Channel frequency, volume, pulse, duration |

Color indices refer to the active palette passed into `present()`. Hosts should not assume a fixed 3-color palette — extended palettes (`COL n`) are supported. While a dialog is open the engine also installs true black, white, and rainbow hues at `kTextboxBlack`, `kTextboxWhite`, and `kTextboxRainbow0` so textbox pixels can be decoded independently of the room palette.

## Interface

```cpp
class Host {
public:
    virtual ~Host() = default;

    virtual void on_engine_ready() {}

    virtual double delta_time_ms() const = 0;
    virtual bool button(Button code) const = 0;

    virtual void log(std::string_view message) {}

    virtual void present(
        GraphicsMode gfx_mode,
        TextMode txt_mode,
        std::span<const Color> palette,
        std::span<const std::uint8_t> video,    // 128×128 indices
        std::span<const std::uint8_t> map1,     // 16×16 tile IDs
        std::span<const std::uint8_t> map2,
        TextboxView textbox,                    // may be hidden
        SoundChannel sound1,
        SoundChannel sound2
    ) = 0;
};
```

| Method | Direction | Purpose |
|---|---|---|
| `delta_time_ms()` | host → engine | Frame delta in milliseconds |
| `button(Button)` | host → engine | Logical button held-state |
| `on_engine_ready()` | engine → host | Once after `Engine::start()` |
| `log(message)` | engine → host | Optional diagnostic sink |
| `present(...)` | engine → host | Full frame state after each `update()` |

### `TextboxView`

| Field | Meaning |
|---|---|
| `visible` | Whether the dialog box should be drawn |
| `x`, `y` | Top-left in 128×128 Bitsy units |
| `width`, `height` | Pixel size of `pixels` |
| `pixels` | Color indices; empty when not visible |

### `SoundChannel`

| Field | Meaning |
|---|---|
| `active` | Host should play this channel |
| `duration_ms` | Remaining note / blip time |
| `frequency_hz` | Square-wave frequency |
| `volume` | 0.0 – 1.0 |
| `pulse` | Duty cycle: `Eighth`, `Quarter`, or `Half` |

The engine never generates PCM. The host synthesizes two pulse waves (or ignores them).

Bitsy's `bitsy.sound(channel, duration, frequency, volume, pulse)` uses decihertz and a volume of 0–15. citsy converts before `present()`: `frequency_hz` is hertz, and `volume` is `n / 15`. Duration stays in milliseconds. The three pulse duties are `Eighth` (1/8), `Quarter` (1/4), and `Half` (1/2). See [Sound](bitsy/sound.md).

## Text modes

`! TXT_MODE` in the game file selects this. The editor calls `0` "default" and `1` "chunky".

| Mode | File | System constant | Blit scale | Internal resolution |
|---|---|---|---|---|
| `TextMode::HiRez` | `0` | `TXT_HIREZ` | 2× | Twice the main display |
| `TextMode::LoRez` | `1` | `TXT_LOREZ` | 4× | Same as the main display |

The web runtime uses a game scale of 4 and a text scale of 2, and switches the text scale to 4 in lorez. Lorez maps one text pixel onto one game pixel. Hirez maps one text pixel onto half a game pixel, so the letters are smaller and the textbox holds more detail. citsy always fills a 104×32 textbox and reports the mode so the host can pick the scale. Position (`x`, `y`) is in 128×128 game pixels, matching `bitsy.textbox(visible, x, y, w, h)`.

## Graphics modes

- **`GraphicsMode::Map`** — Default gameplay. Compose the room from tile IDs in `map1`, then sprites and items from `map2`.
- **`GraphicsMode::Video`** — Transitions and effects. Read individual pixel color indices from `video`.

Switch on the `gfx_mode` argument to `present()`. The unused buffer is still passed; ignore it.

## Palette updates

When a game or transition changes colors, the engine updates its internal palette and passes the full RGB table to `present()`. Index 0 is background, 1 is tile, 2 is sprite; further indices are extended palette colors.

## System API mapping

`citsy::Host` is the C++ shape of the [`bitsy` global](https://make.bitsy.org/docs/technical/system/) (system API v0.2). In the JavaScript runtime the engine calls into that object on every frame. citsy keeps the same split — the core never opens a window — and pushes one snapshot through `present()` each frame, covering the blocks that `set`, `fill`, and `sound` update upstream.

| System API | citsy |
|---|---|
| `bitsy.loop(fn)` with `dt` in milliseconds, aiming at 60 fps | The host loop calls `Engine::update`. `delta_time_ms()` is that `dt` |
| `bitsy.button(code)` | `Host::button` |
| `bitsy.log(message)` | `Host::log` |
| `bitsy.graphicsMode` (`GFX_MAP`, `GFX_VIDEO`) | `GraphicsMode` argument to `present()` |
| `bitsy.textMode` (`TXT_HIREZ`, `TXT_LOREZ`) | `TextMode` argument |
| `bitsy.color(index, r, g, b)` | The `palette` span. Components are 0–255 |
| `bitsy.textbox(visible, x, y, w, h)` | `TextboxView` |
| `bitsy.fill` / `bitsy.set` on `VIDEO`, `MAP1`, `MAP2` | `video`, `map1`, `map2` spans, already filled |
| `bitsy.sound` / `frequency` / `volume` on `SOUND1`, `SOUND2` | `sound1`, `sound2` |
| `TILE_SIZE` 8, `MAP_SIZE` 16, `VIDEO_SIZE` 128 | `kTileSize`, `kMapSize`, `kVideoSize` |
| `getGameData()` / `getFontData()` | The host loads `.bitsy` (and any `.bitsyfont`) and passes the text to `Engine` |

`bitsy.tile()`, `bitsy.delete()`, and writing individual tile pixels are internal to the JS engine. citsy owns the tile cache and does not ask the host to allocate tile memory.

The system API is still marked experimental upstream and can change before 1.0. citsy's `present()` signature is the stable surface for hosts.

## Backends

| Backend | Status | Use case |
|---|---|---|
| **MockHost** | Implemented | Unit tests; records `PresentSnapshot` per frame |
| **32blit** | Implemented | Companion [citsy-32blit](https://github.com/TeriyakiGod/citsy-32blit) player |

MockHost is header-only (`backends/mock/mock_host.hpp`). It stores every `present()` call so tests can assert on buffer sizes, palette colors, and graphics mode without a display. See [Testing](testing.md#mockhost).

### Implementing `present()`

- **32blit** — Paletted blit of the 128×128 index buffer; square-wave audio on `channels[]`. Implemented in [citsy-32blit](https://github.com/TeriyakiGod/citsy-32blit).
- **MockHost** — Record snapshots; no window.

!!! warning "Keep platform code out of core"

    The `citsy` library must not link 32blit, SDL, OpenGL, or any audio/window library. Display hosts are separate projects.
