#include "src/dialog/value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace citsy {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool is_js_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string_view trim_js(std::string_view s) {
    while (!s.empty() && is_js_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_js_space(s.back())) s.remove_suffix(1);
    return s;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Length of the decimal literal (StrDecimalLiteral, no sign) at the start of
// @p s, or 0 when there is none. "Infinity" counts.
std::size_t decimal_prefix(std::string_view s) {
    if (s.starts_with("Infinity")) return 8;
    std::size_t i = 0;
    bool digits = false;
    while (i < s.size() && is_digit(s[i])) { ++i; digits = true; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && is_digit(s[i])) { ++i; digits = true; }
    }
    if (!digits) return 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        if (j < s.size() && is_digit(s[j])) {
            while (j < s.size() && is_digit(s[j])) ++j;
            i = j;
        }
    }
    return i;
}

double parse_decimal(std::string_view lit, bool negative) {
    double v = 0;
    if (lit == "Infinity") {
        v = kInf;
    } else {
        v = std::strtod(std::string(lit).c_str(), nullptr);
    }
    return negative ? -v : v;
}

} // namespace

std::string js_number_to_string(double n) {
    if (std::isnan(n)) return "NaN";
    if (n == 0) return "0";
    if (std::isinf(n)) return n < 0 ? "-Infinity" : "Infinity";

    std::string sign;
    if (n < 0) {
        sign = "-";
        n = -n;
    }

    // Shortest digits that round-trip, from "%.{p}e".
    char buf[64];
    int precision = 1;
    for (; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*e", precision - 1, n);
        if (std::strtod(buf, nullptr) == n) break;
    }
    std::string sci = buf;
    const auto e_pos = sci.find('e');
    std::string digits;
    for (std::size_t i = 0; i < e_pos; ++i) {
        if (sci[i] != '.') digits.push_back(sci[i]);
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const int exponent = std::atoi(sci.c_str() + e_pos + 1);

    // ECMAScript Number::toString: k digits, decimal point position n.
    const int k = static_cast<int>(digits.size());
    const int point = exponent + 1;
    std::string out;
    if (k <= point && point <= 21) {
        out = digits + std::string(static_cast<std::size_t>(point - k), '0');
    } else if (0 < point && point <= 21) {
        out = digits.substr(0, static_cast<std::size_t>(point)) + "." +
              digits.substr(static_cast<std::size_t>(point));
    } else if (-6 < point && point <= 0) {
        out = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
    } else {
        const int e = point - 1;
        out = digits.substr(0, 1);
        if (k > 1) out += "." + digits.substr(1);
        out += e < 0 ? "e-" : "e+";
        out += std::to_string(e < 0 ? -e : e);
    }
    return sign + out;
}

double js_parse_float(std::string_view s) {
    while (!s.empty() && is_js_space(s.front())) s.remove_prefix(1);
    bool negative = false;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    const std::size_t len = decimal_prefix(s);
    if (len == 0) return kNaN;
    return parse_decimal(s.substr(0, len), negative);
}

double js_string_to_number(std::string_view s) {
    s = trim_js(s);
    if (s.empty()) return 0;
    if (s.size() > 2 && s[0] == '0') {
        int base = 0;
        if (s[1] == 'x' || s[1] == 'X') base = 16;
        else if (s[1] == 'o' || s[1] == 'O') base = 8;
        else if (s[1] == 'b' || s[1] == 'B') base = 2;
        if (base != 0) {
            double v = 0;
            for (std::size_t i = 2; i < s.size(); ++i) {
                const char c = s[i];
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                if (d < 0 || d >= base) return kNaN;
                v = v * base + d;
            }
            return v;
        }
    }
    bool negative = false;
    if (s.front() == '+' || s.front() == '-') {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    const std::size_t len = decimal_prefix(s);
    if (len == 0 || len != s.size()) return kNaN;
    return parse_decimal(s, negative);
}

double js_parse_int(std::string_view s) {
    while (!s.empty() && is_js_space(s.front())) s.remove_prefix(1);
    bool negative = false;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    std::size_t i = 0;
    double v = 0;
    while (i < s.size() && is_digit(s[i])) v = v * 10 + (s[i++] - '0');
    if (i == 0) return kNaN;
    return negative ? -v : v;
}

DialogValue DialogValue::from_var_text(std::string_view text) {
    if (text == "true") return from_bool(true);
    if (text == "false") return from_bool(false);
    const double n = js_parse_float(text);
    if (!std::isnan(n)) return from_number(n);
    return from_string(std::string(text));
}

bool DialogValue::is_truthy() const {
    switch (kind) {
        case Kind::Null:   return false;
        case Kind::Bool:   return boolean;
        case Kind::Number: return number != 0 && !std::isnan(number);
        case Kind::String: return !str.empty();
    }
    return false;
}

double DialogValue::as_number() const {
    switch (kind) {
        case Kind::Null:   return 0;
        case Kind::Bool:   return boolean ? 1 : 0;
        case Kind::Number: return number;
        case Kind::String: return js_string_to_number(str);
    }
    return 0;
}

std::string DialogValue::as_string() const {
    switch (kind) {
        case Kind::Null:   return "null";
        case Kind::Bool:   return boolean ? "true" : "false";
        case Kind::Number: return js_number_to_string(number);
        case Kind::String: return str;
    }
    return {};
}

std::string DialogValue::to_json() const {
    switch (kind) {
        case Kind::Null:   return "null";
        case Kind::Bool:   return boolean ? "true" : "false";
        case Kind::Number: return std::isfinite(number) ? js_number_to_string(number) : "null";
        case Kind::String: break;
    }
    std::string out = "\"";
    for (const char c : str) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (u < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", u);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

bool DialogValue::strict_equals(const DialogValue& o) const {
    if (kind != o.kind) return false;
    switch (kind) {
        case Kind::Null:   return true;
        case Kind::Bool:   return boolean == o.boolean;
        case Kind::Number: return number == o.number;
        case Kind::String: return str == o.str;
    }
    return false;
}

DialogValue js_add(const DialogValue& l, const DialogValue& r) {
    if (l.kind == DialogValue::Kind::String || r.kind == DialogValue::Kind::String) {
        return DialogValue::from_string(l.as_string() + r.as_string());
    }
    return DialogValue::from_number(l.as_number() + r.as_number());
}

DialogValue js_sub(const DialogValue& l, const DialogValue& r) {
    return DialogValue::from_number(l.as_number() - r.as_number());
}

DialogValue js_mul(const DialogValue& l, const DialogValue& r) {
    return DialogValue::from_number(l.as_number() * r.as_number());
}

DialogValue js_div(const DialogValue& l, const DialogValue& r) {
    return DialogValue::from_number(l.as_number() / r.as_number());
}

bool js_less(const DialogValue& l, const DialogValue& r) {
    if (l.kind == DialogValue::Kind::String && r.kind == DialogValue::Kind::String) {
        return l.str < r.str;
    }
    const double a = l.as_number();
    const double b = r.as_number();
    if (std::isnan(a) || std::isnan(b)) return false;
    return a < b;
}

bool js_less_eq(const DialogValue& l, const DialogValue& r) {
    if (l.kind == DialogValue::Kind::String && r.kind == DialogValue::Kind::String) {
        return !(r.str < l.str);
    }
    const double a = l.as_number();
    const double b = r.as_number();
    if (std::isnan(a) || std::isnan(b)) return false;
    return a <= b;
}

} // namespace citsy
