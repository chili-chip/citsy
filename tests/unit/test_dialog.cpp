// Dialog scripts follow Bitsy 8.15 (script.js). Expected values come from
// running the same script in the reference engine (tests/parity/reference).

#include <catch2/catch_test_macros.hpp>

#include "src/dialog/script.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using citsy::DialogValue;
using Pages = std::vector<std::string>;

struct TestWorld {
    std::map<std::string, DialogValue> vars;
    std::map<std::string, double> items;
    std::map<std::string, std::string> item_names;  ///< name -> id
    std::optional<DialogValue> locked;               ///< set when an exit/ending runs the script
    bool ended = false;
    std::vector<std::string> exits;
    std::vector<std::string> blips;
    std::string tune = "0";
    std::string avatar = "A";
    citsy::DialogVM vm;  ///< keeps compiled scripts by id between runs

    citsy::DialogWorld bind() {
        citsy::DialogWorld w;
        w.get_var = [this](std::string_view name) -> std::optional<DialogValue> {
            auto it = vars.find(std::string(name));
            if (it == vars.end()) return std::nullopt;
            return it->second;
        };
        w.set_var = [this](std::string_view name, const DialogValue& v) {
            vars[std::string(name)] = v;
        };
        w.item_count = [this](std::string_view id) {
            auto it = items.find(std::string(id));
            return it == items.end() ? 0.0 : it->second;
        };
        w.set_item = [this](std::string_view id, double n) { items[std::string(id)] = n; };
        w.has_property = [this](std::string_view name) {
            return locked.has_value() && name == "locked";
        };
        w.get_property = [this](std::string_view) { return *locked; };
        w.set_property = [this](std::string_view, const DialogValue& v) { locked = v; };
        w.avatar = [this](std::optional<std::string> id) {
            if (id) avatar = *id;
            return DialogValue::from_string(avatar);
        };
        w.tune = [this](std::optional<std::string> id) {
            if (id) tune = *id;
            return DialogValue::from_string(tune);
        };
        w.play_blip = [this](std::string_view id) { blips.emplace_back(id); };
        w.do_exit = [this](std::string room, std::optional<int> x, std::optional<int> y,
                           std::optional<std::string> fx) {
            exits.push_back(room + " " + (x ? std::to_string(*x) : "-") + " " +
                            (y ? std::to_string(*y) : "-") + (fx ? " " + *fx : ""));
            return DialogValue::from_string(room);
        };
        w.do_end = [this] { ended = true; };
        w.resolve = [this](std::string_view kind, std::string_view key) {
            if (kind == "item") {
                if (auto it = item_names.find(std::string(key)); it != item_names.end())
                    return it->second;
            }
            return std::string(key);
        };
        return w;
    }
};

/// Every page of @p src, pressing continue at each one.
Pages run(std::string_view src, TestWorld& w, std::string script_id = {}) {
    citsy::DialogVM fresh;
    citsy::DialogVM& vm = script_id.empty() ? fresh : w.vm;
    Pages pages;
    vm.start(src, w.bind(), script_id);
    while (vm.active()) {
        pages.push_back(vm.plain_text());
        if (!vm.continue_page()) break;
    }
    return pages;
}

Pages run(std::string_view src) {
    TestWorld w;
    return run(src, w);
}

} // namespace

// --- Text -------------------------------------------------------------------

TEST_CASE("dialog: a line prints as written, quotes included", "[dialog]") {
    CHECK(run(R"("Hello, world!")") == Pages{R"("Hello, world!")"});
    CHECK(run("You found the treasure!") == Pages{"You found the treasure!"});
}

TEST_CASE("dialog: a block's lines are line breaks and a blank line is a page break",
          "[dialog]") {
    CHECK(run("\"\"\"\nline one\nline two\n\nline three\n\"\"\"") ==
          Pages{"line one\nline two", "line three"});
}

TEST_CASE("dialog: {pg} starts a page and {br} a line", "[dialog]") {
    CHECK(run("one{pg}two{br}three{br}{br}four") == Pages{"one", "two\nthree", "four"});
}

TEST_CASE("dialog: an empty script shows nothing", "[dialog]") {
    CHECK(run("").empty());
}

TEST_CASE("dialog: text effects print nothing", "[dialog]") {
    CHECK(run("Hi {wvy}there{wvy}") == Pages{"Hi there"});
    CHECK(run("hi {/clr1} there") == Pages{"hi  there"});
}

// --- Values -----------------------------------------------------------------

TEST_CASE("dialog: {x} alone prints nothing; {say x} prints the value", "[dialog][script]") {
    TestWorld w;
    w.vars["x"] = DialogValue::from_number(7);
    CHECK(run("a{x}b{say x}c", w) == Pages{"ab7c"});
}

