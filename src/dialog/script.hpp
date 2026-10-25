#pragma once

// Bitsy 8.15 dialog scripts: a C++ port of the reference engine's script.js
// parser and interpreter, so scripts give the same results as in Bitsy.
//
// A script is parsed once per dialog id into a tree. Sequences, cycles and
// shuffles keep their position in that tree, like Bitsy's nodes do. Running a
// script produces dialog pages for the textbox; it pauses at `{pg}` until the
// player continues, and finishes when the script does.

#include "src/dialog/value.hpp"
#include "src/font/font.hpp"
#include "src/model/game.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace citsy {

/// Engine callbacks the script uses to read and change the game. Ids passed
/// in are already resolved from names (Bitsy checks names first).
struct DialogWorld {
    std::function<std::optional<DialogValue>(std::string_view)> get_var;
    std::function<void(std::string_view, const DialogValue&)> set_var;

    std::function<double(std::string_view)> item_count;      ///< item id
    std::function<void(std::string_view, double)> set_item;   ///< `{item id n}` sets the count

    /// Properties of the exit or ending that runs the script (only `locked`).
    std::function<bool(std::string_view)> has_property;
    std::function<DialogValue(std::string_view)> get_property;
    std::function<void(std::string_view, const DialogValue&)> set_property;

    /// `{ava}` `{pal}` `{tune}`: set the id when given, and return the current one.
    std::function<DialogValue(std::optional<std::string>)> avatar;
    std::function<DialogValue(std::optional<std::string>)> palette;
    std::function<DialogValue(std::optional<std::string>)> tune;
    std::function<void(std::string_view)> play_blip;

    /// `{exit room x y [effect]}`. Without x and y the player does not move.
    std::function<DialogValue(std::string room, std::optional<int> x, std::optional<int> y,
                              std::optional<std::string> effect)> do_exit;
    std::function<void()> do_end;

    /// Look up a name in Bitsy's name maps (room, tile, sprite, item, palette,
    /// tune, blip). Returns the id, or @p key itself when no name matches.
    std::function<std::string(std::string_view kind, std::string_view key)> resolve;

    /// First frame of a drawing for `{drwt}` `{drws}` `{drwi}`; null if missing.
    std::function<const TileFrame*(char kind, std::string_view id)> drawing;
};

namespace script {
struct Node;
}

class DialogVM {
public:
    DialogVM();
    ~DialogVM();
    DialogVM(DialogVM&&) noexcept;
    DialogVM& operator=(DialogVM&&) noexcept;

    /// Stop any running script and forget compiled scripts (and so the
    /// position of every sequence, cycle and shuffle), as on a restart.
    void reset();

    /// Seed for `{shuffle}`. Bitsy uses Math.random; citsy uses the same
    /// shuffle with a seeded generator so runs can be repeated.
    void set_random_seed(std::uint32_t seed) { rng_state_ = seed; }

    /// Run @p source. A non-empty @p script_id compiles it once and reuses it,
    /// so list positions carry over between runs (Bitsy's script ids). When
    /// the script shows no text, @p on_end runs before start() returns.
    void start(std::string_view source, DialogWorld world,
               std::string script_id = {}, std::function<void()> on_end = {});

    /// Show the next page. Returns false once the dialog is over (and then
    /// on_end has run).
    bool continue_page();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::vector<TextSpan>& spans() const;
    [[nodiscard]] std::string plain_text() const { return spans_to_plain(spans()); }

private:
    struct Frame {
        script::Node* block;
        std::size_t   next = 0;
    };

    std::unordered_map<std::string, std::unique_ptr<script::Node>> compiled_;
    std::unique_ptr<script::Node> anonymous_;

    DialogWorld world_;
    std::function<void()> on_end_;
    std::vector<Frame> stack_;
    bool paused_ = false;   ///< waiting at `{pg}`
    bool active_ = false;

    std::vector<std::vector<TextSpan>> pages_;
    std::size_t page_ = 0;
    bool last_was_break_ = false;

    struct Effect {
        std::string name;
        DialogValue param;
    };
    std::vector<Effect> effects_;

    std::uint32_t rng_state_ = 0xC15A9u;

    void run();
    void finish();
    void exec_statement(script::Node& node);
    DialogValue eval(script::Node& node);
    DialogValue eval_block(script::Node& node);
    DialogValue call(const std::string& name, std::vector<DialogValue>& args);
    script::Node* choose(script::Node& list);

    // Dialog buffer (Bitsy's DialogBuffer, minus layout).
    void add_text(const std::string& text);
    void add_linebreak();
    void add_pagebreak();
    void add_drawing(const TileFrame& frame);
    std::vector<TextSpan>& cur_page();
    [[nodiscard]] bool has_effect(std::string_view name) const;
    void push_effect(std::string name, DialogValue param = {});
    void pop_effect(std::string_view name);
    void toggle_effect(std::string name);
    [[nodiscard]] std::uint8_t effect_bits() const;
    [[nodiscard]] int effect_color() const;

    double random();
    friend struct ScriptAccess;
};

/// Plain text of every page of a dialog with no world (for tests): runs the
/// script to the end, pressing continue at each page.
[[nodiscard]] std::vector<std::string> extract_dialog_pages(std::string_view content);

using Value = DialogValue;

struct DialogExit {
    std::string room_id;
    int         x = 0;
    int         y = 0;
    std::string effect;
};

struct DialogResult {
    std::vector<std::string>  pages;
    bool                      end_game = false;
    std::optional<DialogExit> exit;
};

/// A script with its own VM, so list positions persist between runs (tests).
struct DialogScript {
    std::string source;
    DialogVM    vm;
};

[[nodiscard]] DialogScript parse_dialog_script(std::string_view source);

/// Run @p script to the end, pressing continue at every page (tests).
[[nodiscard]] DialogResult run_dialog_script(DialogScript& script, DialogWorld world);

} // namespace citsy
