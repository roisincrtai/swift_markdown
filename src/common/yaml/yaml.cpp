// yaml.cpp -- the events in the test suite's format, the node graph, and
// the core schema. See yaml.hpp. Standard C++17 only.
#include "yaml.hpp"

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <unordered_set>

namespace common::yaml {

std::string events_text(const std::vector<Event>& events) {
    std::string out;
    auto properties = [&](const Event& event) {
        if (!event.anchor.empty()) out += " &" + event.anchor;
        if (!event.tag.empty()) out += " <" + event.tag + ">";
    };
    for (const Event& event : events) {
        switch (event.type) {
            case Event::StreamStart: out += "+STR"; break;
            case Event::StreamEnd: out += "-STR"; break;
            case Event::DocumentStart: out += event.explicit_marker ? "+DOC ---" : "+DOC"; break;
            case Event::DocumentEnd: out += event.explicit_marker ? "-DOC ..." : "-DOC"; break;
            case Event::MappingStart:
                out += event.flow ? "+MAP {}" : "+MAP";
                properties(event);
                break;
            case Event::MappingEnd: out += "-MAP"; break;
            case Event::SequenceStart:
                out += event.flow ? "+SEQ []" : "+SEQ";
                properties(event);
                break;
            case Event::SequenceEnd: out += "-SEQ"; break;
            case Event::Alias: out += "=ALI *" + event.anchor; break;
            case Event::Scalar: {
                out += "=VAL";
                properties(event);
                switch (event.style) {
                    case ScalarStyle::Plain: out += " :"; break;
                    case ScalarStyle::SingleQuoted: out += " '"; break;
                    case ScalarStyle::DoubleQuoted: out += " \""; break;
                    case ScalarStyle::Literal: out += " |"; break;
                    case ScalarStyle::Folded: out += " >"; break;
                }
                for (char c : event.value) {
                    switch (c) {
                        case '\\': out += "\\\\"; break;
                        case '\b': out += "\\b"; break;
                        case '\t': out += "\\t"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        default: out += c;
                    }
                }
                break;
            }
        }
        out += '\n';
    }
    return out;
}

std::vector<Document> load_yaml_all(std::string_view text, const Options& options) {
    std::vector<Event> events = parse_events(text, options);
    std::vector<Document> documents;
    std::size_t i = 0;
    while (i < events.size()) {
        const Event& event = events[i];
        if (event.type != Event::DocumentStart) {
            ++i;
            continue;
        }
        Document document;
        std::map<std::string, const Node*> anchors;
        std::vector<Node*> open;
        ++i;
        for (; i < events.size() && events[i].type != Event::DocumentEnd; ++i) {
            const Event& e = events[i];
            const Node* made = nullptr;
            if (e.type == Event::SequenceEnd || e.type == Event::MappingEnd) {
                made = open.back();
                open.pop_back();
            } else if (e.type == Event::Alias) {
                auto found = anchors.find(e.anchor);
                if (found == anchors.end()) throw YamlError("found an alias to an undefined anchor *" + e.anchor, e.start);
                made = found->second;
            } else {
                document.nodes.push_back(std::make_unique<Node>());
                Node* node = document.nodes.back().get();
                node->tag = e.tag;
                node->anchor = e.anchor;
                node->start = e.start;
                node->flow = e.flow;
                if (e.type == Event::Scalar) {
                    node->kind = Node::Scalar;
                    node->value = e.value;
                    node->style = e.style;
                } else {
                    node->kind = e.type == Event::SequenceStart ? Node::Sequence : Node::Mapping;
                }
                // An anchor names the node from here on, so a collection can refer to itself.
                if (!e.anchor.empty()) anchors[e.anchor] = node;
                if (node->kind != Node::Scalar) {
                    open.push_back(node);
                    continue;
                }
                made = node;
            }
            if (open.empty()) document.root = made;
            else open.back()->items.push_back(made);
        }
        ++i;
        documents.push_back(std::move(document));
    }
    return documents;
}

Document load_yaml(std::string_view text, const Options& options) {
    std::vector<Document> documents = load_yaml_all(text, options);
    if (documents.size() != 1)
        throw YamlError("expected one document, found " + std::to_string(documents.size()), Mark{});
    return std::move(documents[0]);
}

namespace {

constexpr const char* CORE = "tag:yaml.org,2002:";

bool all_of(const std::string& s, std::size_t from, bool (*ok)(char)) {
    if (from >= s.size()) return false;
    for (std::size_t i = from; i < s.size(); ++i)
        if (!ok(s[i])) return false;
    return true;
}
bool decimal(char c) { return c >= '0' && c <= '9'; }
bool octal(char c) { return c >= '0' && c <= '7'; }
bool hexadecimal(char c) { return decimal(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool is_null(const std::string& s) { return s.empty() || s == "~" || s == "null" || s == "Null" || s == "NULL"; }
int as_bool(const std::string& s) {
    if (s == "true" || s == "True" || s == "TRUE") return 1;
    if (s == "false" || s == "False" || s == "FALSE") return 0;
    return -1;
}
bool is_int(const std::string& s) {
    if (s.size() > 2 && s[0] == '0' && s[1] == 'o') return all_of(s, 2, octal);
    if (s.size() > 2 && s[0] == '0' && s[1] == 'x') return all_of(s, 2, hexadecimal);
    std::size_t from = !s.empty() && (s[0] == '-' || s[0] == '+') ? 1 : 0;
    return all_of(s, from, decimal);
}
// [-+]? ( \. [0-9]+ | [0-9]+ ( \. [0-9]* )? ) ( [eE] [-+]? [0-9]+ )?
bool is_float(const std::string& s) {
    std::size_t i = 0, n = s.size();
    if (i < n && (s[i] == '-' || s[i] == '+')) ++i;
    std::size_t digits = 0;
    while (i < n && decimal(s[i])) ++i, ++digits;
    if (i < n && s[i] == '.') {
        ++i;
        std::size_t fraction = 0;
        while (i < n && decimal(s[i])) ++i, ++fraction;
        if (!digits && !fraction) return false;
    } else if (!digits) {
        return false;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '-' || s[i] == '+')) ++i;
        std::size_t exponent = 0;
        while (i < n && decimal(s[i])) ++i, ++exponent;
        if (!exponent) return false;
    }
    return i == n;
}
int special_float(const std::string& s) {   // 1: inf, -1: -inf, 2: nan, 0: none
    std::string body = s;
    int sign = 1;
    if (!body.empty() && (body[0] == '-' || body[0] == '+')) {
        sign = body[0] == '-' ? -1 : 1;
        body = body.substr(1);
    }
    if (body == ".inf" || body == ".Inf" || body == ".INF") return sign;
    if (s == ".nan" || s == ".NaN" || s == ".NAN") return 2;
    return 0;
}

json::Json string_json(const std::string& s) {
    json::Json value;
    value.type = json::Json::String;
    value.text = s;
    return value;
}

json::Json number_json(const std::string& s, const Mark& mark) {
    json::Json value;
    int special = special_float(s);
    if (special) {
        value.type = json::Json::Float;
        value.real = special == 2 ? std::numeric_limits<double>::quiet_NaN()
                                  : special * std::numeric_limits<double>::infinity();
        return value;
    }
    if (is_int(s)) {
        int base = 10;
        std::string digits = s;
        bool negative = false;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'x')) {
            base = s[1] == 'o' ? 8 : 16;
            digits = s.substr(2);
        } else if (s[0] == '-' || s[0] == '+') {
            negative = s[0] == '-';
            digits = s.substr(1);
        }
        // Exact in 64 bits, or else a BigInt of its decimal digits.
        const std::uint64_t limit = negative ? (std::uint64_t{1} << 63) : (std::uint64_t{1} << 63) - 1;
        std::uint64_t acc = 0;
        bool big = false;
        for (char c : digits) {
            std::uint64_t d = static_cast<std::uint64_t>(decimal(c) ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10));
            std::uint64_t b = static_cast<std::uint64_t>(base);
            if (acc > (limit - d) / b) {
                big = true;
                break;
            }
            acc = acc * b + d;
        }
        if (!big) {
            value.type = json::Json::Int;
            value.integer = negative ? (acc == (std::uint64_t{1} << 63) ? std::numeric_limits<std::int64_t>::min()
                                                                         : -static_cast<std::int64_t>(acc))
                                     : static_cast<std::int64_t>(acc);
            return value;
        }
        if (base == 10) {
            std::size_t first = digits.find_first_not_of('0');
            value.type = json::Json::BigInt;
            value.text = (negative ? "-" : "") + (first == std::string::npos ? std::string("0") : digits.substr(first));
            value.real = std::strtod(value.text.c_str(), nullptr);
            return value;
        }
        // A wide octal or hexadecimal integer: its decimal digits, by long division.
        std::vector<int> number;   // decimal digits, most significant first
        for (char c : digits) {
            int d = decimal(c) ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10);
            int carry = d;
            for (auto it = number.rbegin(); it != number.rend(); ++it) {
                int v = *it * base + carry;
                *it = v % 10;
                carry = v / 10;
            }
            while (carry) {
                number.insert(number.begin(), carry % 10);
                carry /= 10;
            }
        }
        value.type = json::Json::BigInt;
        for (int d : number) value.text += static_cast<char>('0' + d);
        if (value.text.empty()) value.text = "0";
        value.real = std::strtod(value.text.c_str(), nullptr);
        return value;
    }
    if (is_float(s)) {
        // strtod reads the decimal point of the C locale; the text is swapped to it first, as
        // json.cpp does.
        std::string text = s;
        char point = std::use_facet<std::numpunct<char>>(std::locale()).decimal_point();
        const char* c_point = std::localeconv()->decimal_point;
        if (c_point && *c_point && *c_point != '.')
            for (char& c : text)
                if (c == '.') c = *c_point;
        (void)point;
        value.type = json::Json::Float;
        value.real = std::strtod(text.c_str(), nullptr);
        return value;
    }
    throw YamlError("\"" + s + "\" is not a number", mark);
}

