#include "src/sound/sound.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace citsy {
namespace {

// Bitsy sound.js `frequencies`: octave 4 (middle C), in hertz.
constexpr double kChromaticHz[12] = {
    261.7, 277.2, 293.7, 311.2, 329.7, 349.3,
    370.0, 392.0, 415.3, 440.0, 466.2, 493.9,
};

constexpr int kNoteCount = 12;
constexpr int kSolfaCount = 7;
constexpr int kOctaveCount = 4;
constexpr double kMaxVolume = 15.0;
constexpr double kNoteVolume = 5.0;
constexpr double kSfxPeakVolume = 10.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// JavaScript Math.min: NaN if either side is NaN.
double js_min(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return kNaN;
    return std::min(a, b);
}

/// JavaScript Math.max.
double js_max(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return kNaN;
    return std::max(a, b);
}

int pitch_to_steps(const Pitch& p) { return p.octave * kNoteCount + p.note; }

/// Bitsy stepsToPitch: a one-beat pitch, clamped to C2..B5.
Pitch steps_to_pitch(int steps) {
    Pitch p;
    p.beats = 1;
    p.note = 0;
    p.octave = 0;
    while (steps >= kNoteCount) {
        p.octave = (p.octave + 1) % kOctaveCount;
        steps -= kNoteCount;
    }
    p.note += steps;
    if (p.note <= -1) p.note = 0;
    else if (p.note >= kNoteCount) p.note = kNoteCount - 1;
    if (p.octave <= -1) p.octave = 0;
    else if (p.octave >= kOctaveCount) p.octave = kOctaveCount - 1;
    return p;
}

Pitch adjust_pitch(const Pitch& p, int step_delta) {
    return steps_to_pitch(pitch_to_steps(p) + step_delta);
}

bool scale_has(const TuneKey& key, int note) {
    return std::find(key.scale.begin(), key.scale.end(), note) != key.scale.end();
}

/// Key note for solfa degree @p degree, or -2 when there is none (Bitsy's
/// `undefined`, which fails every comparison).
int key_note(const TuneKey& key, int degree) {
    if (degree < 0 || degree >= kSolfaCount) return -2;
    return key.notes[static_cast<std::size_t>(degree)];
}

/// Bitsy isPitchPlayable. With a key, the note is a solfa degree.
bool is_pitch_playable(const Pitch& pitch, const TuneKey* key) {
    if (pitch.beats <= 0) return false;
    if (!key) return true;
    const int n = key_note(*key, pitch.note);
    return scale_has(*key, pitch.note) && n > -1 && n < kNoteCount;
}

/// Bitsy pitchToChromatic: with a key, map the solfa degree to its note.
Pitch to_chromatic(const Pitch& pitch, const TuneKey* key) {
    if (!key) return pitch;
    Pitch out = pitch;
    const int offset = pitch.note >= kSolfaCount ? 1 : 0;
    out.octave = pitch.octave + offset;
    out.note = key->notes[static_cast<std::size_t>(((pitch.note % kSolfaCount) + kSolfaCount) % kSolfaCount)];
    out.solfa = false;
    return out;
}

/// Bitsy makePitchFrequency, in hertz. NaN for a note past B.
double make_pitch_frequency(const Pitch& pitch) {
    const int note = std::max(0, pitch.note);
    const int octave = std::clamp(pitch.octave, 0, kOctaveCount - 1);
    if (note >= kNoteCount) return kNaN;
    return kChromaticHz[note] * std::pow(2.0, octave - 2);
}

int tempo_ms(Tempo t) {
    switch (t) {
        case Tempo::Slow:      return 250;
        case Tempo::Medium:    return 188;
        case Tempo::Fast:      return 125;
        case Tempo::ExtraFast: return 94;
    }
    return 188;
}

/// Bitsy arpeggioPattern, in scale degrees.
std::vector<int> arpeggio_steps(Arpeggio a) {
    switch (a) {
        case Arpeggio::Up:   return {0, 2, 4, 7};
        case Arpeggio::Down: return {7, 4, 2, 0};
        case Arpeggio::Int5: return {0, 4};
        case Arpeggio::Int8: return {0, 7};
        case Arpeggio::Off:  break;
    }
    return {};
}

/// Bitsy arpeggiateBar: the chord on the bar's first harmony note.
std::vector<Pitch> arpeggiate_bar(const std::array<Pitch, kBarLength>& bar,
                                  const TuneKey& key, Arpeggio pattern) {
    std::vector<Pitch> out;
    if (!is_pitch_playable(bar[0], &key)) return out;
    for (const int step : arpeggio_steps(pattern)) {
        Pitch p;
        p.beats = 1;
        p.note = bar[0].note + step;
        p.octave = bar[0].octave;
        out.push_back(to_chromatic(p, &key));
    }
    return out;
}

double sfx_volume_at(const Blip& blip, double time) {
    const auto& e = blip.envelope;
    const double attack = e.attack, decay = e.decay, length = e.length, release = e.release;
    if (time < attack) {
        return std::floor(kSfxPeakVolume * (time / attack));
    }
    if (time < attack + decay) {
        const double t = (time - attack) / decay;
        return std::floor(kSfxPeakVolume + (e.sustain - kSfxPeakVolume) * t);
    }
    if (time < attack + decay + length) {
        return e.sustain;
    }
    if (time < attack + decay + length + release) {
        const double t = (time - (attack + decay + length)) / release;
        return std::floor(e.sustain * (1 - t));
    }
    return 0;
}

PulseWave pulse_named(std::string_view n) {
    if (n == "P8") return PulseWave::Eighth;
    if (n == "P4") return PulseWave::Quarter;
    return PulseWave::Half;
}

} // namespace

