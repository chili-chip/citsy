#include <citsy/engine.hpp>

#include "src/dialog/script.hpp"
#include "src/font/font.hpp"
#include "src/model/game.hpp"
#include "src/parser/parser.hpp"
#include "src/render/compose.hpp"
#include "src/sound/sound.hpp"
#include "src/transition/transition.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace citsy {
namespace {

constexpr double kFirstHoldMs  = 500.0;
constexpr double kRepeatHoldMs = 150.0;
constexpr double kAnimMs       = 400.0;

constexpr int kTextboxWidth  = 104;
constexpr int kTextboxHeight = 32;

enum class Dir { None = -1, Up = 0, Down = 1, Left = 2, Right = 3 };

[[nodiscard]] Dir read_direction(const Host& host) {
    if (host.button(Button::Up))    return Dir::Up;
    if (host.button(Button::Down))  return Dir::Down;
    if (host.button(Button::Left))  return Dir::Left;
    if (host.button(Button::Right)) return Dir::Right;
    return Dir::None;
}

[[nodiscard]] bool any_action_down(const Host& host) {
    return host.button(Button::Up) || host.button(Button::Down) ||
           host.button(Button::Left) || host.button(Button::Right) ||
           host.button(Button::Ok);
}

[[nodiscard]] int dir_dx(Dir d) {
    if (d == Dir::Left)  return -1;
    if (d == Dir::Right) return  1;
    return 0;
}

[[nodiscard]] int dir_dy(Dir d) {
    if (d == Dir::Up)    return -1;
    if (d == Dir::Down)  return  1;
    return 0;
}

} // namespace

// ===========================================================================
// Engine::Impl
// ===========================================================================

struct Engine::Impl {
    Game game;

    bool running = false;

    std::string current_room_id;
    int         avatar_x = 0;
    int         avatar_y = 0;
    std::string avatar_id{Game::kAvatarId};
    std::string palette_id = "0";
    std::string tune_id = "0";  ///< Bitsy's state.tune; "0" is silence

    std::unordered_map<std::string, std::vector<RoomItem>> room_items;
    std::unordered_map<std::string, double> inventory;
    std::unordered_map<std::string, DialogValue> variables;

    // The `locked` property of each exit and ending in the current room. Bitsy
    // creates these when a room is entered, so locks only last for one visit.
    std::vector<std::shared_ptr<DialogValue>> exit_locks;
    std::vector<std::shared_ptr<DialogValue>> ending_locks;
    std::shared_ptr<DialogValue> dialog_context;  ///< lock of the exit/ending running the dialog

    Dir    cur_dir        = Dir::None;
    double hold_timer_ms  = 0.0;
    bool   any_held       = false;
    bool   ignore_input   = false;
    bool   menu_held      = false;

    DialogVM                   dlg;
    std::string                dialog_plain;
    std::vector<std::uint8_t>  textbox_pixels;
    std::vector<std::vector<TextSpan>> textbox_pages_;
    std::size_t                textbox_page_ = 0;
    double                     dialog_time_ms = 0;
    double                     type_page_start_ms_ = 0;
    bool                       type_skip_ = false;
    bool                       textbox_centered_ = false;
    std::string                dialog_log_;  ///< text shown since take_dialog_text()

    bool narrating = false;  ///< title or ending text over a cleared screen
    bool ending    = false;  ///< game over; the next press restarts
    bool game_over = false;  ///< restart at the end of this update

    BitsyFont   font;
    SoundPlayer sound;
    Transition  transition;
    GraphicsMode gfx_mode = GraphicsMode::Map;

    double anim_counter_ms = 0;
    int    anim_frame      = 0;

