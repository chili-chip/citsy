// Browser host for the citsy engine. The core stays headless; this file is
// the Emscripten display backend used by the Chili creator's play mode.

#include <citsy/engine.hpp>
#include <citsy/host.hpp>
#include <citsy/types.hpp>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr int kVideo = citsy::kVideoSize;
constexpr int kPixels = kVideo * kVideo;

class WebHost final : public citsy::Host {
public:
    double dt_ms = 16.667;
    bool keys[6] = {};

    std::array<std::uint8_t, kPixels * 4> rgba{};
    float sound[8] = {};

    double delta_time_ms() const override { return dt_ms; }

    bool button(citsy::Button code) const override {
        const int i = static_cast<int>(code);
        return i >= 0 && i < 6 && keys[i];
    }

    void present(
        citsy::GraphicsMode /*gfx_mode*/,
        citsy::TextMode /*txt_mode*/,
        std::span<const citsy::Color> palette,
        std::span<const std::uint8_t> video,
        std::span<const std::uint8_t> /*map1*/,
        std::span<const std::uint8_t> /*map2*/,
        citsy::TextboxView textbox,
        citsy::SoundChannel sound1,
        citsy::SoundChannel sound2
    ) override {
        std::array<std::uint8_t, kPixels> indices{};
        const auto n = std::min(video.size(), indices.size());
        if (n > 0) {
            std::memcpy(indices.data(), video.data(), n);
        }
        overlay_textbox(indices, textbox);

        for (int i = 0; i < kPixels; ++i) {
            const auto index = indices[static_cast<std::size_t>(i)];
            const citsy::Color color = index < palette.size()
                ? palette[index]
                : citsy::Color{};
            rgba[static_cast<std::size_t>(i * 4 + 0)] = color.r;
            rgba[static_cast<std::size_t>(i * 4 + 1)] = color.g;
            rgba[static_cast<std::size_t>(i * 4 + 2)] = color.b;
            rgba[static_cast<std::size_t>(i * 4 + 3)] = 255;
        }

        pack_sound(0, sound1);
        pack_sound(1, sound2);
    }

private:
    static void overlay_textbox(
        std::array<std::uint8_t, kPixels>& video,
        const citsy::TextboxView& textbox
    ) {
        if (!textbox.visible || textbox.pixels.empty() ||
            textbox.width <= 0 || textbox.height <= 0) {
            return;
        }
        const int tw = textbox.width;
        const int th = textbox.height;
        const int x0 = std::max(0, textbox.x);
        const int y0 = std::max(0, textbox.y);
        const int x1 = std::min(kVideo, textbox.x + tw);
        const int y1 = std::min(kVideo, textbox.y + th);
        const int copy_w = x1 - x0;
        if (copy_w <= 0 || y1 <= y0) return;

        const int src_x = x0 - textbox.x;
        const int src_y = y0 - textbox.y;
        const auto* src = textbox.pixels.data();
        const std::size_t src_n = textbox.pixels.size();

        for (int row = 0; row < y1 - y0; ++row) {
            const std::size_t si =
                static_cast<std::size_t>((src_y + row) * tw + src_x);
            if (si >= src_n) break;
            const std::size_t n = std::min(
                static_cast<std::size_t>(copy_w), src_n - si);
            std::memcpy(
                &video[static_cast<std::size_t>((y0 + row) * kVideo + x0)],
                src + si,
                n);
        }
    }

    void pack_sound(int channel, const citsy::SoundChannel& sound_ch) {
        const int base = channel * 4;
        sound[base + 0] = sound_ch.active ? 1.f : 0.f;
        sound[base + 1] = static_cast<float>(sound_ch.frequency_hz);
        sound[base + 2] = sound_ch.volume;
        sound[base + 3] = static_cast<float>(static_cast<int>(sound_ch.pulse));
    }
};

WebHost g_host;
std::unique_ptr<citsy::Engine> g_engine;
std::string g_error;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int citsy_load(const char* text) {
    g_engine.reset();
    g_error.clear();
    g_host = WebHost{};
    if (!text) {
        g_error = "empty game data";
        return 0;
    }
    try {
        auto engine = std::make_unique<citsy::Engine>(text);
        engine->start(g_host);
        g_engine = std::move(engine);
        return 1;
    } catch (const std::exception& ex) {
        g_error = ex.what();
        g_engine.reset();
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE
void citsy_unload() {
    g_engine.reset();
}

EMSCRIPTEN_KEEPALIVE
void citsy_set_button(int index, int down) {
    if (index >= 0 && index < 6) {
        g_host.keys[index] = down != 0;
    }
}

EMSCRIPTEN_KEEPALIVE
int citsy_frame(double dt_ms) {
    if (!g_engine || !g_engine->is_running()) return 0;
    g_host.dt_ms = dt_ms > 0 ? dt_ms : 16.667;
    g_engine->update(g_host);
    return g_engine->is_running() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const std::uint8_t* citsy_pixels() {
    return g_host.rgba.data();
}

EMSCRIPTEN_KEEPALIVE
const float* citsy_sound() {
    return g_host.sound;
}

EMSCRIPTEN_KEEPALIVE
const char* citsy_last_error() {
    return g_error.c_str();
}

} // extern "C"
