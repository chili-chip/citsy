#include "src/dialog/script.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace citsy {
namespace script {

// ===========================================================================
// Nodes (script.js: DialogBlockNode, CodeBlockNode, FuncNode, LiteralNode,
// VarNode, ExpNode, SequenceNode, CycleNode, ShuffleNode, IfNode, ElseNode,
// UndefinedNode)
// ===========================================================================

enum class NodeType {
    DialogBlock, CodeBlock, Function, Literal, Variable, Operator,
    Sequence, Cycle, Shuffle, If, Else, Undefined,
};

struct Node {
    NodeType type = NodeType::DialogBlock;
    std::vector<std::unique_ptr<Node>> children;    ///< blocks; list options; if results
    std::vector<std::unique_ptr<Node>> conditions;  ///< if
    std::vector<std::unique_ptr<Node>> args;        ///< function
    std::unique_ptr<Node> left, right;              ///< operator
    std::string name;   ///< function / variable name, operator symbol, undefined source
    DialogValue value;  ///< literal

    // List state lives in the node, as in Bitsy.
    std::size_t index = 0;
    std::vector<std::size_t> shuffled;
};

namespace {

std::unique_ptr<Node> make(NodeType t) {
    auto n = std::make_unique<Node>();
    n->type = t;
    return n;
}

std::unique_ptr<Node> literal(DialogValue v) {
    auto n = make(NodeType::Literal);
    n->value = std::move(v);
    return n;
}

const char* const kFunctions[] = {
    "say", "br", "pg", "wvy", "/wvy", "shk", "/shk", "rbw", "/rbw", "clr", "/clr",
    "drwt", "drws", "drwi", "end", "exit", "pal", "ava", "item", "property",
    "tune", "blip", "clr1", "clr2", "clr3", "print", "printTile", "printSprite",
    "printItem", "_debugOnlyPrintFont",
};

bool has_function(std::string_view name) {
    return std::any_of(std::begin(kFunctions), std::end(kFunctions),
                       [&](const char* f) { return name == f; });
}

bool is_sequence(std::string_view s) {
    return s == "sequence" || s == "cycle" || s == "shuffle";
}

// JS String.prototype.trim (ASCII whitespace is all Bitsy data uses).
std::string js_trim(std::string_view s) {
    auto ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };
    while (!s.empty() && ws(s.front())) s.remove_prefix(1);
    while (!s.empty() && ws(s.back())) s.remove_suffix(1);
    return std::string(s);
}

// JS str.slice(start, end) on a byte string; negative indices count from the end.
std::string js_slice(const std::string& s, long start, long end) {
    const long len = static_cast<long>(s.size());
    if (start < 0) start = std::max(0L, len + start);
    if (end < 0) end = std::max(0L, len + end);
    start = std::min(start, len);
    end = std::min(end, len);
    if (start >= end) return {};
    return s.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

// str.slice(n) where n counts UTF-16 code units, on UTF-8 text.
std::string slice_units(const std::string& s, int units) {
    std::size_t i = 0;
    while (units > 0 && i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        int u = 1;
        if (c >= 0xF0) { len = 4; u = 2; }
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        i += len;
        units -= u;
    }
    return i >= s.size() ? std::string{} : s.substr(i);
}

// Cut the trailing "\n" that list parsing appends (JS slice(0, -1)).
void drop_last(std::string& s) {
    if (s.empty()) return;
    std::size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    s.erase(i);
}

bool is_valid_variable_name(std::string_view s) {
    if (s.empty()) return false;
    auto first = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    };
    if (!first(s[0])) return false;
    for (char c : s.substr(1)) {
        if (!first(c) && !(c >= '0' && c <= '9')) return false;
    }
    return true;
}

bool is_bitsy_whitespace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

// ===========================================================================
// Parser (script.js: Parser)
// ===========================================================================

class ParserState {
public:
    explicit ParserState(std::string src) : src_(std::move(src)) {}

    [[nodiscard]] bool done() const { return i_ >= src_.size(); }
    [[nodiscard]] char ch() const { return done() ? '\0' : src_[i_]; }
    void step(std::size_t n = 1) { i_ += n; }
    [[nodiscard]] const std::string& source() const { return src_; }

    [[nodiscard]] bool match_ahead(std::string_view s) const {
        for (std::size_t j = 0; j < s.size(); ++j) {
            if (i_ + j >= src_.size() || s[j] != src_[i_ + j]) return false;
        }
        return true;
    }