    std::array<std::uint8_t, kVideoSize * kVideoSize> video{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map1{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map2{};
    std::vector<Color>                                palette;

    // Skip compose_room() when the visible room has not changed.
    bool          composed_valid_ = false;
    bool          composed_narrating_ = false;
    int           composed_ax_ = -1;
    int           composed_ay_ = -1;
    int           composed_anim_ = -1;
    std::uint32_t items_generation_ = 0;
    std::uint32_t composed_items_gen_ = 0;
    std::string   composed_room_;
    std::string   composed_avatar_;

    bool        textbox_was_active_ = false;
    bool        textbox_pages_dirty_ = true;
    int         last_revealed_ = -1;
    bool        last_show_arrow_ = false;
    std::size_t last_textbox_page_ = static_cast<std::size_t>(-1);

    explicit Impl(Game g) : game(std::move(g)) {
        // A FONT segment wins; otherwise DEFAULT_FONT names a built-in font.
        if (!game.font_data.empty()) {
            font = parse_bitsyfont(game.font_data);
        } else if (auto builtin = builtin_font(game.font_name)) {
            font = std::move(*builtin);
        } else {
            font = default_font();
        }
        seed_palette_fallback();
    }

    void seed_palette_fallback() {
        if (!game.palettes.empty()) {
            auto it = game.palettes.find("0");
            if (it == game.palettes.end()) it = game.palettes.begin();
            palette = it->second.colors;
        }
        while (palette.size() < 3) palette.push_back({});
    }

    [[nodiscard]] const Room* current_room() const {
        auto it = game.rooms.find(current_room_id);
        return it != game.rooms.end() ? &it->second : nullptr;
    }

    [[nodiscard]] std::vector<RoomItem>& items_in(const std::string& room_id) {
        return room_items[room_id];
    }

    void load_room_palette() {
        const Room* room = current_room();
        std::string pal_id = (room && !room->palette_id.empty()) ? room->palette_id : "0";
        apply_palette(pal_id);
    }

    void apply_palette(const std::string& pal_id) {
        palette_id = pal_id;
        const Palette* pal = game.find_palette(pal_id);
        if (!pal && !game.palettes.empty()) pal = &game.palettes.begin()->second;
        if (pal) palette = pal->colors;
        while (palette.size() < 3) palette.push_back({});
    }

    /// Bitsy's initRoom tune rule: stop for no tune, and only restart the
    /// music when the room's tune differs from the one playing.
    void apply_room_tune() {
        const Room* room = current_room();
        const std::string tid = room ? room->tune_id : std::string{};
        const Tune* t = (tid.empty() || tid == "0") ? nullptr : game.find_tune(tid);
        if (!t) {
            tune_id = "0";
            sound.stop_tune();
        } else if (tid != tune_id) {
            tune_id = tid;
            sound.play_tune(*t);
        }
    }

    void apply_room_avatar() {
        const Room* room = current_room();
        avatar_id = (room && !room->avatar_id.empty()) ? room->avatar_id
                                                       : std::string(Game::kAvatarId);
    }

    /// Bitsy looks a script argument up in the name map first, then uses it as an id.
    template <typename T>
    static std::string id_for_name(const IdMap<T>& map, std::string_view key) {
        for (const auto& [id, obj] : map) {
            if (!obj.name.empty() && obj.name == key) return id;
        }
        return std::string(key);
    }

    [[nodiscard]] std::string resolve_name(std::string_view kind, std::string_view key) const {
        if (kind == "room")    return id_for_name(game.rooms, key);
        if (kind == "tile")    return id_for_name(game.tiles, key);
        if (kind == "sprite")  return id_for_name(game.sprites, key);
        if (kind == "item")    return id_for_name(game.items, key);
        if (kind == "palette") return id_for_name(game.palettes, key);
        if (kind == "tune")    return id_for_name(game.tunes, key);
        if (kind == "blip")    return id_for_name(game.blips, key);
        return std::string(key);
    }

    [[nodiscard]] const TileFrame* drawing_frame(char kind, std::string_view id) const {
        const std::vector<TileFrame>* frames = nullptr;
        if (kind == 't') {
            if (auto it = game.tiles.find(id); it != game.tiles.end()) frames = &it->second.frames;
        } else if (kind == 's') {
            if (auto it = game.sprites.find(id); it != game.sprites.end()) frames = &it->second.frames;
        } else {
            if (auto it = game.items.find(id); it != game.items.end()) frames = &it->second.frames;
        }
        return frames && !frames->empty() ? &frames->front() : nullptr;
    }

    void play_blip_id(const std::string& id) {
        if (id.empty()) return;
        if (const Blip* b = game.find_blip(id)) sound.play_blip(*b);
    }

    DialogWorld make_world() {
        DialogWorld w;
        w.get_var = [this](std::string_view name) -> std::optional<DialogValue> {
            auto it = variables.find(std::string(name));
            if (it == variables.end()) return std::nullopt;
            return it->second;
        };
        w.set_var = [this](std::string_view name, const DialogValue& v) {
            variables[std::string(name)] = v;
        };
        w.item_count = [this](std::string_view id) {
            auto it = inventory.find(std::string(id));
            return it == inventory.end() ? 0.0 : it->second;
        };
        w.set_item = [this](std::string_view id, double count) {
            inventory[std::string(id)] = count;
        };
        w.has_property = [this](std::string_view name) {
            return dialog_context && name == "locked";
        };
        w.get_property = [this](std::string_view) {
            return dialog_context ? *dialog_context : DialogValue::null();
        };
        w.set_property = [this](std::string_view, const DialogValue& v) {
            if (dialog_context) *dialog_context = v;
        };
        w.avatar = [this](std::optional<std::string> id) {
            if (id) avatar_id = *id;
            return DialogValue::from_string(avatar_id);
        };
        w.palette = [this](std::optional<std::string> id) {
            if (id) apply_palette(*id);
            return DialogValue::from_string(palette_id);
        };
        w.tune = [this](std::optional<std::string> id) {
            if (id) {
                if (*id == "0") {
                    sound.stop_tune();
                } else if (*id != tune_id) {
                    if (const Tune* t = game.find_tune(*id)) sound.play_tune(*t);
                }
                tune_id = *id;
            }
            return DialogValue::from_string(tune_id);
        };
        w.play_blip = [this](std::string_view id) { play_blip_id(std::string(id)); };
        w.do_exit = [this](std::string room, std::optional<int> x, std::optional<int> y,
                           std::optional<std::string> fx) {
            if (x && y) {
                Exit ext;
                ext.dest_room_id = std::move(room);
                ext.dest_x = *x;
                ext.dest_y = *y;
                if (fx) ext.transition_effect = *fx;
                // Bitsy's script waits for the transition and then goes on in
                // the new room; citsy's runs a page at once, so it moves first.
                go_to(ext, /*move_first=*/true);
                return DialogValue::from_string(current_room_id);
            }
            return DialogValue::from_string(current_room_id);
        };
        w.do_end = [this] {
            ending = true;
            narrating = true;
            textbox_centered_ = true;
        };
        w.resolve = [this](std::string_view kind, std::string_view key) {
            return resolve_name(kind, key);
        };
        w.drawing = [this](char kind, std::string_view id) { return drawing_frame(kind, id); };
        return w;
    }

    void enter_room(std::string room_id, int x, int y) {
        current_room_id = std::move(room_id);
        avatar_x = x;
        avatar_y = y;
        load_room_palette();
        apply_room_avatar();
        exit_locks.clear();
        ending_locks.clear();
        if (const Room* room = current_room()) {
            for (std::size_t i = 0; i < room->exits.size(); ++i)
                exit_locks.push_back(std::make_shared<DialogValue>(DialogValue::from_bool(false)));
            for (std::size_t i = 0; i < room->endings.size(); ++i)
                ending_locks.push_back(std::make_shared<DialogValue>(DialogValue::from_bool(false)));
        }
        apply_room_tune();
    }

    void init_runtime() {
        current_room_id = game.start_room_id();
        int x = 0;
        int y = 0;
        if (const Sprite* av = game.avatar(); av && av->position) {
            current_room_id = av->position->room_id;
            x = av->position->x;
            y = av->position->y;
        }

        room_items.clear();
        for (const auto& [id, room] : game.rooms) {
            room_items[id] = room.items;
        }
        items_generation_ = 1;
        composed_valid_ = false;
        textbox_was_active_ = false;
        textbox_pages_dirty_ = true;

        inventory.clear();
        if (const Sprite* av = game.avatar()) {
            for (const auto& [id, n] : av->inventory) inventory[id] = n;
        }

        variables.clear();
        for (const auto& [name, var] : game.variables) {
            variables[name] = DialogValue::from_var_text(var.value);
        }

        dlg.reset();
        dialog_context.reset();
        dialog_plain.clear();
        textbox_pixels.clear();
        textbox_pages_.clear();
        textbox_page_ = 0;
        ending = false;
        narrating = false;
        game_over = false;
        type_page_start_ms_ = 0;
        type_skip_ = false;
        anim_counter_ms = 0;
        anim_frame = 0;
        gfx_mode = GraphicsMode::Map;
        transition = Transition{};
        sound.stop_tune();
        sound.resume_tune();
        tune_id = "0";

        enter_room(current_room_id, x, y);
        compose();
    }

    void start_title() {
        narrating = true;
        start_dialog(game.title_dialog, {}, {}, nullptr);
    }

    void restart() {
        init_runtime();
        start_title();
    }

    void snapshot_room(TransitionFrame& frame, const std::string& room_id,
                       int ax, int ay, const std::string& ava) {
        ComposeState st;
        st.game = &game;
        auto rit = game.rooms.find(room_id);
        st.room = rit != game.rooms.end() ? &rit->second : nullptr;
        auto iit = room_items.find(room_id);
        st.items = (iit != room_items.end()) ? &iit->second : nullptr;
        st.room_id = room_id;
        st.avatar_x = ax;
        st.avatar_y = ay;
        st.anim_frame = anim_frame;
        st.avatar_id = ava;
        compose_room(st, ComposeBuffers{map1, map2, video});
        frame.pixels = video;
        frame.player_x = ax;
        frame.player_y = ay;
        std::string pal_id = (st.room && !st.room->palette_id.empty())
            ? st.room->palette_id : "0";
        auto pit = game.palettes.find(pal_id);
        if (pit != game.palettes.end()) frame.palette = pit->second.colors;
        else frame.palette = palette;
        while (frame.palette.size() < 3) frame.palette.push_back({});
    }

    void compose() {
        const bool need_room =
            !composed_valid_ ||
            (narrating || ending) != composed_narrating_ ||
            current_room_id != composed_room_ ||
            avatar_x != composed_ax_ ||
            avatar_y != composed_ay_ ||
            anim_frame != composed_anim_ ||
            avatar_id != composed_avatar_ ||
            items_generation_ != composed_items_gen_;

        if (need_room) {
            if (narrating || ending) {
                map1.fill(0);
                map2.fill(0);
                video.fill(0);
            } else if (!transition.active()) {
                ComposeState st;
                st.game = &game;
                st.room = current_room();
                auto it = room_items.find(current_room_id);
                st.items = (it != room_items.end()) ? &it->second : nullptr;
                st.room_id = current_room_id;
                st.avatar_x = avatar_x;
                st.avatar_y = avatar_y;
                st.anim_frame = anim_frame;
                st.avatar_id = avatar_id;
                compose_room(st, ComposeBuffers{map1, map2, video});
            }
            composed_valid_ = true;
            composed_narrating_ = narrating || ending;
            composed_room_ = current_room_id;
            composed_ax_ = avatar_x;
            composed_ay_ = avatar_y;
            composed_anim_ = anim_frame;
            composed_avatar_ = avatar_id;
            composed_items_gen_ = items_generation_;
        }
        refresh_textbox();
    }

    void refresh_textbox() {
        if (!dlg.active()) {
            if (textbox_was_active_) {
                textbox_pixels.clear();
                dialog_plain.clear();
                textbox_pages_.clear();
                textbox_page_ = 0;
                textbox_was_active_ = false;
                textbox_pages_dirty_ = true;
                last_revealed_ = -1;
                last_show_arrow_ = false;
                last_textbox_page_ = static_cast<std::size_t>(-1);
            }
            return;
        }
        textbox_was_active_ = true;

        TextboxLayout layout;
        layout.width = kTextboxWidth;
        layout.height = kTextboxHeight;
        layout.rtl = game.text_direction == TextDirection::RightToLeft;
        layout.time_ms = dialog_time_ms;

        if (textbox_pages_dirty_) {
            // Bitsy joins Arabic letters as the text is added to the box.
            std::vector<TextSpan> spans = dlg.spans();
            for (auto& sp : spans) {
                if (!sp.is_drawing) sp.text = shape_arabic(sp.text);
            }
            textbox_pages_ = paginate_spans(font, spans, layout);
            if (textbox_pages_.empty()) textbox_pages_.emplace_back();
            textbox_pages_dirty_ = false;
            last_textbox_page_ = static_cast<std::size_t>(-1);
            last_revealed_ = -1;
        }
        if (textbox_page_ >= textbox_pages_.size()) {
            textbox_page_ = textbox_pages_.size() - 1;
        }
        const auto& page = textbox_pages_[textbox_page_];
        if (textbox_page_ != last_textbox_page_) {
            dialog_plain = spans_to_plain(page);
            last_textbox_page_ = textbox_page_;
            last_revealed_ = -1;
        }

        const int total = count_printable_chars(page);
        const int revealed = revealed_chars(total);
        layout.visible_char_count = revealed;
        layout.show_arrow = is_page_complete(page, revealed);
        const bool timed = spans_have_time_effects(page);
        if (revealed != last_revealed_ ||
            layout.show_arrow != last_show_arrow_ ||
            timed) {
            textbox_pixels = render_textbox(font, page, layout);
            last_revealed_ = revealed;
            last_show_arrow_ = layout.show_arrow;
        }
    }

    void reset_typewriter() {
        type_page_start_ms_ = dialog_time_ms;
        type_skip_ = false;
    }

    [[nodiscard]] int revealed_chars(int total) const {
        if (total <= 0) return 0;
        if (type_skip_) return total;
        const double elapsed = dialog_time_ms - type_page_start_ms_;
        const int n = 1 + static_cast<int>(elapsed / kTextboxTypewriterMsPerChar);
        return n < total ? n : total;
    }

    [[nodiscard]] std::string_view dialog_line() const {
        return dialog_plain;
    }

    [[nodiscard]] bool is_wall_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize) return true;
        const Room* room = current_room();
        if (!room) return true;
        const std::string& tid =
            room->tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        if (tid.empty() || tid == "0") return false;
        if (std::find(room->wall_ids.begin(), room->wall_ids.end(), tid) !=
            room->wall_ids.end()) {
            return true;
        }
        auto it = game.tiles.find(tid);
        if (it == game.tiles.end()) return false;
        return it->second.is_wall;
    }