// ---------------------------------------------------------------------------
// SoundChannels
// ---------------------------------------------------------------------------

void SoundChannels::sound(int ch, double duration_ms, double centihz, double volume,
                          PulseWave pulse) {
    voices_[static_cast<std::size_t>(ch)] = Voice{duration_ms, centihz, volume, pulse};
}

void SoundChannels::frequency(int ch, double centihz) {
    voices_[static_cast<std::size_t>(ch)].centihz = centihz;
}

void SoundChannels::volume(int ch, double volume) {
    voices_[static_cast<std::size_t>(ch)].volume = volume;
}

void SoundChannels::advance(double dt_ms) {
    for (auto& v : voices_) v.left_ms = std::max(0.0, v.left_ms - dt_ms);
}

void SoundChannels::clear() { voices_ = {}; }

SoundChannel SoundChannels::channel(int ch) const {
    const Voice& v = voices_[static_cast<std::size_t>(ch)];
    SoundChannel out;
    // NaN frequencies (a Bitsy quirk) fail the test and stay silent.
    if (!(v.left_ms > 0 && v.volume > 0 && v.centihz > 0)) return out;
    out.active = true;
    out.duration_ms = static_cast<int>(std::ceil(v.left_ms));
    out.frequency_hz = static_cast<int>(std::lround(v.centihz / 100.0));
    out.volume = static_cast<float>(std::clamp(v.volume / kMaxVolume, 0.0, 1.0));
    out.pulse = v.pulse;
    return out;
}

// ---------------------------------------------------------------------------
// SoundPlayer
// ---------------------------------------------------------------------------

PulseWave pulse_from_name(std::string_view name) { return pulse_named(name); }
int tempo_sixteenth_ms(Tempo t) { return tempo_ms(t); }

double pitch_frequency_hz(const Pitch& pitch, const TuneKey* key) {
    return make_pitch_frequency(to_chromatic(pitch, key));
}

void SoundPlayer::play_blip(const Blip& blip, bool interrupt_music, const Pitch* pitch,
                            const TuneKey* key) {
    music_paused_for_blip_ = interrupt_music;

    sfx_ = Sfx{};
    sfx_.blip = blip;
    const auto& e = blip.envelope;
    sfx_.duration = static_cast<double>(e.attack + e.decay + e.length + e.release);
    // A blip played as a tune note is moved to that note's pitch.
    int step = 0;
    if (pitch) step = pitch_to_steps(to_chromatic(*pitch, key)) - pitch_to_steps(blip.pitch_a);
    for (const Pitch* p : {&blip.pitch_a, &blip.pitch_b, &blip.pitch_c}) {
        if (p->beats > 0) sfx_.frequencies.push_back(make_pitch_frequency(adjust_pitch(*p, step)));
    }
    sfx_active_ = true;

    // Duration times 10 is Bitsy's guard against the note stopping early.
    out_.sound(SoundChannels::kSound1, sfx_.duration * 10,
               sfx_.frequencies.empty() ? 0 : sfx_.frequencies[0] * 100, 0,
               blip.instrument);
}

void SoundPlayer::play_tune(const Tune& tune) {
    cur_tune_ = &tune;
    beat16_timer_ = 0;
    beat16_index_ = -1;
    bar_index_ = 0;
    beat16_ = tempo_ms(tune.tempo);
    // Without a key Bitsy keeps the previous tune's arpeggio.
    if (tune.arpeggio != Arpeggio::Off && tune.key && !tune.harmony.empty()) {
        arpeggio_ = arpeggiate_bar(tune.harmony[0], *tune.key, tune.arpeggio);
    }
}

// Stopping or pausing leaves the notes already playing to finish, as in Bitsy.
void SoundPlayer::stop_tune() { cur_tune_ = nullptr; }
void SoundPlayer::pause_tune() { tune_paused_ = true; }
void SoundPlayer::resume_tune() { tune_paused_ = false; }