    [[nodiscard]] std::string peak(std::string_view end_chars) const {
        std::size_t j = i_;
        while (j < src_.size() && end_chars.find(src_[j]) == std::string_view::npos) ++j;
        return src_.substr(i_, j - i_);
    }

    std::string consume_block(std::string_view open, std::string_view close,
                              bool include_symbols = false) {
        const std::size_t start = i_;
        int count = 0;
        if (match_ahead(open)) {
            ++count;
            step(open.size());
        }
        while (count > 0 && !done()) {
            if (match_ahead(close)) {
                --count;
                step(close.size());
            } else if (match_ahead(open)) {
                ++count;
                step(open.size());
            } else {
                step();
            }
        }
        const long end = static_cast<long>(std::min(i_, src_.size()));
        if (include_symbols) return js_slice(src_, static_cast<long>(start), end);
        return js_slice(src_, static_cast<long>(start + open.size()),
                        end - static_cast<long>(close.size()));
    }

private:
    std::string src_;
    std::size_t i_ = 0;
};

class Parser {
public:
    explicit Parser(std::function<double()> random) : random_(std::move(random)) {}

    std::unique_ptr<Node> parse(std::string_view script) {
        ParserState state{std::string(script)};
        if (state.match_ahead("\"\"\"")) {
            std::string inner = state.consume_block("\"\"\"\n", "\n\"\"\"");
            return parse_dialog(std::move(inner));
        }
        return parse_dialog(std::string(script));
    }

private:
    std::function<double()> random_;

    // ParseDialog: text becomes {say} nodes; a {br} goes between two dialog
    // lines (lines with text, empty lines, and lines with a list block).
    std::unique_ptr<Node> parse_dialog(std::string src) {
        auto root = make(NodeType::DialogBlock);
        ParserState state{std::move(src)};

        std::vector<std::unique_ptr<Node>> line_nodes;
        std::string text;
        bool line_empty = true;
        bool line_has_text = false;
        bool prev_line_is_dialog = false;

        auto line_is_dialog = [&] { return line_has_text || line_empty; };
        auto flush_text = [&] {
            if (!text.empty()) {
                auto say = make(NodeType::Function);
                say->name = "say";
                say->args.push_back(literal(DialogValue::from_string(text)));
                line_nodes.push_back(std::move(say));
                text.clear();
                line_empty = false;
                line_has_text = true;
            }
        };
        auto add_code = [&] {
            auto code = parse_code(state.consume_block("{", "}"));
            line_empty = false;
            if (is_list_block(*code)) line_has_text = true;
            line_nodes.push_back(std::move(code));
        };
        auto add_linebreak = [&] {
            if (prev_line_is_dialog) {
                auto br = make(NodeType::Function);
                br->name = "br";
                line_nodes.insert(line_nodes.begin(), std::move(br));
            }
        };
        auto flush_line = [&] {
            for (auto& n : line_nodes) root->children.push_back(std::move(n));
            line_nodes.clear();
        };
        auto reset_line = [&] {
            prev_line_is_dialog = line_is_dialog();
            line_has_text = false;
            line_empty = true;
            text.clear();
        };

        while (!state.done()) {
            if (state.match_ahead("{")) {
                flush_text();
                add_code();
            } else if (state.match_ahead("\n")) {
                flush_text();
                add_linebreak();
                flush_line();
                reset_line();
                state.step();
            } else {
                text.push_back(state.ch());
                state.step();
            }
        }
        flush_text();
        add_linebreak();
        flush_line();
        return root;
    }

    static bool is_list_block(const Node& code) {
        if (code.type != NodeType::CodeBlock || code.children.empty()) return false;
        const auto t = code.children[0]->type;
        return t == NodeType::Sequence || t == NodeType::Cycle ||
               t == NodeType::Shuffle || t == NodeType::If;
    }

    std::unique_ptr<Node> parse_code(std::string src) {
        auto root = make(NodeType::CodeBlock);
        ParserState state{std::move(src)};
        if (is_conditional_block(state)) {
            parse_conditional(state, *root);
        } else if (const auto fn = state.peak(" "); has_function(fn)) {
            state.step(fn.size());
            parse_function(state, *root, fn);
        } else if (const auto seq = state.peak(" \n"); is_sequence(seq)) {
            state.step(seq.size());
            parse_sequence(state, *root, seq);
        } else if (is_literal(state.source()) || is_expression(state.source())) {
            root->children.push_back(create_expression(state.source()));
        } else {
            auto undefined = make(NodeType::Undefined);
            undefined->name = state.source();
            root->children.push_back(std::move(undefined));
        }
        return root;
    }