    [[nodiscard]] const Sprite* sprite_at(int x, int y) const {
        for (const auto& [id, spr] : game.sprites) {
            if (id == Game::kAvatarId) continue;
            if (!spr.position) continue;
            if (spr.position->room_id != current_room_id) continue;
            if (spr.position->x == x && spr.position->y == y) return &spr;
        }
        return nullptr;
    }

    [[nodiscard]] int item_index_at(int x, int y) {
        auto& items = items_in(current_room_id);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[static_cast<std::size_t>(i)].x == x &&
                items[static_cast<std::size_t>(i)].y == y) {
                return i;
            }
        }
        return -1;
    }

    std::string dialog_source(const std::string& dialog_id) const {
        if (dialog_id.empty()) return {};
        auto it = game.dialogues.find(dialog_id);
        if (it != game.dialogues.end()) return it->second.content;
        return {};
    }

    [[nodiscard]] bool has_dialog(const std::string& dialog_id) const {
        return !dialog_id.empty() && game.dialogues.count(dialog_id) > 0;
    }

    std::string sprite_dialog_id(const Sprite& spr) const {
        if (!spr.dialog_id.empty()) return spr.dialog_id;
        if (game.dlg_compat) return spr.id;
        return {};
    }

    void clear_textbox() {
        dialog_plain.clear();
        textbox_pixels.clear();
        textbox_pages_.clear();
        textbox_page_ = 0;
    }

    /// Bitsy's onExitDialog: leave narration, run the callback, resume music.
    void on_exit_dialog(const std::function<void()>& callback) {
        narrating = false;
        dialog_context.reset();
        clear_textbox();
        if (callback) callback();
        sound.resume_tune();
    }

    /// Bitsy's startDialog. A non-empty @p script_id keeps the compiled script
    /// (and its sequence positions) for the next time. @p context is the lock
    /// of the exit or ending the dialog belongs to, for `{property "locked"}`.
    void start_dialog(std::string source, std::string script_id,
                      std::function<void()> callback,
                      std::shared_ptr<DialogValue> context) {
        sound.pause_tune();
        dialog_time_ms = 0;
        textbox_page_ = 0;
        textbox_pages_dirty_ = true;
        textbox_centered_ = narrating;
        last_revealed_ = -1;
        reset_typewriter();
        dialog_context = std::move(context);
        dlg.start(source, make_world(), std::move(script_id),
                  [this, cb = std::move(callback)] { on_exit_dialog(cb); });
        if (dlg.active()) {
            log_dialog_page();
            refresh_textbox();
        }
    }

    void advance_dialog() {
        if (!dlg.active()) return;
        if (!textbox_pages_.empty()) {
            const auto& page = textbox_pages_[textbox_page_];
            if (!is_page_complete(page, revealed_chars(count_printable_chars(page)))) {
                type_skip_ = true;
                refresh_textbox();
                return;
            }
        }
        reset_typewriter();
        if (textbox_page_ + 1 < textbox_pages_.size()) {
            ++textbox_page_;
            refresh_textbox();
            return;
        }
        textbox_page_ = 0;
        if (!dlg.continue_page()) {
            // Ignore held keys until they are released, so the avatar does not
            // walk off as the dialog closes.
            ignore_input = true;
            cur_dir = Dir::None;
        } else {
            log_dialog_page();
            textbox_pages_dirty_ = true;
            last_revealed_ = -1;
            refresh_textbox();
        }
    }

    void log_dialog_page() {
        dialog_log_ += dlg.plain_text();
        dialog_log_ += '\n';
    }

    void warp_now(const Exit& ext) {
        enter_room(ext.dest_room_id, ext.dest_x, ext.dest_y);
    }

    /// Move to an exit's destination, with its transition if it has one. The
    /// move happens when the transition ends, unless @p move_first.
    void go_to(const Exit& ext, bool move_first = false) {
        const std::string fx = ext.transition_effect;
        if (fx.empty() || fx == "none" || !is_known_transition(fx)) {
            warp_now(ext);
            return;
        }

        TransitionFrame start_f, end_f;
        snapshot_room(start_f, current_room_id, avatar_x, avatar_y, avatar_id);
        if (move_first) warp_now(ext);

        const std::string dest = ext.dest_room_id;
        std::string dest_ava = std::string(Game::kAvatarId);
        auto rit = game.rooms.find(dest);
        if (rit != game.rooms.end() && !rit->second.avatar_id.empty())
            dest_ava = rit->second.avatar_id;
        snapshot_room(end_f, dest, ext.dest_x, ext.dest_y, dest_ava);

        gfx_mode = GraphicsMode::Video;
        transition.begin(start_f, end_f, fx, [this, ext, move_first] {
            gfx_mode = GraphicsMode::Map;
            if (!move_first) warp_now(ext);
            composed_valid_ = false;
        });
        // First paint.
        std::vector<Color> pal = palette;
        transition.update(0, video, pal);
        palette = std::move(pal);
    }

    static bool is_locked(const std::shared_ptr<DialogValue>& lock) {
        // Only `true` itself locks, as in Bitsy (`locked === true`).
        return lock && lock->kind == DialogValue::Kind::Bool && lock->boolean;
    }

    void take_exit(std::size_t index) {
        const Exit ext = current_room()->exits[index];
        auto lock = exit_locks[index];
        if (!has_dialog(ext.dialog_id)) {
            go_to(ext);
            return;
        }
        start_dialog(dialog_source(ext.dialog_id), ext.dialog_id,
                     [this, ext, lock] {
                         if (!is_locked(lock)) go_to(ext);
                     },
                     lock);
    }

    void start_ending(std::size_t index) {
        const std::string ending_id = current_room()->endings[index].ending_id;
        auto lock = ending_locks[index];
        narrating = true;
        ending = true;

        std::string script_id = ending_id;
        std::string src = dialog_source(ending_id);
        auto eit = game.endings.find(ending_id);
        if (eit != game.endings.end() && (game.dlg_compat || !has_dialog(ending_id))) {
            // Pre-7.0 endings kept their text in END blocks.
            script_id = "end_compat_" + ending_id;
            src = eit->second.text;
        }

        const std::string prev_tune = sound.tune_playing() ? sound.tune_id() : std::string{};
        sound.stop_tune();

        start_dialog(std::move(src), std::move(script_id),
                     [this, lock, prev_tune] {
                         if (!is_locked(lock)) return;
                         ending = false;
                         // A cancelled ending starts its music again.
                         if (!prev_tune.empty() && !sound.tune_playing()) {
                             if (const Tune* t = game.find_tune(prev_tune)) sound.play_tune(*t);
                         }
                     },
                     lock);
    }

    /// Bitsy runs the item's dialog first and only then takes the item.
    void pick_up_item(int index) {
        const auto& items = items_in(current_room_id);
        const RoomItem ri = items[static_cast<std::size_t>(index)];
        const std::string room_id = current_room_id;

        std::string dlg_id = ri.dialog_id;
        if (auto it = game.items.find(ri.item_id); it != game.items.end() && dlg_id.empty()) {
            dlg_id = it->second.dialog_id;
        }

        auto take = [this, room_id, index, item_id = ri.item_id] {
            auto& list = items_in(room_id);
            if (index < static_cast<int>(list.size())) {
                list.erase(list.begin() + index);
                ++items_generation_;
            }
            inventory[item_id] += 1;
        };

        if (has_dialog(dlg_id)) {
            start_dialog(dialog_source(dlg_id), dlg_id, std::move(take), nullptr);
        } else {
            take();
        }
    }

    void talk_to(const Sprite& spr) {
        const auto id = sprite_dialog_id(spr);
        if (has_dialog(id)) start_dialog(dialog_source(id), id, {}, nullptr);
    }

    /// Bitsy's movePlayer: move (unless a sprite or wall is in the way), then
    /// item, then ending or exit or sprite. Only one blip plays per step.
    void try_move(Dir direction) {
        if (direction == Dir::None) return;
        if (current_room_id.empty() || !current_room()) return;

        const int nx = avatar_x + dir_dx(direction);
        const int ny = avatar_y + dir_dy(direction);

        const Sprite* bumped = sprite_at(nx, ny);
        if (!bumped && !is_wall_at(nx, ny)) {
            avatar_x = nx;
            avatar_y = ny;
        }

        const Room* room = current_room();
        std::optional<std::size_t> ext;
        for (std::size_t i = 0; i < room->exits.size(); ++i) {
            if (room->exits[i].x == avatar_x && room->exits[i].y == avatar_y) {
                ext = i;
                break;
            }
        }
        std::optional<std::size_t> end;
        for (std::size_t i = 0; i < room->endings.size(); ++i) {
            if (room->endings[i].x == avatar_x && room->endings[i].y == avatar_y) {
                end = i;
                break;
            }
        }

        std::string blip_id;
        if (const int itm = item_index_at(avatar_x, avatar_y); itm >= 0) {
            const std::string item_id =
                items_in(current_room_id)[static_cast<std::size_t>(itm)].item_id;
            if (auto it = game.items.find(item_id); it != game.items.end()) {
                blip_id = it->second.blip_id;
            }
            pick_up_item(itm);
        }

        if (end) {
            start_ending(*end);
        } else if (ext) {
            take_exit(*ext);
        } else if (bumped) {
            if (!bumped->blip_id.empty()) blip_id = bumped->blip_id;
            talk_to(*bumped);
        }

        play_blip_id(blip_id);
    }

    void handle_input(Host& host) {
        const bool down = any_action_down(host);

        if (dlg.active()) {
            // Dialog waits while a blip plays.
            if (!sound.blip_playing() && !any_held && down) advance_dialog();
        } else if (ending) {
            if (!any_held && down) game_over = true;
        } else if (!ignore_input) {
            const Dir prev = cur_dir;
            cur_dir = read_direction(host);
            if (cur_dir != Dir::None && cur_dir != prev) {
                try_move(cur_dir);
                hold_timer_ms = kFirstHoldMs;
            }
        }

        if (!down) ignore_input = false;

        // Releasing Menu restarts the game, as in Bitsy.
        const bool menu = host.button(Button::Menu);
        if (menu_held && !menu) game_over = true;

        any_held = down;
        menu_held = menu;
    }

    void hold_repeat(double dt_ms) {
        if (dlg.active() || ending || transition.active()) return;
        if (cur_dir == Dir::None) return;
        hold_timer_ms -= dt_ms;
        if (hold_timer_ms <= 0) {
            try_move(cur_dir);
            hold_timer_ms = kRepeatHoldMs;
        }
    }

    void tick_animation(double dt_ms) {
        if (narrating || ending || transition.active()) return;
        anim_counter_ms += dt_ms;
        if (anim_counter_ms >= kAnimMs) {
            anim_counter_ms = 0;
            ++anim_frame;
        }
    }

    void do_present(Host& host) const {
        TextboxView textbox{};
        if (dlg.active()) {
            textbox.visible = true;
            textbox.width = kTextboxWidth;
            textbox.height = kTextboxHeight;
            textbox.x = 12;
            if (textbox_centered_) {
                textbox.y = (kVideoSize / 2) - (kTextboxHeight / 2);
            } else if (avatar_y < kMapSize / 2) {
                textbox.y = kVideoSize - 12 - kTextboxHeight;
            } else {
                textbox.y = 12;
            }
            textbox.pixels = std::span<const std::uint8_t>(textbox_pixels);
        }

        std::array<Color, 256> present_palette{};
        std::size_t plen = std::min(palette.size(), present_palette.size());
        for (std::size_t i = 0; i < plen; ++i) {
            present_palette[i] = palette[i];
        }
        if (dlg.active()) {
            for (std::size_t i = plen; i < present_palette.size(); ++i) {
                present_palette[i] = {};
            }
            install_textbox_colors(std::span<Color>(present_palette));
            plen = present_palette.size();
        }

        host.present(
            gfx_mode,
            game.txt_mode == 1 ? TextMode::LoRez : TextMode::HiRez,
            std::span<const Color>(present_palette.data(), plen),
            std::span<const std::uint8_t>(video),
            std::span<const std::uint8_t>(map1),
            std::span<const std::uint8_t>(map2),
            textbox,
            sound.channel1(),
            sound.channel2()
        );
    }
};