void SoundPlayer::advance(double dt_ms) { out_.advance(dt_ms); }

void SoundPlayer::reset() { *this = SoundPlayer{}; }

void SoundPlayer::update(double dt_ms, const Game& game) {
    update_sfx(dt_ms);
    if (!tune_paused_ && !music_paused_for_blip_) update_tune(dt_ms, game);
}

void SoundPlayer::update_sfx(double dt_ms) {
    dt_ms = std::min(dt_ms, 32.0);
    const bool any_blip = sfx_active_;

    if (sfx_active_) {
        sfx_.timer += dt_ms;
        if (sfx_.timer >= sfx_.duration) sfx_.timer = sfx_.duration;

        if (!sfx_.frequencies.empty()) {
            // Bitsy sfxFrequencyAtTime, NaN and all.
            const double n = static_cast<double>(sfx_.frequencies.size());
            const double delta =
                js_max(0, sfx_.timer - sfx_.blip.beat.delay) / sfx_.blip.beat.time;
            const double pitch_delta = sfx_.blip.do_repeat ? std::fmod(delta, n)
                                                           : js_min(delta, n - 1);
            const double prev = sfx_.pitch_index;
            sfx_.pitch_index = std::floor(pitch_delta);
            const double idx = sfx_.pitch_index;
            const double freq = (idx >= 0 && idx < n)
                ? sfx_.frequencies[static_cast<std::size_t>(idx)]
                : kNaN;
            if (prev != sfx_.pitch_index) out_.frequency(SoundChannels::kSound1, freq * 100);
            out_.volume(SoundChannels::kSound1, sfx_volume_at(sfx_.blip, sfx_.timer));
        }

        if (sfx_.timer >= sfx_.duration) {
            out_.volume(SoundChannels::kSound1, 0);
            sfx_active_ = false;
        }
    }

    if (music_paused_for_blip_ && !any_blip) music_paused_for_blip_ = false;
}

void SoundPlayer::play_note(const Pitch& pitch, PulseWave instrument, int channel,
                            const TuneKey* key, double beat_len) {
    if (pitch.beats <= 0) return;
    if (!is_pitch_playable(pitch, key)) return;
    const double freq = make_pitch_frequency(to_chromatic(pitch, key));
    out_.sound(channel, pitch.beats * beat_len, freq * 100, kNoteVolume, instrument);
}

void SoundPlayer::play_tune_blip(const Pitch& pitch, const Game& game) {
    const auto it = game.blips.find(pitch.blip_id);
    if (it == game.blips.end()) return;
    const TuneKey* key = cur_tune_->key ? &*cur_tune_->key : nullptr;
    play_blip(it->second, false, &pitch, key);
}

void SoundPlayer::update_tune(double dt_ms, const Game& game) {
    if (!cur_tune_ || cur_tune_->melody.empty()) return;
    const Tune& tune = *cur_tune_;
    const TuneKey* key = tune.key ? &*tune.key : nullptr;

    beat16_timer_ += dt_ms;
    if (beat16_timer_ < beat16_) return;
    beat16_timer_ = 0;
    ++beat16_index_;

    if (beat16_index_ >= kBarLength) {
        beat16_index_ = 0;
        bar_index_ = (bar_index_ + 1) % static_cast<int>(tune.melody.size());
        if (tune.arpeggio != Arpeggio::Off && key &&
            bar_index_ < static_cast<int>(tune.harmony.size())) {
            arpeggio_ = arpeggiate_bar(tune.harmony[static_cast<std::size_t>(bar_index_)],
                                       *key, tune.arpeggio);
        }
    }

    const auto bar = static_cast<std::size_t>(bar_index_);
    const auto beat = static_cast<std::size_t>(beat16_index_);

    // Melody on channel 1. Any melody note cancels a blip on that channel.
    const Pitch& a = tune.melody[bar][beat];
    if (a.beats > 0) sfx_active_ = false;
    if (!a.blip_id.empty() && a.beats > 0) {
        play_tune_blip(a, game);
    } else {
        play_note(a, tune.instrument_a, SoundChannels::kSound1, key, beat16_);
    }

    // Harmony (or the arpeggio) on channel 2.
    if (tune.arpeggio == Arpeggio::Off) {
        if (bar < tune.harmony.size()) {
            const Pitch& b = tune.harmony[bar][beat];
            if (!b.blip_id.empty() && b.beats > 0) {
                play_tune_blip(b, game);
            } else {
                play_note(b, tune.instrument_b, SoundChannels::kSound2, key, beat16_);
            }
        }
    } else if (!arpeggio_.empty()) {
        const Pitch& p = arpeggio_[beat % arpeggio_.size()];
        if (p.beats > 0) play_note(p, tune.instrument_b, SoundChannels::kSound2, nullptr, beat16_);
    }
}

} // namespace citsy
