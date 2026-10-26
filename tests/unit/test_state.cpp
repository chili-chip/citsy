// Live state API (parity P6, issue #35): hosts such as the creator list and
// change variables and the inventory while a game runs.

#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string load_fixture(const char* name) {
    fs::path here = fs::path(__FILE__).parent_path();
    fs::path data = here.parent_path() / "data" / name;
    std::ifstream f(data);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static void tap(citsy::Engine& engine, citsy::MockHost& host, citsy::Button b) {
    host.set_button(b, true);
    engine.update(host);
    host.set_button(b, false);
    engine.update(host);
}

static void skip_title(citsy::Engine& engine, citsy::MockHost& host) {
    engine.update(host);
    for (int i = 0; i < 8 && engine.dialog_active(); ++i) {
        tap(engine, host, citsy::Button::Ok);
    }
}

static void finish_dialog(citsy::Engine& engine, citsy::MockHost& host) {
    for (int i = 0; i < 8 && engine.dialog_active(); ++i) {
        tap(engine, host, citsy::Button::Ok);
    }
}

using Vars = std::vector<std::pair<std::string, std::string>>;
using Inv = std::vector<std::pair<std::string, double>>;

// ===========================================================================
// Listings
// ===========================================================================

TEST_CASE("state: variables() lists every variable with its JSON value", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.variables() == Vars{{"mood", "\"happy\""}, {"visits", "0"}});
}

TEST_CASE("state: variables() includes variables a script creates", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);
    tap(engine, host, citsy::Button::Left);  // talk to the shopkeeper
    REQUIRE(engine.dialog_active());
    CHECK(engine.variables() == Vars{{"mood", "\"happy\""}, {"visits", "1"}});
}

// ===========================================================================
// Setters
// ===========================================================================

TEST_CASE("state: set_variable reads text like a VAR value", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);

    engine.set_variable("visits", "41");
    CHECK(engine.variable_json("visits") == "41");
    engine.set_variable("mood", "true");
    CHECK(engine.variable_json("mood") == "true");
    engine.set_variable("mood", "grumpy");
    CHECK(engine.variable_json("mood") == "\"grumpy\"");
    engine.set_variable("fresh", "2.5");
    CHECK(engine.variable_json("fresh") == "2.5");
}

TEST_CASE("state: scripts see a variable the host set", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);

    engine.set_variable("visits", "41");
    engine.set_variable("mood", "grumpy");
    tap(engine, host, citsy::Button::Left);
    CHECK(engine.dialog_line() == "Visits: 42, mood: grumpy");
}

TEST_CASE("state: set_inventory_count by id or name", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);

    CHECK(engine.set_inventory_count("0", 3));
    CHECK(engine.inventory_count("coin") == 3);
    CHECK(engine.set_inventory_count("coin", 5));
    CHECK(engine.inventory() == Inv{{"0", 5}});
    CHECK(engine.set_inventory_count("0", 0));
    CHECK(engine.inventory().empty());
}

TEST_CASE("state: set_inventory_count refuses an unknown item", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    CHECK_FALSE(engine.set_inventory_count("nope", 1));
    CHECK(engine.inventory().empty());
}

TEST_CASE("state: a pickup adds to the count the host set", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);
    engine.set_inventory_count("coin", 2);
    tap(engine, host, citsy::Button::Right);  // walk onto the coin
    finish_dialog(engine, host);
    CHECK(engine.inventory_count("coin") == 3);
}

// ===========================================================================
// Change counter
// ===========================================================================

TEST_CASE("state: state_version stays put while nothing changes", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);
    const auto v = engine.state_version();
    for (int i = 0; i < 10; ++i) engine.update(host);
    tap(engine, host, citsy::Button::Down);  // a plain step
    CHECK(engine.state_version() == v);
}

TEST_CASE("state: state_version goes up when the host changes state", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);

    auto v = engine.state_version();
    engine.set_variable("visits", "1");
    CHECK(engine.state_version() > v);

    v = engine.state_version();
    engine.set_variable("visits", "1");  // same value
    CHECK(engine.state_version() == v);

    v = engine.state_version();
    engine.set_inventory_count("coin", 1);
    CHECK(engine.state_version() > v);

    v = engine.state_version();
    engine.set_inventory_count("coin", 1);  // same count
    CHECK(engine.state_version() == v);

    v = engine.state_version();
    engine.set_inventory_count("nope", 1);
    CHECK(engine.state_version() == v);
}

TEST_CASE("state: state_version goes up when a script sets a variable", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);
    const auto v = engine.state_version();
    tap(engine, host, citsy::Button::Left);
    CHECK(engine.state_version() > v);
}

TEST_CASE("state: state_version goes up on a pickup", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    skip_title(engine, host);
    const auto v = engine.state_version();
    tap(engine, host, citsy::Button::Right);
    finish_dialog(engine, host);
    REQUIRE(engine.inventory_count("coin") == 1);
    CHECK(engine.state_version() > v);
}

TEST_CASE("state: state_version goes up when the game restarts", "[engine][state]") {
    citsy::Engine engine(load_fixture("state.bitsy"));
    citsy::MockHost host;
    engine.start(host);
    const auto v = engine.state_version();
    engine.start(host);
    CHECK(engine.state_version() > v);
}

// ===========================================================================
// Validation
// ===========================================================================

TEST_CASE("state: validate accepts a good game", "[engine][state][validate]") {
    CHECK(citsy::Engine::validate(load_fixture("state.bitsy")).empty());
}

TEST_CASE("state: validate reports the line of a parse error", "[engine][state][validate]") {
    std::string src = load_fixture("state.bitsy");
    const auto at = src.find("10111101");
    REQUIRE(at != std::string::npos);
    src.replace(at, 8, "1011110");  // a 7-pixel row in the avatar

    const auto errors = citsy::Engine::validate(src);
    REQUIRE(errors.size() == 1);
    // The avatar's fifth pixel row is line 39 of state.bitsy.
    CHECK(errors[0].line() == 39);
    CHECK(errors[0].message().find("8 characters") != std::string::npos);
    CHECK(std::string(errors[0].what()).rfind("line 39: ", 0) == 0);
}

// Replace line @p line (1-based) of state.bitsy and return the error's line.
static int error_line_after_editing(int line, const std::string& text) {
    std::istringstream in(load_fixture("state.bitsy"));
    std::string out, l;
    for (int n = 1; std::getline(in, l); ++n) out += (n == line ? text : l) + "\n";
    const auto errors = citsy::Engine::validate(out);
    REQUIRE(errors.size() == 1);
    return errors[0].line();
}

TEST_CASE("state: validate points at the line that is wrong", "[engine][state][validate]") {
    CHECK(error_line_after_editing(10, "300,0,0") == 10);      // palette colour
    CHECK(error_line_after_editing(16, "0,0,0") == 16);        // room row
    CHECK(error_line_after_editing(32, "ITM 0 zz") == 32);     // item in a room
    CHECK(error_line_after_editing(44, "POS 0 x") == 44);      // sprite position
    CHECK(error_line_after_editing(52, "1010") == 52);         // sprite pixel row
}