json::Json scalar_json(const Node& node) {
    const std::string& s = node.value;
    std::string tag = node.tag;
    if (tag == std::string(CORE) + "str" || tag == "!") return string_json(s);
    if (tag == std::string(CORE) + "null") {
        if (!is_null(s)) throw YamlError("\"" + s + "\" is not a null", node.start);
        return json::Json();
    }
    if (tag == std::string(CORE) + "bool") {
        int b = as_bool(s);
        if (b < 0) throw YamlError("\"" + s + "\" is not a boolean", node.start);
        json::Json value;
        value.type = json::Json::Bool;
        value.boolean = b == 1;
        return value;
    }
    if (tag == std::string(CORE) + "int") {
        if (!is_int(s)) throw YamlError("\"" + s + "\" is not an integer", node.start);
        return number_json(s, node.start);
    }
    if (tag == std::string(CORE) + "float") {
        if (!is_float(s) && !special_float(s) && !is_int(s)) throw YamlError("\"" + s + "\" is not a float", node.start);
        json::Json value = number_json(s, node.start);
        if (value.type == json::Json::Int) {
            value.type = json::Json::Float;
            value.real = static_cast<double>(value.integer);
        } else if (value.type == json::Json::BigInt) {
            value.type = json::Json::Float;
        }
        return value;
    }
    if (!tag.empty() || node.style != ScalarStyle::Plain) return string_json(s);
    if (is_null(s)) return json::Json();
    int b = as_bool(s);
    if (b >= 0) {
        json::Json value;
        value.type = json::Json::Bool;
        value.boolean = b == 1;
        return value;
    }
    if (is_int(s) || is_float(s) || special_float(s)) return number_json(s, node.start);
    return string_json(s);
}