    static bool is_conditional_block(const ParserState& state) {
        const std::string to_list = state.peak("-");
        // Bitsy compares this string with a number, so it only holds when the
        // text before the first "-" is whitespace (JS turns that into 0).
        const double as_number = js_string_to_number(to_list);
        const bool found_list = !std::isnan(as_number) &&
                                as_number < static_cast<double>(state.source().size());
        const bool all_ws = std::all_of(to_list.begin(), to_list.end(), is_bitsy_whitespace);
        const std::string to_cond = state.peak("?").substr(
            std::min(to_list.size(), state.peak("?").size()));
        const bool no_break = to_cond.find('\n') == std::string::npos;
        return found_list && all_ws && no_break;
    }

    struct ListLine {
        std::string text;
        int whitespace = 0;
        bool is_new = false;
    };

    // parseSequenceItemLine / parseConditionalItemLine.
    static ListLine read_list_line(ParserState& state, bool conditional) {
        ListLine line;
        bool seen_text = false;
        bool seen_cond_end = false;
        while (!state.done() && state.ch() != '\n') {
            if (!seen_text) {
                if (state.ch() == ' ' || state.ch() == '\t') {
                    ++line.whitespace;
                } else {
                    seen_text = true;
                    if (state.ch() == '-') {
                        line.is_new = true;
                        line.whitespace += 2;
                    }
                }
            }
            if (conditional && line.is_new && !seen_cond_end && state.ch() == '?') {
                seen_cond_end = true;
            }
            if (state.ch() == '{') {
                line.text += state.consume_block("{", "}", true);
            } else {
                if (!seen_cond_end) line.text.push_back(state.ch());
                state.step();
            }
        }
        if (state.ch() == '\n') state.step();
        return line;
    }

    void parse_conditional(ParserState& state, Node& root) {
        std::vector<std::string> conds;
        std::vector<std::string> results;
        int required = -1;
        while (!state.done()) {
            const ListLine line = read_list_line(state, true);
            if (line.is_new) {
                required = line.whitespace;
                conds.emplace_back();
                results.emplace_back();
            }
            if (line.whitespace >= required && !conds.empty()) {
                const std::string trimmed = slice_units(line.text, required);
                if (line.is_new) conds.back() += trimmed;
                else results.back() += trimmed + "\n";
            }
        }
        for (auto& r : results) drop_last(r);

        auto node = make(NodeType::If);
        for (std::size_t i = 0; i < conds.size(); ++i) {
            const std::string c = js_trim(conds[i]);
            node->conditions.push_back(c == "else" ? make(NodeType::Else) : create_expression(c));
            node->children.push_back(parse_dialog(results[i]));
        }
        root.children.push_back(std::move(node));
    }

    void parse_sequence(ParserState& state, Node& root, std::string_view kind) {
        std::vector<std::string> items;
        int required = -1;
        while (!state.done()) {
            const ListLine line = read_list_line(state, false);
            if (line.is_new) {
                required = line.whitespace;
                items.emplace_back();
            }
            if (line.whitespace >= required && !items.empty()) {
                items.back() += slice_units(line.text, required) + "\n";
            }
        }
        for (auto& it : items) drop_last(it);

        auto node = make(kind == "sequence" ? NodeType::Sequence
                         : kind == "cycle"  ? NodeType::Cycle
                                            : NodeType::Shuffle);
        for (auto& it : items) node->children.push_back(parse_dialog(it));
        if (node->type == NodeType::Shuffle) reshuffle(*node, random_);
        root.children.push_back(std::move(node));
    }

    void parse_function(ParserState& state, Node& root, const std::string& name) {
        auto fn = make(NodeType::Function);
        fn->name = name;
        std::string symbol;
        auto end_symbol = [&] {
            fn->args.push_back(string_to_value(js_trim(symbol)));
            symbol.clear();
        };
        while (!(state.ch() == '\n' || state.done())) {
            if (state.match_ahead("{")) {
                fn->args.push_back(parse_code(state.consume_block("{", "}")));
                symbol.clear();
            } else if (state.match_ahead("\"")) {
                fn->args.push_back(literal(DialogValue::from_string(state.consume_block("\"", "\""))));
                symbol.clear();
            } else if (state.ch() == ' ' && !symbol.empty()) {
                end_symbol();
            } else {
                symbol.push_back(state.ch());
            }
            state.step();
        }
        if (!symbol.empty()) end_symbol();
        root.children.push_back(std::move(fn));
    }