TEST_CASE("dialog: an unknown name prints nothing", "[dialog][script]") {
    CHECK(run("hi {foo} there {say y}") == Pages{"hi  there "});
}

TEST_CASE("dialog: say takes its first argument as written", "[dialog][script]") {
    // Function arguments are space separated, so `10 - 3` is three arguments.
    CHECK(run("{say 10 - 3 + 2}") == Pages{"10"});
    CHECK(run("{say 1/3}") == Pages{"1"});
}

TEST_CASE("dialog: numbers print the JavaScript way", "[dialog][script]") {
    CHECK(run("{say 2.5} {say 0.1 + 0.2} {say {0.1 + 0.2}}") ==
          Pages{"2.5 0.1 0.30000000000000004"});
    CHECK(run("{say 1e21}{say -0}") == Pages{"1e+210"});
    CHECK(run("{say true}") == Pages{"true"});
}

TEST_CASE("dialog: operators split at the first operator in Bitsy's order",
          "[dialog][script]") {
    TestWorld w;
    run(R"({a = 10 - 3 + 2}{b = "a" + "b"}{c = 1 == "1"}{d = 5 / 2}{e = 2 * 3 + 1})"
        R"({f = 3 > 2}{g = "x" + 1})",
        w);
    CHECK(w.vars["a"].to_json() == "5");      // 10 - (3 + 2)
    CHECK(w.vars["b"].to_json() == "\"ab\"");
    CHECK(w.vars["c"].to_json() == "false");  // == is strict
    CHECK(w.vars["d"].to_json() == "2.5");
    CHECK(w.vars["e"].to_json() == "7");
    CHECK(w.vars["f"].to_json() == "true");
    CHECK(w.vars["g"].to_json() == "\"x1\"");
}

TEST_CASE("dialog: assigning an unknown variable stores null", "[dialog][script]") {
    TestWorld w;
    w.vars["x"] = DialogValue::from_string("hello world");
    run("{y = x}{z = q}", w);
    CHECK(w.vars["y"].to_json() == "\"hello world\"");
    CHECK(w.vars["z"].to_json() == "null");
}

TEST_CASE("dialog: an inline conditional prints its result as dialog", "[dialog][script]") {
    TestWorld w;
    w.vars["x"] = DialogValue::from_number(7);
    CHECK(run(R"({say {x == 7 ? "yes" : "no"}}{x == 7 ? "yes" : "no"})", w) ==
          Pages{R"("yes""yes")"});
}

// --- Branches and lists ---------------------------------------------------------

TEST_CASE("dialog: a branch picks the first true arm", "[dialog][script]") {
    constexpr std::string_view src = "\"\"\"\n{\n  - score == 1 ?\n    one\n  - else ?\n    other\n}\n\"\"\"";
    TestWorld w;
    w.vars["score"] = DialogValue::from_number(1);
    CHECK(run(src, w) == Pages{"one"});
    w.vars["score"] = DialogValue::from_string("1");
    CHECK(run(src, w) == Pages{"other"});
}

TEST_CASE("dialog: sequence advances then sticks on the last option", "[dialog][script]") {
    TestWorld w;
    constexpr std::string_view src = "{sequence\n  - a\n  - b\n  - c\n}";
    CHECK(run(src, w, "seq") == Pages{"a"});
    CHECK(run(src, w, "seq") == Pages{"b"});
    CHECK(run(src, w, "seq") == Pages{"c"});
    CHECK(run(src, w, "seq") == Pages{"c"});
}

TEST_CASE("dialog: cycle wraps around", "[dialog][script]") {
    TestWorld w;
    constexpr std::string_view src = "{cycle\n  - a\n  - b\n}";
    CHECK(run(src, w, "cyc") == Pages{"a"});
    CHECK(run(src, w, "cyc") == Pages{"b"});
    CHECK(run(src, w, "cyc") == Pages{"a"});
}

TEST_CASE("dialog: list positions belong to the script id", "[dialog][script]") {
    TestWorld w;
    constexpr std::string_view src = "{sequence\n  - a\n  - b\n}";
    CHECK(run(src, w, "one") == Pages{"a"});
    CHECK(run(src, w, "two") == Pages{"a"});
    CHECK(run(src, w, "one") == Pages{"b"});
    CHECK(run(src, w) == Pages{"a"});  // no id: compiled fresh each time
    CHECK(run(src, w) == Pages{"a"});
}