// The text a key is written as: a string as it is, any other scalar as its source text.
std::string key_text(const Node& key) {
    if (key.kind != Node::Scalar) throw YamlError("a mapping key that is a collection has no JSON form", key.start);
    return key.value;
}

}  // namespace

// Iterative, with an explicit stack, so no document can overflow the call stack; a node reached
// again while it is still open is an alias cycle.
json::Json to_json(const Node& root) {
    struct Frame {
        const Node* node;
        json::Json* out;
        std::size_t next;
    };
    json::Json result;
    std::vector<Frame> stack;
    std::unordered_set<const Node*> open;
    auto start = [&](const Node* node, json::Json* out) {
        if (node->kind == Node::Scalar) {
            *out = scalar_json(*node);
            return;
        }
        if (!open.insert(node).second) throw YamlError("an alias cycle has no JSON form", node->start);
        out->type = node->kind == Node::Sequence ? json::Json::Array : json::Json::Object;
        if (node->kind == Node::Mapping && node->items.size() % 2)
            throw YamlError("internal error: a mapping with a key and no value", node->start);
        out->items.reserve(node->kind == Node::Sequence ? node->items.size() : node->items.size() / 2);
        stack.push_back({node, out, 0});
    };
    start(&root, &result);
    while (!stack.empty()) {
        Frame& frame = stack.back();
        const Node* node = frame.node;
        if (frame.next >= node->items.size()) {
            open.erase(node);
            stack.pop_back();
            continue;
        }
        json::Json* out = frame.out;
        const Node* child;
        if (node->kind == Node::Sequence) {
            child = node->items[frame.next++];
            out->items.emplace_back();
        } else {
            std::string key = key_text(*node->items[frame.next]);
            child = node->items[frame.next + 1];
            frame.next += 2;
            // A repeated key keeps its first position and takes its last value, as json.hpp does.
            std::size_t position = out->keys.size();
            for (std::size_t k = 0; k < out->keys.size(); ++k)
                if (out->keys[k] == key) position = k;
            if (position == out->keys.size()) {
                out->keys.push_back(key);
                out->items.emplace_back();
            } else {
                out->items[position] = json::Json();
            }
            json::Json* slot = &out->items[position];
            start(child, slot);
            continue;
        }
        start(child, &out->items.back());
    }
    return result;
}

std::vector<json::Json> parse_yaml_all(std::string_view text, const Options& options) {
    std::vector<json::Json> out;
    for (const Document& document : load_yaml_all(text, options)) out.push_back(to_json(*document.root));
    return out;
}

json::Json parse_yaml(std::string_view text, const Options& options) {
    Document document = load_yaml(text, options);
    return to_json(*document.root);
}

}  // namespace common::yaml
