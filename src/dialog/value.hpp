#pragma once

// Bitsy script values follow JavaScript, because the reference engine is
// JavaScript: null, booleans, numbers (doubles) and strings, with JS rules for
// `==` (strict), `+` (joins strings), comparisons, truthiness and printing.

#include <string>
#include <string_view>

namespace citsy {

/// JS `Number.prototype.toString()` (shortest round trip, JS exponent rules).
[[nodiscard]] std::string js_number_to_string(double n);

/// JS `parseFloat()`: the longest numeric prefix after leading whitespace, or NaN.
[[nodiscard]] double js_parse_float(std::string_view s);

/// JS `Number(string)`: the whole trimmed string as a number ("" is 0), or NaN.
[[nodiscard]] double js_string_to_number(std::string_view s);

/// JS `parseInt(String(x), 10)` for a number already in hand.
[[nodiscard]] double js_parse_int(std::string_view s);

struct DialogValue {
    enum class Kind { Null, Bool, Number, String };
    Kind        kind = Kind::Null;
    bool        boolean = false;
    double      number = 0;
    std::string str;

    static DialogValue null() { return {}; }
    static DialogValue from_bool(bool b) {
        DialogValue v;
        v.kind = Kind::Bool;
        v.boolean = b;
        return v;
    }
    static DialogValue from_number(double n) {
        DialogValue v;
        v.kind = Kind::Number;
        v.number = n;
        return v;
    }
    static DialogValue from_string(std::string s) {
        DialogValue v;
        v.kind = Kind::String;
        v.str = std::move(s);
        return v;
    }

    /// How Bitsy reads a `VAR` value: "true"/"false", then parseFloat, else text.
    static DialogValue from_var_text(std::string_view text);

    [[nodiscard]] bool is_null() const { return kind == Kind::Null; }
    [[nodiscard]] bool is_truthy() const;
    [[nodiscard]] double as_number() const;       ///< JS ToNumber
    [[nodiscard]] std::string as_string() const;  ///< JS ToString

    /// JS `JSON.stringify()` of the value (NaN and Infinity become `null`).
    [[nodiscard]] std::string to_json() const;

    /// JS `===`.
    [[nodiscard]] bool strict_equals(const DialogValue& o) const;
};

/// JS binary operators as Bitsy applies them.
[[nodiscard]] DialogValue js_add(const DialogValue& l, const DialogValue& r);
[[nodiscard]] DialogValue js_sub(const DialogValue& l, const DialogValue& r);
[[nodiscard]] DialogValue js_mul(const DialogValue& l, const DialogValue& r);
[[nodiscard]] DialogValue js_div(const DialogValue& l, const DialogValue& r);
/// `<`, `>`, `<=`, `>=` with JS abstract relational comparison.
[[nodiscard]] bool js_less(const DialogValue& l, const DialogValue& r);
[[nodiscard]] bool js_less_eq(const DialogValue& l, const DialogValue& r);

} // namespace citsy
