#pragma once

#include <citsy/host.hpp>
#include <citsy/types.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace citsy {

// ---------------------------------------------------------------------------
// Parse error
// ---------------------------------------------------------------------------

/// Thrown by Engine constructors and citsy::parse() when the .bitsy input is
/// malformed or unsupported.
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg, int line = -1)
        : std::runtime_error(line >= 0
              ? "line " + std::to_string(line) + ": " + msg
              : msg)
        , line_(line)
    {}

    /// 1-based line number where the error was detected, or -1 if unknown.
    [[nodiscard]] int line() const noexcept { return line_; }

private:
    int line_;
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

/// The citsy game engine.
///
/// Owns a parsed Game, runs the simulation, and drives the Host on each
/// update.  Construct from a .bitsy text string or from a file path.
///
/// ```cpp
/// auto engine = citsy::Engine::from_file("game.bitsy");
/// engine.start();
/// while (engine.is_running()) {
///     engine.update(my_host);
/// }
/// ```
class Engine {
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Parse @p bitsy_text and construct the engine.
    ///
    /// @throws ParseError   if the data is malformed.
    explicit Engine(std::string_view bitsy_text);

    /// Load a .bitsy file from disk and construct the engine.
    ///
    /// @throws ParseError           if the data is malformed.
    /// @throws std::ios_base::failure if the file cannot be read.
    [[nodiscard]] static Engine from_file(const std::string& path);

    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Initialise runtime state and call Host::on_engine_ready().
    ///
    /// Must be called before the first update().
    void start(Host& host);

    /// Run one simulation step and call Host::present().
    ///
    /// @pre start() must have been called.
    void update(Host& host);

    /// True once start() has run. Like Bitsy, the game never stops by itself:
    /// after an ending, or when Menu is released, it restarts from the title.
    [[nodiscard]] bool is_running() const noexcept;

    // -----------------------------------------------------------------------
    // Runtime inspection (useful for tests and hosts without a font yet)
    // -----------------------------------------------------------------------

    /// Id of the room the avatar is currently in.
    [[nodiscard]] std::string current_room_id() const;

    [[nodiscard]] int avatar_x() const noexcept;
    [[nodiscard]] int avatar_y() const noexcept;

    /// True while a dialog box is open.
    [[nodiscard]] bool dialog_active() const noexcept;

    /// Text of the current dialog page, or empty if no dialog is open.
    [[nodiscard]] std::string_view dialog_line() const noexcept;

    /// All text dialogs have shown since the last call, a page per line, with
    /// no screen layout (for tests and tools).
    [[nodiscard]] std::string take_dialog_text();

    /// How many of @p item_id the avatar currently holds (id or NAME).
    [[nodiscard]] int inventory_count(std::string_view item_id) const;

    /// Alias for inventory_count (Phase 2 name).
    [[nodiscard]] int item_count(std::string_view id_or_name) const {
        return inventory_count(id_or_name);
    }

    /// Current value of a Bitsy variable (empty if unset).
    [[nodiscard]] std::string variable(std::string_view name) const;

    /// Alias for variable (Phase 2 name).
    [[nodiscard]] std::string variable_value(std::string_view name) const {
        return variable(name);
    }

    /// True after an ending has been reached. The next button press (once its
    /// text is over) restarts the game, as in Bitsy.
    [[nodiscard]] bool ending_active() const noexcept;

    /// True while the title or an ending is shown over a cleared screen.
    [[nodiscard]] bool narrating() const noexcept;

    /// Items the avatar holds (count above 0), sorted by item id.
    [[nodiscard]] std::vector<std::pair<std::string, double>> inventory() const;

    /// Names of all script variables, sorted.
    [[nodiscard]] std::vector<std::string> variable_names() const;

    /// A variable's value as JSON (`1`, `true`, `"text"`, `null` when unset).
    [[nodiscard]] std::string variable_json(std::string_view name) const;

    /// Current flipbook animation frame index (advances every 400 ms).
    [[nodiscard]] int anim_frame() const noexcept;

    /// Sprite id used for the avatar's appearance (AVA / {ava}).
    [[nodiscard]] std::string avatar_appearance() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace citsy
