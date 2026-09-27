# Sound

Bitsy games use two square-wave channels. citsy turns blips and tunes into `SoundChannel` parameters and passes them to `Host::present()`. The host plays the waves. The engine does not write PCM.

This page restates the [blip](https://make.bitsy.org/docs/tools/blip/) and [tune](https://make.bitsy.org/docs/tools/tune/) tools, plus the sound section of the [Bitsy System API](https://make.bitsy.org/docs/technical/system/) (v0.2).

---

## Channels

| Channel | System id | What citsy plays on it |
|---|---|---|
| 1 | `SOUND1` | Melody, and blips (a blip borrows this channel) |
| 2 | `SOUND2` | Harmony |

A blip pauses the tune until the blip finishes. Opening a dialog pauses the tune and resumes it when the box closes. While a room transition is on screen, the tune clock does not advance.

### Pulse waves

The system chip has three duty cycles. Tune instruments use the same three tones.

| System constant | Tune tone | `PulseWave` | Duty |
|---|---|---|---|
| `PULSE_1_8` | `P8` | `Eighth` | 1/8 |
| `PULSE_1_4` | `P4` | `Quarter` | 1/4 |
| `PULSE_1_2` | `P2` | `Half` | 1/2 (square) |

Melody and harmony can each pick a tone (`instrument_a`, `instrument_b`).

### Units

The system API and `present()` use different units.

| | Bitsy `bitsy.sound` | citsy `SoundChannel` |
|---|---|---|
| Frequency | decihertz (Hz × 10) | `frequency_hz` in hertz |
| Volume | integer 0–15 | `volume` in the range 0.0–1.0 (`n / 15`) |
| Duration | milliseconds | `duration_ms` |
| Pulse | `PULSE_*` constant | `PulseWave` |

Tune notes are emitted at volume 5 on that 0–15 scale (`volume` ≈ 0.333). Blip loudness follows the envelope and is divided by 15 the same way.

---

## Blips

A blip is a short effect. It can play when the avatar walks into a sprite, when an item is picked up, from dialog (`{blip "id"}`), or as a note inside a tune.

The file stores the result of the editor's generators (pickup, greeting, bloop, and the rest). Those generators are authoring aids. At runtime a blip is:

| Field | Meaning |
|---|---|
| Up to three pitches | Notes the effect steps through |
| Envelope | Attack, decay, sustain (0–15), length, and release, in milliseconds |
| Beat | Delay before the first pitch change, then time between changes |
| Instrument | One of the three pulse waves |
| Repeat | Whether the pitch pattern loops |

citsy plays the pitches in order on channel 1 for the envelope's total length, and scales sustain by the envelope.

---

## Tunes

A tune is looping music. A room can select one (`TUNE`), and dialog can switch tunes with `{tune "id"}`. `{tune "0"}` or an empty id stops the music.

### Bars

A tune has a **melody** and a **harmony**. Each is a list of bars, up to 16. A bar is 16 steps long (`kBarLength`). A step is a sixteenth note: a pitch with a length in steps, a rest (`beats == 0`), or a blip id instead of a pitched note.

The editor's piano roll writes those steps. Four octaves are available. Bitsy numbers them `0`–`3`, where `2` is the octave that contains middle C (C4 ≈ 261.7 Hz). citsy maps note `0` in octave `2` to that C and shifts by powers of two for the other octaves.

### Tempo

Tempo is how long one sixteenth lasts. The editor names match these values:

| File | Editor name | Marking | Sixteenth |
|---|---|---|---|
| `SLW` | Slow | 60 bpm (adagio) | 250 ms |
| `MED` | Medium | 80 bpm (andante) | 188 ms |
| `FST` | Fast | 120 bpm (moderato) | 125 ms |
| `XFST` | Turbo | 160 bpm (allegro) | 94 ms |

`188` and `94` are the nearest millisecond to 80 and 160 bpm.

### Key

The editor can limit which scale degrees are available. The file stores the allowed degrees and their chromatic notes. citsy skips a solfa note whose degree is outside that scale.

| Editor key | Scale |
|---|---|
| Major | C major pentatonic |
| Minor | C minor pentatonic |
| Full major | C major |
| Full minor | C minor |
| Chromatic | All 12 notes. Arpeggios are off in this mode |

### Harmony strum

When arpeggio mode is on, a harmony step names the start of a pattern. With strum off, each step is one held note.

| `Arpeggio` | Editor name | Pattern |
|---|---|---|
| `Off` | Strum off | Notes are entered one by one |
| `Up` | Strum chord (up) | Four-note chord, upward |
| `Down` | Strum chord (down) | Four-note chord, downward |
| `Int5` | Strum interval (small) | Two notes a small interval apart |
| `Int8` | Strum interval (big) | Two notes an octave apart |

---

## Sources

- [Blip](https://make.bitsy.org/docs/tools/blip/)
- [Tune](https://make.bitsy.org/docs/tools/tune/)
- [Sound actions](https://make.bitsy.org/docs/tools/dialog/soundActions/)
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — `bitsy.sound`, pulse-wave constants
