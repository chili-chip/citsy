// Plays every tests/parity/cases/*.case in citsy and compares the trace with
// the .golden file the Bitsy 8.15 reference engine wrote for the same steps
// (tests/parity/bitsy_reference.mjs). Formats: tests/parity/README.md.

#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include "backends/mock/mock_host.hpp"
#include "src/dialog/value.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Keep in step with bitsy_reference.mjs.
constexpr double kFrameMs = 16;
constexpr double kSettleMs = 6000;
constexpr int kMaxReadPages = 200;

std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

std::string json(const std::string& s) { return citsy::DialogValue::from_string(s).to_json(); }

/// Collapse whitespace runs to one space and trim, like the reference.
std::string normalize(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

struct Player {
    citsy::Engine engine;
    citsy::MockHost host;

    explicit Player(std::string_view game) : engine(game) {
        host.dt_ms = kFrameMs;
        engine.start(host);
    }

    void frames(double ms) {
        for (double t = 0; t < ms; t += kFrameMs) engine.update(host);
    }

    void press(citsy::Button b) {
        host.set_button(b, true);
        engine.update(host);
        host.set_button(b, false);
        engine.update(host);
    }

    // Text is printed once its dialog has closed (see bitsy_reference.mjs).
    void shown(std::vector<std::string>& out) {
        if (engine.dialog_active()) return;
        const std::string text = normalize(engine.take_dialog_text());
        if (!text.empty()) out.push_back("text " + json(text));
    }

    void snapshot(std::vector<std::string>& out) const {
        out.push_back("room " + engine.current_room_id() + " at " +
                      std::to_string(engine.avatar_x()) + "," + std::to_string(engine.avatar_y()));
        const char* mode = engine.ending_active() ? "ending"
                         : engine.narrating()     ? "narrating"
                                                  : "playing";
        out.push_back(std::string("mode ") + mode);
        if (engine.dialog_active()) {
            out.push_back("dialog open");
            return;
        }
        out.push_back("dialog -");
        std::string inv;
        for (const auto& [id, n] : engine.inventory()) {
            inv += (inv.empty() ? "" : " ") + id + "=" + citsy::js_number_to_string(n);
        }
        out.push_back("inventory " + (inv.empty() ? std::string("-") : inv));
        for (const auto& name : engine.variable_names()) {
            out.push_back("var " + name + " = " + engine.variable_json(name));
        }
    }
};

std::vector<std::string> split_words(std::string_view line) {
    std::vector<std::string> words;
    std::istringstream ss{std::string(line)};
    for (std::string w; ss >> w;) words.push_back(w);
    return words;
}

std::string run_case(const fs::path& case_file) {
    const std::string text = read_file(case_file);
    std::vector<std::vector<std::string>> steps;
    std::vector<std::string> step_lines;
    std::string game;
    std::istringstream lines(text);
    for (std::string raw; std::getline(lines, raw);) {
        if (auto hash = raw.find('#'); hash != std::string::npos) raw.resize(hash);
        auto words = split_words(raw);
        if (words.empty()) continue;
        if (words[0] == "game") {
            const auto start = raw.find_first_not_of(" \t", raw.find("game") + 4);
            auto end = raw.find_last_not_of(" \t\r");
            game = raw.substr(start, end - start + 1);
            continue;
        }
        std::string joined;
        for (const auto& w : words) joined += (joined.empty() ? "" : " ") + w;
        step_lines.push_back(joined);
        steps.push_back(std::move(words));
    }
    REQUIRE_FALSE(game.empty());

    Player p(read_file(case_file.parent_path() / game));
    std::vector<std::string> out;
    p.frames(kSettleMs);
    out.push_back("> start");
    p.shown(out);
    p.snapshot(out);

    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto& words = steps[i];
        out.push_back("> " + step_lines[i]);
        if (words[0] == "press") {
            for (std::size_t j = 1; j < words.size(); ++j) {
                const std::string& b = words[j];
                citsy::Button btn = citsy::Button::Ok;
                if (b == "up") btn = citsy::Button::Up;
                else if (b == "down") btn = citsy::Button::Down;
                else if (b == "left") btn = citsy::Button::Left;
                else if (b == "right") btn = citsy::Button::Right;
                else if (b == "ok") btn = citsy::Button::Ok;
                else if (b == "menu") btn = citsy::Button::Menu;
                else FAIL("unknown button " << b);
                p.press(btn);
                p.frames(kSettleMs);
            }
        } else if (words[0] == "read") {
            for (int n = 0; n < kMaxReadPages && p.engine.dialog_active(); ++n) {
                p.press(citsy::Button::Ok);
                p.frames(kSettleMs);
            }
        } else if (words[0] == "wait") {
            p.frames(words.size() > 1 ? std::stod(words[1]) : kSettleMs);
        } else {
            FAIL("unknown command " << words[0]);
        }
        p.shown(out);
        p.snapshot(out);
    }

    std::string trace;
    for (const auto& l : out) trace += l + "\n";
    return trace;
}

} // namespace

TEST_CASE("parity: citsy plays each case like Bitsy 8.15", "[parity]") {
    const fs::path cases = fs::path(__FILE__).parent_path().parent_path() / "parity" / "cases";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(cases)) {
        if (entry.path().extension() == ".case") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    REQUIRE_FALSE(files.empty());

    for (const auto& file : files) {
        DYNAMIC_SECTION(file.stem().string()) {
            auto golden = file;
            golden.replace_extension(".golden");
            CHECK(run_case(file) == read_file(golden));
        }
    }
}