TEST_CASE("dialog: shuffle shows every option before repeating", "[dialog][script]") {
    TestWorld w;
    constexpr std::string_view src = "{shuffle\n  - a\n  - b\n  - c\n}";
    Pages seen;
    for (int i = 0; i < 3; ++i) {
        auto pages = run(src, w, "shuf");
        REQUIRE(pages.size() == 1);
        seen.push_back(pages[0]);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(seen == Pages{"a", "b", "c"});
}

TEST_CASE("dialog: line breaks inside list options", "[dialog][script]") {
    CHECK(run("\"\"\"\n{sequence\n  - hello\n    world\n}\n\"\"\"") == Pages{"hello\nworld"});
    // An empty line right inside an option is dropped, as in Bitsy. (Bitsy
    // would still turn the page after "a" because its box holds two rows;
    // citsy pages by its own three-row screens.)
    CHECK(run("\"\"\"\n{sequence\n  - hello\n\n    world\n}\n\"\"\"") == Pages{"hello\nworld"});
    CHECK(run("\"\"\"\nfirst\n{sequence\n  - a\n\n    b\n}\nlast\n\"\"\"") ==
          Pages{"first\na\nb\nlast"});
    // A line of spaces reads as a blank line, so it turns the page.
    CHECK(run("\"\"\"\n{sequence\n  - I tend\n    \n    would you\n}\n\"\"\"") ==
          Pages{"I tend", "would you"});
}

// --- World functions ------------------------------------------------------------

TEST_CASE("dialog: item reads and sets counts by id or name", "[dialog][script]") {
    TestWorld w;
    w.item_names["key"] = "0";
    CHECK(run(R"({say {item "key"}}{item "0" 3}{say {item "0"}})", w) == Pages{"03"});
    CHECK(w.items["0"] == 3);
    run(R"({item "0" {{item "0"} - 1}})", w);
    CHECK(w.items["0"] == 2);
    run(R"({item "key" -4})", w);
    CHECK(w.items["0"] == 0);  // never below zero
}

TEST_CASE("dialog: property locked works only for an exit or ending", "[dialog][script]") {
    TestWorld w;
    CHECK(run("{property locked true}{say {property locked}}", w).empty());

    w.locked = DialogValue::from_bool(false);
    CHECK(run("{property locked true}{say {property locked}}", w) == Pages{"true"});
    CHECK(w.locked->to_json() == "true");

    // Bitsy only reads the name as a bare word; a quoted name does nothing.
    w.locked = DialogValue::from_bool(false);
    CHECK(run(R"({property "locked" true})", w).empty());
    CHECK(w.locked->to_json() == "false");
}

TEST_CASE("dialog: end, exit, tune, blip and ava reach the world", "[dialog][script]") {
    TestWorld w;
    CHECK(run("{end}bye", w) == Pages{"bye"});
    CHECK(w.ended);

    CHECK(run(R"({exit "1" 3 4}{exit "2" 5 6 "fade_w"}moved)", w) == Pages{"moved"});
    CHECK(w.exits == std::vector<std::string>{"1 3 4", "2 5 6 fade_w"});

    CHECK(run(R"({tune "2"}{say {tune}}{blip "1"}{ava "B"}{say {ava}})", w) == Pages{"2B"});
    CHECK(w.tune == "2");
    CHECK(w.blips == std::vector<std::string>{"1"});
    CHECK(w.avatar == "B");
}

TEST_CASE("dialog: colour and wavy effects mark their spans", "[dialog][script]") {
    TestWorld w;
    citsy::DialogVM vm;
    vm.start("plain {clr3}blue{clr3} {wvy}wave{wvy} {clr 2}red{/clr}", w.bind());
    REQUIRE(vm.active());
    int checked = 0;
    for (const auto& sp : vm.spans()) {
        if (sp.text.find("blue") != std::string::npos) {
            CHECK(sp.color == 2);  // clr3 is palette colour 2
            ++checked;
        }
        if (sp.text.find("wave") != std::string::npos) {
            CHECK(sp.effects == citsy::GlyphFx::Wavy);
            ++checked;
        }
        if (sp.text.find("red") != std::string::npos) {
            CHECK(sp.color == 2);
            ++checked;
        }
        if (sp.text.find("plain") != std::string::npos) {
            CHECK(sp.color == -1);
            ++checked;
        }
    }
    CHECK(checked == 4);
}

TEST_CASE("dialog: property keeps the value it is given", "[dialog][script]") {
    TestWorld w;
    w.locked = DialogValue::from_bool(false);
    CHECK(run("{property locked 1}{say {property locked}}", w) == Pages{"1"});
    CHECK(w.locked->to_json() == "1");
}