    std::unique_ptr<Node> string_to_value(const std::string& s) {
        if (!s.empty() && s[0] == '{') {
            ParserState tmp{s};
            return parse_code(tmp.consume_block("{", "}"));
        }
        if (!s.empty() && s[0] == '"') {
            std::string str;
            for (std::size_t i = 1; i < s.size() && s[i] != '"'; ++i) str.push_back(s[i]);
            return literal(DialogValue::from_string(std::move(str)));
        }
        if (s == "true") return literal(DialogValue::from_bool(true));
        if (s == "false") return literal(DialogValue::from_bool(false));
        if (const double n = js_parse_float(s); !std::isnan(n)) {
            return literal(DialogValue::from_number(n));
        }
        if (is_valid_variable_name(s)) {
            auto v = make(NodeType::Variable);
            v->name = s;
            return v;
        }
        return literal(DialogValue::null());
    }

    std::unique_ptr<Node> create_expression(std::string_view raw) {
        const std::string exp = js_trim(raw);

        auto inside_string = [&](std::size_t index) {
            bool in = false;
            for (std::size_t i = 0; i < exp.size(); ++i) {
                if (exp[i] == '"') in = !in;
                if (i == index) return in;
            }
            return false;
        };
        auto inside_code = [&](std::size_t index) {
            int count = 0;
            for (std::size_t i = 0; i < exp.size(); ++i) {
                if (exp[i] == '{') ++count;
                else if (exp[i] == '}') --count;
                if (i == index) return count > 0;
            }
            return false;
        };

        if (const auto set = exp.find('='); set != std::string::npos &&
                                            !inside_string(set) && !inside_code(set)) {
            const char after = set + 1 < exp.size() ? exp[set + 1] : '\0';
            const char before = set > 0 ? exp[set - 1] : '\0';
            if (after != '=' && before != '>' && before != '<') {
                auto node = make(NodeType::Operator);
                node->name = "=";
                const std::string var = js_trim(exp.substr(0, set));
                if (is_valid_variable_name(var)) {
                    node->left = make(NodeType::Variable);
                    node->left->name = var;
                } else {
                    node->left = literal(DialogValue::null());
                }
                node->right = create_expression(exp.substr(set + 1));
                return node;
            }
        }

        if (const auto q = exp.find('?'); q != std::string::npos &&
                                          !inside_string(q) && !inside_code(q)) {
            auto node = make(NodeType::If);
            node->conditions.push_back(create_expression(js_trim(exp.substr(0, q))));
            std::string result = exp.substr(q + 1);
            if (const auto colon = result.find(':'); colon != std::string::npos) {
                node->conditions.push_back(make(NodeType::Else));
                const std::string else_str = result.substr(colon + 1);
                result = result.substr(0, colon);
                node->children.push_back(parse_dialog(js_trim(result)));
                node->children.push_back(parse_dialog(js_trim(else_str)));
            } else {
                node->children.push_back(parse_dialog(js_trim(result)));
            }
            return node;
        }

        // Bitsy splits on the first operator found, in this order.
        for (const char* op : {"==", ">=", "<=", ">", "<", "-", "+", "/", "*"}) {
            const auto at = exp.find(op);
            if (at != std::string::npos && !inside_string(at) && !inside_code(at)) {
                auto node = make(NodeType::Operator);
                node->name = op;
                node->left = create_expression(exp.substr(0, at));
                node->right = create_expression(exp.substr(at + std::string_view(op).size()));
                return node;
            }
        }

        return string_to_value(exp);
    }

    static bool is_expression(const std::string& s) {
        ParserState tmp{s};
        std::string outside;
        while (!tmp.done()) {
            if (tmp.match_ahead("{")) {
                (void)tmp.consume_block("{", "}");
            } else {
                outside.push_back(tmp.ch());
                tmp.step();
            }
        }
        for (const char* op : {"?", "=", "==", ">=", "<=", ">", "<", "-", "+", "/", "*"}) {
            if (outside.find(op) != std::string::npos) return true;
        }
        return false;
    }