// ===========================================================================
// Engine  — public methods
// ===========================================================================

Engine::Engine(std::string_view bitsy_text)
    : impl_(std::make_unique<Impl>(citsy::parse(bitsy_text)))
{}

Engine Engine::from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::ios_base::failure("cannot open file: " + path);
    std::ostringstream buf;
    buf << file.rdbuf();
    return Engine(buf.str());
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

void Engine::start(Host& host) {
    impl_->running = true;
    impl_->init_runtime();
    impl_->start_title();
    host.on_engine_ready();
}

void Engine::update(Host& host) {
    if (!impl_->running) return;

    const double dt = host.delta_time_ms();
    // Notes already playing keep sounding in real time, whatever the game does.
    impl_->sound.advance(dt);

    // The same order as Bitsy's update(): input, then a transition takes over
    // the frame, else sound, animation, dialog and holding a direction.
    if (!impl_->transition.active()) impl_->handle_input(host);
    if (impl_->transition.active()) {
        impl_->gfx_mode = GraphicsMode::Video;
        impl_->transition.update(dt, impl_->video, impl_->palette);
    } else {
        impl_->sound.update(dt, impl_->game);
        impl_->tick_animation(dt);
        // The typewriter waits while a blip plays, as in Bitsy.
        if (impl_->dlg.active() && !impl_->sound.blip_playing()) impl_->dialog_time_ms += dt;
        impl_->hold_repeat(dt);
        impl_->compose();
    }

    impl_->do_present(host);

    // After an ending (or a Menu release) Bitsy reloads the game and shows the title.
    if (impl_->game_over) impl_->restart();
}

