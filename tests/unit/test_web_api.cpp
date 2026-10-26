// The browser player's C API (backends/web/web_player.cpp), built natively
// against a stub emscripten.h. The creator calls these from its worker.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
int citsy_load(const char* text);
void citsy_unload();
void citsy_set_button(int index, int down);
int citsy_frame(double dt_ms);
const char* citsy_last_error();
const char* citsy_state_json();
double citsy_state_version();
void citsy_set_variable(const char* name, const char* text);
int citsy_set_item(const char* id, double count);
const char* citsy_validate(const char* text);
}

namespace fs = std::filesystem;

static std::string load_fixture(const char* name) {
    fs::path here = fs::path(__FILE__).parent_path();
    fs::path data = here.parent_path() / "data" / name;
    std::ifstream f(data);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

TEST_CASE("web: citsy_state_json lists inventory and variables", "[web][state]") {
    REQUIRE(citsy_load(load_fixture("state.bitsy").c_str()) == 1);
    const double v = citsy_state_version();
    CHECK(std::string(citsy_state_json()) ==
          "{\"version\":" + std::to_string(static_cast<std::uint64_t>(v)) +
          ",\"inventory\":{},\"variables\":{\"mood\":\"happy\",\"visits\":0}}");
    citsy_unload();
}

TEST_CASE("web: setters change the state the JSON shows", "[web][state]") {
    REQUIRE(citsy_load(load_fixture("state.bitsy").c_str()) == 1);
    const double before = citsy_state_version();
    CHECK(citsy_set_item("coin", 2) == 1);
    CHECK(citsy_set_item("nope", 2) == 0);
    citsy_set_variable("mood", "say \"hi\"");
    citsy_set_variable("visits", "7");
    CHECK(citsy_state_version() > before);
    const std::string json = citsy_state_json();
    CHECK(json.find("\"inventory\":{\"0\":2}") != std::string::npos);
    CHECK(json.find("\"variables\":{\"mood\":\"say \\\"hi\\\"\",\"visits\":7}") != std::string::npos);
    citsy_unload();
}

TEST_CASE("web: citsy_state_json returns the same string while nothing changes", "[web][state]") {
    REQUIRE(citsy_load(load_fixture("state.bitsy").c_str()) == 1);
    const char* a = citsy_state_json();
    citsy_frame(16.0);
    const char* b = citsy_state_json();
    CHECK(a == b);  // not rebuilt
    citsy_set_variable("visits", "3");
    CHECK(std::string(citsy_state_json()).find("\"visits\":3") != std::string::npos);
    citsy_unload();
}

TEST_CASE("web: citsy_state_json is empty with no game loaded", "[web][state]") {
    citsy_unload();
    CHECK(std::string(citsy_state_json()) == "{\"version\":0,\"inventory\":{},\"variables\":{}}");
    CHECK(citsy_state_version() == 0);
    citsy_set_variable("x", "1");  // no crash
    CHECK(citsy_set_item("0", 1) == 0);
}

TEST_CASE("web: citsy_validate reports errors with line numbers", "[web][validate]") {
    std::string src = load_fixture("state.bitsy");
    CHECK(std::string(citsy_validate(src.c_str())) == "{\"ok\":true}");

    src.replace(src.find("10111101"), 8, "1011110");
    CHECK(std::string(citsy_validate(src.c_str())) ==
          "{\"ok\":false,\"errors\":[{\"line\":39,\"message\":"
          "\"tile row must be exactly 8 characters, got '1011110'\"}]}");
}