    static bool is_literal(const std::string& s) {
        const bool is_bool = s == "true" || s == "false";
        const bool is_num = !std::isnan(js_parse_float(s));
        const bool is_str = !s.empty() && s.front() == '"' && s.back() == '"';
        return is_bool || is_num || is_str || is_valid_variable_name(s) || s.empty();
    }

public:
    static void reshuffle(Node& node, const std::function<double()>& random) {
        std::vector<std::size_t> pool(node.children.size());
        for (std::size_t i = 0; i < pool.size(); ++i) pool[i] = i;
        node.shuffled.clear();
        while (!pool.empty()) {
            const auto i = static_cast<std::size_t>(
                std::floor(random() * static_cast<double>(pool.size())));
            node.shuffled.push_back(pool[std::min(i, pool.size() - 1)]);
            pool.erase(pool.begin() + static_cast<long>(std::min(i, pool.size() - 1)));
        }
    }
};

} // namespace
} // namespace script

using script::Node;
using script::NodeType;

// ===========================================================================
// DialogVM
// ===========================================================================

DialogVM::DialogVM() = default;
DialogVM::~DialogVM() = default;
DialogVM::DialogVM(DialogVM&&) noexcept = default;
DialogVM& DialogVM::operator=(DialogVM&&) noexcept = default;