bool Engine::is_running() const noexcept {
    return impl_ && impl_->running;
}

std::string Engine::current_room_id() const {
    return impl_ ? impl_->current_room_id : std::string{};
}

int Engine::avatar_x() const noexcept {
    return impl_ ? impl_->avatar_x : 0;
}

int Engine::avatar_y() const noexcept {
    return impl_ ? impl_->avatar_y : 0;
}

bool Engine::dialog_active() const noexcept {
    return impl_ && impl_->dlg.active();
}

std::string_view Engine::dialog_line() const noexcept {
    return impl_ ? impl_->dialog_line() : std::string_view{};
}

int Engine::inventory_count(std::string_view item_id) const {
    if (!impl_) return 0;
    std::string id{item_id};
    if (const Item* it = impl_->game.find_item(item_id)) id = it->id;
    auto found = impl_->inventory.find(id);
    return found == impl_->inventory.end() ? 0 : static_cast<int>(found->second);
}

std::vector<std::pair<std::string, double>> Engine::inventory() const {
    std::vector<std::pair<std::string, double>> out;
    if (!impl_) return out;
    for (const auto& [id, n] : impl_->inventory) {
        if (n > 0) out.emplace_back(id, n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> Engine::variable_names() const {
    std::vector<std::string> out;
    if (!impl_) return out;
    for (const auto& [name, v] : impl_->variables) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

std::string Engine::variable_json(std::string_view name) const {
    if (!impl_) return "null";
    auto it = impl_->variables.find(std::string(name));
    if (it == impl_->variables.end()) return "null";
    return it->second.to_json();
}

std::string Engine::take_dialog_text() {
    if (!impl_) return {};
    return std::exchange(impl_->dialog_log_, {});
}

std::string Engine::variable(std::string_view name) const {
    if (!impl_) return {};
    auto it = impl_->variables.find(std::string(name));
    if (it == impl_->variables.end()) return {};
    return it->second.as_string();
}

bool Engine::ending_active() const noexcept {
    return impl_ && impl_->ending;
}

bool Engine::narrating() const noexcept {
    return impl_ && impl_->narrating;
}

int Engine::anim_frame() const noexcept {
    return impl_ ? impl_->anim_frame : 0;
}

std::string Engine::avatar_appearance() const {
    return impl_ ? impl_->avatar_id : std::string(Game::kAvatarId);
}

} // namespace citsy
