#pragma once

// Two-channel square-wave parameter generator (blips + tunes), ported from
// Bitsy 8.15's sound.js. The host plays the SoundChannel values; this module
// never opens a device.

#include "src/model/game.hpp"

#include <citsy/types.hpp>

#include <array>
#include <string>
#include <vector>

namespace citsy {

/// The two channels as a host plays them, following the Bitsy System API:
/// sound() starts a note that lasts `duration` ms, and frequency() and
/// volume() change the note that is playing. Frequencies are in hundredths of
/// a hertz and volume is 0 to 15, as in Bitsy.
class SoundChannels {
public:
    static constexpr int kSound1 = 0;
    static constexpr int kSound2 = 1;

    void sound(int ch, double duration_ms, double centihz, double volume, PulseWave pulse);
    void frequency(int ch, double centihz);
    void volume(int ch, double volume);
    /// Let @p dt_ms of real time pass.
    void advance(double dt_ms);
    void clear();

    [[nodiscard]] SoundChannel channel(int ch) const;

private:
    struct Voice {
        double left_ms = 0;
        double centihz = 0;
        double volume = 0;
        PulseWave pulse = PulseWave::Half;
    };
    std::array<Voice, 2> voices_{};
};

class SoundPlayer {
public:
    /// Bitsy `playBlip`. A blip pauses the tune unless it is part of it.
    void play_blip(const Blip& blip, bool interrupt_music = true,
                   const Pitch* pitch = nullptr, const TuneKey* key = nullptr);
    void play_tune(const Tune& tune);
    void stop_tune();
    void pause_tune();
    void resume_tune();

    /// Let real time pass for the notes already playing. Call every frame.
    void advance(double dt_ms);
    /// Bitsy `soundPlayer.update`: step the blip and the tune.
    void update(double dt_ms, const Game& game);
    /// Silence everything and forget the tune (a restart).
    void reset();

    [[nodiscard]] SoundChannel channel1() const { return out_.channel(SoundChannels::kSound1); }
    [[nodiscard]] SoundChannel channel2() const { return out_.channel(SoundChannels::kSound2); }
    /// Bitsy `isBlipPlaying`: true while a blip holds the tune (and dialog) back.
    [[nodiscard]] bool blip_playing() const { return music_paused_for_blip_; }
    [[nodiscard]] bool tune_playing() const { return cur_tune_ != nullptr; }
    [[nodiscard]] std::string tune_id() const {
        return cur_tune_ ? cur_tune_->id : std::string{};
    }

private:
    struct Sfx {
        Blip blip;
        double pitch_index = -1;  ///< a double: Bitsy's index can be NaN
        std::vector<double> frequencies;
        double timer = 0;
        double duration = 0;
    };

    SoundChannels out_;

    const Tune* cur_tune_ = nullptr;
    bool tune_paused_ = false;
    int bar_index_ = -1;
    std::vector<Pitch> arpeggio_;
    double beat16_ = 0;
    double beat16_timer_ = 0;
    int beat16_index_ = 0;

    bool sfx_active_ = false;
    Sfx sfx_;
    bool music_paused_for_blip_ = false;

    void update_sfx(double dt_ms);
    void update_tune(double dt_ms, const Game& game);
    void play_note(const Pitch& pitch, PulseWave instrument, int channel,
                   const TuneKey* key, double beat_len);
    void play_tune_blip(const Pitch& pitch, const Game& game);
};

[[nodiscard]] double pitch_frequency_hz(const Pitch& pitch, const TuneKey* key = nullptr);
[[nodiscard]] PulseWave pulse_from_name(std::string_view name);
[[nodiscard]] int tempo_sixteenth_ms(Tempo t);

} // namespace citsy