double DialogVM::random() {
    // mulberry32, the generator tests/parity/bitsy_reference.mjs gives Bitsy.
    rng_state_ += 0x6D2B79F5u;
    std::uint32_t t = rng_state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

void DialogVM::reset() {
    compiled_.clear();
    anonymous_.reset();
    stack_.clear();
    pages_.clear();
    page_ = 0;
    paused_ = false;
    active_ = false;
    effects_.clear();
    last_was_break_ = false;
    on_end_ = {};
    world_ = {};
}

const std::vector<TextSpan>& DialogVM::spans() const {
    static const std::vector<TextSpan> kEmpty;
    return pages_.empty() ? kEmpty : pages_[page_];
}

void DialogVM::start(std::string_view source, DialogWorld world,
                     std::string script_id, std::function<void()> on_end) {
    stack_.clear();
    pages_.clear();
    page_ = 0;
    paused_ = false;
    active_ = false;
    effects_.clear();
    last_was_break_ = false;
    world_ = std::move(world);
    on_end_ = std::move(on_end);

    if (source.empty()) {
        finish();
        return;
    }

    auto rnd = [this] { return random(); };
    Node* root = nullptr;
    if (!script_id.empty()) {
        auto& slot = compiled_[script_id];
        if (!slot) slot = script::Parser(rnd).parse(source);
        root = slot.get();
    } else {
        anonymous_ = script::Parser(rnd).parse(source);
        root = anonymous_.get();
    }

    stack_.push_back({root});
    run();
    if (pages_.empty() && !paused_) {
        finish();
    } else {
        active_ = true;
    }
}

bool DialogVM::continue_page() {
    if (!active_) return false;
    if (page_ + 1 < pages_.size()) {
        ++page_;
        return true;
    }
    if (paused_) {
        paused_ = false;
        pages_.clear();
        page_ = 0;
        last_was_break_ = false;
        run();
        if (!pages_.empty() || paused_) return true;
    }
    finish();
    return false;
}

void DialogVM::finish() {
    active_ = false;
    stack_.clear();
    pages_.clear();
    page_ = 0;
    paused_ = false;
    auto cb = std::move(on_end_);
    on_end_ = {};
    if (cb) cb();
}

void DialogVM::run() {
    while (!stack_.empty() && !paused_) {
        Frame& f = stack_.back();
        if (f.next >= f.block->children.size()) {
            stack_.pop_back();
            continue;
        }
        Node& child = *f.block->children[f.next++];
        exec_statement(child);
    }
}

Node* DialogVM::choose(Node& list) {
    if (list.children.empty()) return nullptr;
    Node* chosen = nullptr;
    switch (list.type) {
        case NodeType::Sequence:
            chosen = list.children[list.index].get();
            if (list.index + 1 < list.children.size()) ++list.index;
            break;
        case NodeType::Cycle:
            chosen = list.children[list.index].get();
            list.index = list.index + 1 < list.children.size() ? list.index + 1 : 0;
            break;
        case NodeType::Shuffle:
            chosen = list.children[list.shuffled[list.index]].get();
            if (++list.index >= list.children.size()) {
                script::Parser::reshuffle(list, [this] { return random(); });
                list.index = 0;
            }
            break;
        default:
            break;
    }
    return chosen;
}

void DialogVM::exec_statement(Node& node) {
    switch (node.type) {
        case NodeType::DialogBlock:
        case NodeType::CodeBlock:
            stack_.push_back({&node});
            return;
        case NodeType::Sequence:
        case NodeType::Cycle:
        case NodeType::Shuffle:
            if (Node* chosen = choose(node)) stack_.push_back({chosen});
            return;
        case NodeType::If:
            for (std::size_t i = 0; i < node.conditions.size(); ++i) {
                const bool pass = node.conditions[i]->type == NodeType::Else ||
                                  eval(*node.conditions[i]).is_truthy();
                if (pass) {
                    if (i < node.children.size()) stack_.push_back({node.children[i].get()});
                    return;
                }
            }
            return;
        case NodeType::Function:
            if (node.name == "pg") {
                add_pagebreak();
                paused_ = true;
                return;
            }
            (void)eval(node);
            return;
        default:
            (void)eval(node);
            return;
    }
}

DialogValue DialogVM::eval_block(Node& node) {
    DialogValue last;
    for (auto& child : node.children) last = eval(*child);
    return last;
}

DialogValue DialogVM::eval(Node& node) {
    switch (node.type) {
        case NodeType::DialogBlock:
        case NodeType::CodeBlock:
            return eval_block(node);
        case NodeType::Literal:
            return node.value;
        case NodeType::Variable:
            if (world_.get_var) {
                if (auto v = world_.get_var(node.name)) return *v;
            }
            return DialogValue::null();
        case NodeType::Else:
            return DialogValue::from_bool(true);
        case NodeType::Undefined: {
            toggle_effect("_debug_highlight");
            add_text("{" + node.name + "}");
            toggle_effect("_debug_highlight");
            return DialogValue::null();
        }
        case NodeType::Sequence:
        case NodeType::Cycle:
        case NodeType::Shuffle:
            if (Node* chosen = choose(node)) return eval(*chosen);
            return DialogValue::null();
        case NodeType::If:
            for (std::size_t i = 0; i < node.conditions.size(); ++i) {
                if (eval(*node.conditions[i]).is_truthy()) {
                    return i < node.children.size() ? eval(*node.children[i])
                                                    : DialogValue::null();
                }
            }
            return DialogValue::null();
        case NodeType::Operator: {
            const std::string& op = node.name;
            if (op == "=") {
                if (node.left->type != NodeType::Variable) return DialogValue::null();
                const DialogValue r = eval(*node.right);
                if (world_.set_var) world_.set_var(node.left->name, r);
                return eval(*node.left);
            }
            // Bitsy evaluates the right side first.
            const DialogValue r = eval(*node.right);
            const DialogValue l = eval(*node.left);
            if (op == "==") return DialogValue::from_bool(l.strict_equals(r));
            if (op == ">")  return DialogValue::from_bool(js_less(r, l));
            if (op == "<")  return DialogValue::from_bool(js_less(l, r));
            if (op == ">=") return DialogValue::from_bool(js_less_eq(r, l));
            if (op == "<=") return DialogValue::from_bool(js_less_eq(l, r));
            if (op == "*")  return js_mul(l, r);
            if (op == "/")  return js_div(l, r);
            if (op == "+")  return js_add(l, r);
            if (op == "-")  return js_sub(l, r);
            return DialogValue::null();
        }
        case NodeType::Function: {
            std::vector<DialogValue> values;
            std::size_t i = 0;
            if (node.name == "property" && !node.args.empty()) {
                // The property name is a bare word; anything else drops every argument.
                if (node.args[0]->type == NodeType::Variable) {
                    values.push_back(DialogValue::from_string(node.args[0]->name));
                    i = 1;
                } else {
                    i = node.args.size();
                }
            }
            for (; i < node.args.size(); ++i) values.push_back(eval(*node.args[i]));
            if (node.name == "pg") {
                add_pagebreak();
                return DialogValue::null();
            }
            return call(node.name, values);
        }
    }
    return DialogValue::null();
}

DialogValue DialogVM::call(const std::string& name, std::vector<DialogValue>& args) {
    auto arg = [&](std::size_t i) -> const DialogValue* {
        return i < args.size() ? &args[i] : nullptr;
    };
    auto resolve = [&](std::string_view kind, const DialogValue* v) {
        // A missing argument is JS undefined, which becomes "undefined" as a key.
        const std::string key = v ? v->as_string() : std::string("undefined");
        return world_.resolve ? world_.resolve(kind, key) : key;
    };

    if (name == "say" || name == "print") {
        if (const DialogValue* v = arg(0); v && !v->is_null()) add_text(v->as_string());
        return DialogValue::null();
    }
    if (name == "br") {
        add_linebreak();
        return DialogValue::null();
    }
    if (name == "drws" || name == "printSprite" || name == "drwt" || name == "printTile" ||
        name == "drwi" || name == "printItem") {
        const char kind = (name == "drws" || name == "printSprite") ? 's'
                        : (name == "drwt" || name == "printTile")   ? 't'
                                                                    : 'i';
        const std::string_view kind_name = kind == 's' ? "sprite" : kind == 't' ? "tile" : "item";
        const std::string id = resolve(kind_name, arg(0));
        if (world_.drawing) {
            if (const TileFrame* f = world_.drawing(kind, id)) add_drawing(*f);
        }
        return DialogValue::null();
    }
    if (name == "item") {
        const std::string id = resolve("item", arg(0));
        double count = world_.item_count ? world_.item_count(id) : 0;
        if (args.size() > 1) {
            const DialogValue& v = args[1];
            double n = v.kind == DialogValue::Kind::Number && std::isfinite(v.number)
                ? std::trunc(v.number)
                : js_parse_int(v.as_string());
            if (std::isnan(n)) n = 0;  // Bitsy stores NaN, which then reads as 0
            n = std::max(0.0, n);
            if (world_.set_item) world_.set_item(id, n);
            count = n;
        }
        return DialogValue::from_number(count);
    }
    if (name == "property") {
        DialogValue out;
        if (const DialogValue* p = arg(0); p && p->is_truthy()) {
            const std::string pname = p->as_string();
            if (world_.has_property && world_.has_property(pname)) {
                if (args.size() > 1 && world_.set_property) world_.set_property(pname, args[1]);
                if (world_.get_property) out = world_.get_property(pname);
            }
        }
        return out;
    }
    if (name == "end") {
        if (world_.do_end) world_.do_end();
        return DialogValue::null();
    }
    if (name == "exit") {
        std::string room;
        std::optional<int> x, y;
        std::optional<std::string> effect;
        if (!args.empty()) room = resolve("room", arg(0));
        if (args.size() >= 3) {
            const double px = js_parse_int(args[1].as_string());
            const double py = js_parse_int(args[2].as_string());
            if (!std::isnan(px)) x = static_cast<int>(px);
            if (!std::isnan(py)) y = static_cast<int>(py);
        }
        if (args.size() >= 4) effect = args[3].as_string();
        if (world_.do_exit) return world_.do_exit(room, x, y, effect);
        return DialogValue::null();
    }
    if (name == "pal") {
        if (!world_.palette) return DialogValue::null();
        if (args.empty()) return world_.palette(std::nullopt);
        return world_.palette(resolve("palette", arg(0)));
    }
    if (name == "ava") {
        if (!world_.avatar) return DialogValue::null();
        if (args.empty()) return world_.avatar(std::nullopt);
        return world_.avatar(resolve("sprite", arg(0)));
    }
    if (name == "tune") {
        if (!world_.tune) return DialogValue::null();
        if (args.empty()) return world_.tune(std::nullopt);
        return world_.tune(resolve("tune", arg(0)));
    }
    if (name == "blip") {
        if (!args.empty() && world_.play_blip) world_.play_blip(resolve("blip", arg(0)));
        return DialogValue::null();
    }
    if (name == "clr1" || name == "clr2" || name == "clr3" || name == "wvy" ||
        name == "shk" || name == "rbw") {
        toggle_effect(name);
        return DialogValue::null();
    }
    if (name == "clr") {
        push_effect("clr", args.empty() ? DialogValue::null() : args[0]);
        return DialogValue::null();
    }
    if (name == "/clr" || name == "/wvy" || name == "/shk" || name == "/rbw") {
        const std::string effect = name.substr(1);
        if (has_effect(effect)) pop_effect(effect);
        return DialogValue::null();
    }
    // _debugOnlyPrintFont: an editor debugging aid; shows nothing in citsy.
    return DialogValue::null();
}

// ---------------------------------------------------------------------------
// Dialog buffer
// ---------------------------------------------------------------------------

std::vector<TextSpan>& DialogVM::cur_page() {
    if (pages_.empty()) pages_.emplace_back();
    return pages_.back();
}

void DialogVM::add_text(const std::string& text) {
    auto& page = cur_page();
    if (text.empty()) return;
    TextSpan sp;
    sp.text = text;
    sp.effects = effect_bits();
    sp.color = effect_color();
    page.push_back(std::move(sp));
    last_was_break_ = false;
}

void DialogVM::add_linebreak() {
    auto& page = cur_page();
    if (page.empty()) return;  // no blank rows at the top of a screen
    if (last_was_break_) {
        // An empty line between two lines of text starts a new screen
        // (citsy keeps its own screen size; this matches the old behaviour).
        if (!page.empty() && !page.back().is_drawing && page.back().text == "\n") {
            page.pop_back();
        }
        pages_.emplace_back();
        last_was_break_ = false;
        return;
    }
    TextSpan sp;
    sp.text = "\n";
    sp.effects = effect_bits();
    sp.color = effect_color();
    page.push_back(std::move(sp));
    last_was_break_ = true;
}

void DialogVM::add_pagebreak() {
    (void)cur_page();
    last_was_break_ = false;
}

void DialogVM::add_drawing(const TileFrame& frame) {
    auto& page = cur_page();
    TextSpan sp;
    sp.is_drawing = true;
    sp.drawing = frame;
    sp.effects = effect_bits();
    const int c = effect_color();
    sp.color = c;
    sp.drawing_color = c < 0 ? kTextboxWhite : static_cast<std::uint8_t>(c);
    page.push_back(std::move(sp));
    last_was_break_ = false;
}

bool DialogVM::has_effect(std::string_view name) const {
    return std::any_of(effects_.begin(), effects_.end(),
                       [&](const Effect& e) { return e.name == name; });
}

void DialogVM::push_effect(std::string name, DialogValue param) {
    effects_.push_back({std::move(name), std::move(param)});
}

void DialogVM::pop_effect(std::string_view name) {
    for (auto it = effects_.rbegin(); it != effects_.rend(); ++it) {
        if (it->name == name) {
            effects_.erase(std::next(it).base());
            return;
        }
    }
}

void DialogVM::toggle_effect(std::string name) {
    if (has_effect(name)) pop_effect(name);
    else push_effect(std::move(name));
}

std::uint8_t DialogVM::effect_bits() const {
    std::uint8_t bits = GlyphFx::None;
    for (const auto& e : effects_) {
        if (e.name == "wvy") bits |= GlyphFx::Wavy;
        else if (e.name == "shk") bits |= GlyphFx::Shaky;
        else if (e.name == "rbw") bits |= GlyphFx::Rainbow;
    }
    // Bitsy applies effects in the order they were turned on, and {rbw} and the
    // colours all set the glyph colour, so a colour turned on later wins.
    for (auto it = effects_.rbegin(); it != effects_.rend(); ++it) {
        if (it->name == "rbw") break;
        if (it->name.starts_with("clr") || it->name == "_debug_highlight") {
            bits &= static_cast<std::uint8_t>(~GlyphFx::Rainbow);
            break;
        }
    }
    return bits;
}

int DialogVM::effect_color() const {
    // Bitsy applies effects in the order they were turned on; the last colour wins.
    int color = -1;
    for (const auto& e : effects_) {
        if (e.name == "clr1" || e.name == "_debug_highlight") color = 0;
        else if (e.name == "clr2") color = 1;
        else if (e.name == "clr3") color = 2;
        else if (e.name == "clr") {
            const double n = e.param.as_number();
            color = std::isfinite(n) && n >= 0 ? static_cast<int>(n) : 0;
        }
    }
    return color;
}

// ===========================================================================
// Test helpers
// ===========================================================================

std::vector<std::string> extract_dialog_pages(std::string_view content) {
    std::vector<std::string> pages;
    DialogVM vm;
    vm.start(content, DialogWorld{});
    while (vm.active()) {
        pages.push_back(vm.plain_text());
        if (!vm.continue_page()) break;
    }
    return pages;
}

DialogScript parse_dialog_script(std::string_view source) {
    DialogScript script;
    script.source = std::string(source);
    return script;
}

DialogResult run_dialog_script(DialogScript& script, DialogWorld world) {
    DialogResult result;
    auto prev_end = world.do_end;
    auto prev_exit = world.do_exit;
    world.do_end = [&] {
        result.end_game = true;
        if (prev_end) prev_end();
    };
    world.do_exit = [&](std::string room, std::optional<int> x, std::optional<int> y,
                        std::optional<std::string> fx) {
        result.exit = DialogExit{room, x.value_or(0), y.value_or(0), fx.value_or("")};
        if (prev_exit) return prev_exit(std::move(room), x, y, std::move(fx));
        return DialogValue::null();
    };
    script.vm.start(script.source, std::move(world), "compat");
    while (script.vm.active()) {
        result.pages.push_back(script.vm.plain_text());
        if (!script.vm.continue_page()) break;
    }
    return result;
}

} // namespace citsy
