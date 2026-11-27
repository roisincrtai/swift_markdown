// tokenizer/cpp/scanner.cpp -- see scanner.hpp. Standard C++17 only.
#include "yaml_scanner.hpp"

#include <algorithm>

namespace mira_bpe {
namespace yaml {

YamlError::YamlError(const std::string& what, const Mark& where)
    : std::runtime_error(what + " (line " + std::to_string(where.line + 1) + ", column " +
                         std::to_string(where.column + 1) + ")"),
      mark(where) {}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

namespace {

Mark mark_of(const std::vector<std::uint32_t>& cps, std::size_t index) {
    Mark mark;
    for (std::size_t i = 0; i < index && i < cps.size(); ++i) {
        if (cps[i] == '\n' || (cps[i] == '\r' && (i + 1 >= cps.size() || cps[i + 1] != '\n'))) {
            ++mark.line;
            mark.column = 0;
        } else {
            ++mark.column;
        }
    }
    mark.index = index;
    return mark;
}

// c-printable, and inside quoted scalars nb-json: x9 | x20..x10FFFF. The characters every context
// refuses are the C0 controls but tab, line feed and carriage return, the surrogates, and xFFFE and
// xFFFF.
bool acceptable(std::uint32_t c) {
    if (c < 0x20) return c == 0x09 || c == 0x0A || c == 0x0D;
    if (c >= 0xD800 && c <= 0xDFFF) return false;
    if (c == 0xFFFE || c == 0xFFFF) return false;
    return c <= 0x10FFFF;
}

}  // namespace

std::vector<std::uint32_t> decode_stream(std::string_view bytes) {
    const auto* b = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t n = bytes.size();
    enum { Utf8, Utf16Le, Utf16Be, Utf32Le, Utf32Be } encoding = Utf8;
    if (n >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0xFE && b[3] == 0xFF) encoding = Utf32Be;
    else if (n >= 4 && b[0] == 0xFF && b[1] == 0xFE && b[2] == 0 && b[3] == 0) encoding = Utf32Le;
    else if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) encoding = Utf16Be;
    else if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) encoding = Utf16Le;
    else if (n >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] != 0) encoding = Utf32Be;
    else if (n >= 4 && b[0] != 0 && b[1] == 0 && b[2] == 0 && b[3] == 0) encoding = Utf32Le;
    else if (n >= 2 && b[0] == 0 && b[1] != 0) encoding = Utf16Be;
    else if (n >= 2 && b[0] != 0 && b[1] == 0) encoding = Utf16Le;
    std::vector<std::uint32_t> out;
    out.reserve(n);
    auto bad = [&](const char* what) { throw YamlError(what, mark_of(out, out.size())); };
    std::size_t i = 0;
    if (encoding == Utf8) {
        while (i < n) {
            unsigned char c = b[i];
            std::uint32_t cp;
            int length;
            if (c < 0x80) {
                cp = c;
                length = 1;
            } else if (c >= 0xC2 && c <= 0xDF) {
                cp = c & 0x1F;
                length = 2;
            } else if (c >= 0xE0 && c <= 0xEF) {
                cp = c & 0x0F;
                length = 3;
            } else if (c >= 0xF0 && c <= 0xF4) {
                cp = c & 0x07;
                length = 4;
            } else {
                bad("invalid UTF-8");
            }
            if (i + static_cast<std::size_t>(length) > n) bad("invalid UTF-8: truncated sequence");
            for (int k = 1; k < length; ++k) {
                unsigned char d = b[i + static_cast<std::size_t>(k)];
                if ((d & 0xC0) != 0x80) bad("invalid UTF-8");
                cp = (cp << 6) | (d & 0x3F);
            }
            if ((length == 3 && cp < 0x800) || (length == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
                bad("invalid UTF-8: overlong or out of range");
            if (!acceptable(cp)) bad("found a character that YAML does not allow");
            out.push_back(cp);
            i += static_cast<std::size_t>(length);
        }
    } else if (encoding == Utf16Le || encoding == Utf16Be) {
        if (n % 2) bad("invalid UTF-16: odd length");
        auto unit = [&](std::size_t k) {
            return encoding == Utf16Le ? static_cast<std::uint32_t>(b[k] | (b[k + 1] << 8))
                                       : static_cast<std::uint32_t>((b[k] << 8) | b[k + 1]);
        };
        while (i < n) {
            std::uint32_t u = unit(i);
            i += 2;
            if (u >= 0xD800 && u <= 0xDBFF) {
                if (i >= n) bad("invalid UTF-16: lone surrogate");
                std::uint32_t v = unit(i);
                if (v < 0xDC00 || v > 0xDFFF) bad("invalid UTF-16: lone surrogate");
                i += 2;
                u = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
            } else if (u >= 0xDC00 && u <= 0xDFFF) {
                bad("invalid UTF-16: lone surrogate");
            }
            if (!acceptable(u)) bad("found a character that YAML does not allow");
            out.push_back(u);
        }
    } else {
        if (n % 4) bad("invalid UTF-32: length not a multiple of 4");
        while (i < n) {
            std::uint32_t u = encoding == Utf32Le
                                  ? static_cast<std::uint32_t>(b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) |
                                                               (static_cast<std::uint32_t>(b[i + 3]) << 24))
                                  : static_cast<std::uint32_t>((static_cast<std::uint32_t>(b[i]) << 24) |
                                                               (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]);
            i += 4;
            if (!acceptable(u)) bad("found a character that YAML does not allow");
            out.push_back(u);
        }
    }
    return out;
}

const char* token_name(Token::Type type) {
    switch (type) {
        case Token::StreamStart: return "<stream start>";
        case Token::StreamEnd: return "<stream end>";
        case Token::VersionDirective: return "<%YAML directive>";
        case Token::TagDirective: return "<%TAG directive>";
        case Token::DocumentStart: return "'---'";
        case Token::DocumentEnd: return "'...'";
        case Token::BlockSequenceStart: return "<block sequence start>";
        case Token::BlockMappingStart: return "<block mapping start>";
        case Token::BlockEnd: return "<block end>";
        case Token::FlowSequenceStart: return "'['";
        case Token::FlowMappingStart: return "'{'";
        case Token::FlowSequenceEnd: return "']'";
        case Token::FlowMappingEnd: return "'}'";
        case Token::BlockEntry: return "'-'";
        case Token::FlowEntry: return "','";
        case Token::Key: return "'?' or an implicit key";
        case Token::Value: return "':'";
        case Token::Alias: return "<alias>";
        case Token::Anchor: return "<anchor>";
        case Token::Tag: return "<tag>";
        case Token::Scalar: return "<scalar>";
    }
    return "<token>";
}

Scanner::Scanner(std::string_view text) : text_(decode_stream(text)) {}

void Scanner::forward(std::size_t n) {
    for (std::size_t k = 0; k < n && mark_.index < text_.size(); ++k) {
        std::uint32_t c = text_[mark_.index++];
        if (c == '\n' || (c == '\r' && at() != '\n')) {
            ++mark_.line;
            mark_.column = 0;
        } else {
            ++mark_.column;
        }
    }
}

std::string Scanner::take_break() {
    if (at() == '\r' && at(1) == '\n') forward(2);
    else forward(1);
    return "\n";
}

void Scanner::fail(const std::string& message) const { throw YamlError(message, mark_); }
void Scanner::fail(const std::string& message, const Mark& mark) const { throw YamlError(message, mark); }

const Token& Scanner::peek() {
    while (need_more_tokens()) fetch_more_tokens();
    return tokens_.front();
}

Token Scanner::next() {
    peek();
    Token token = std::move(tokens_.front());
    tokens_.pop_front();
    ++tokens_taken_;
    return token;
}

bool Scanner::need_more_tokens() {
    if (stream_end_produced_) return false;
    if (tokens_.empty()) return true;
    stale_simple_keys();
    for (const SimpleKey& key : simple_keys_)
        if (key.possible && key.token_number == tokens_taken_) return true;
    return false;
}

Token Scanner::make(Token::Type type, const Mark& start) const {
    Token token;
    token.type = type;
    token.start = start;
    token.end = mark_;
    return token;
}

void Scanner::push(Token token) { tokens_.push_back(std::move(token)); }

void Scanner::stale_simple_keys() {
    for (SimpleKey& key : simple_keys_) {
        std::size_t level = static_cast<std::size_t>(&key - simple_keys_.data());
        bool multi_line_ok = level > 0 && flow_mapping_[level - 1];
        if (key.possible && ((!multi_line_ok && key.mark.line < mark_.line) || key.mark.index + 1024 < mark_.index)) {
            if (key.required) fail("could not find the ':' of an implicit key", key.mark);
            key.possible = false;
        }
    }
}

void Scanner::save_simple_key() {
    bool required = flow_level_ == 0 && indent_ == static_cast<int>(mark_.column);
    if (simple_key_allowed_) {
        remove_simple_key();
        SimpleKey& key = simple_keys_.back();
        key.possible = true;
        key.required = required;
        key.token_number = tokens_taken_ + tokens_.size();
        key.mark = mark_;
        key.after_tab = tab_before_;
    }
}

void Scanner::remove_simple_key() {
    SimpleKey& key = simple_keys_.back();
    if (key.possible && key.required) fail("could not find the ':' of an implicit key", key.mark);
    key.possible = false;
}

void Scanner::unwind_indent(int column) {
    if (flow_level_) return;
    while (indent_ > column) {
        push(make(Token::BlockEnd, mark_));
        indent_ = indents_.back();
        indents_.pop_back();
    }
}

bool Scanner::add_indent(int column) {
    if (flow_level_) return false;
    if (indent_ < column) {
        indents_.push_back(indent_);
        indent_ = column;
        return true;
    }
    return false;
}

bool Scanner::check_document_indicator(char c) const {
    return mark_.column == 0 && at() == static_cast<std::uint32_t>(c) && at(1) == static_cast<std::uint32_t>(c) &&
           at(2) == static_cast<std::uint32_t>(c) && blankz(3);
}

// ns-plain-safe(c) at k: a non-blank character, and in flow context not a flow indicator.
bool Scanner::plain_safe(std::size_t k) const {
    if (blankz(k)) return false;
    return !(flow_level_ && flow_indicator(k));
}

bool Scanner::check_plain() const {
    std::uint32_t c = at();
    if (blankz() || c == 0xFEFF) return false;
    switch (c) {
        case '-': case '?': case ':':
            return plain_safe(1);
        case ',': case '[': case ']': case '{': case '}': case '#': case '&': case '*': case '!':
        case '|': case '>': case '\'': case '"': case '%': case '@': case '`':
            return false;
        default:
            return true;
    }
}

void Scanner::scan_to_next_token() {
    while (true) {
        if (at() == 0xFEFF && mark_.column == 0) {
            forward();
            continue;
        }
        while (at() == ' ' || at() == '\t') {
            if (at() == '\t') tab_before_ = true;
            forward();
        }
        if (at() == '#') {
            std::uint32_t before = mark_.index ? text_[mark_.index - 1] : '\n';
            if (before != ' ' && before != '\t' && before != '\n' && before != '\r' && before != 0xFEFF)
                fail("a comment must be separated from what comes before it by whitespace");
            while (!line_break() && at() != 0) forward();
        }
        if (line_break()) {
            take_break();
            tab_before_ = false;
            if (flow_level_ == 0) {
                simple_key_allowed_ = true;
            } else {
                // A continuation line of a flow collection in block context must be indented more
                // than the block collection around it.
                std::size_t spaces = 0;
                while (at(spaces) == ' ') ++spaces;
                std::size_t k = spaces;
                while (blank(k)) ++k;
                bool empty = at(k) == '\r' || at(k) == '\n' || at(k) == 0 || at(k) == '#';
                if (!empty && static_cast<int>(spaces) <= indent_) {
                    forward(spaces);
                    fail("a flow collection's continuation line must be indented more than its block context");
                }
            }
            continue;
        }
        break;
    }
}

void Scanner::fetch_more_tokens() {
    if (!stream_start_produced_) {
        stream_start_produced_ = true;
        simple_key_allowed_ = true;
        simple_keys_.assign(1, SimpleKey{});
        push(make(Token::StreamStart, mark_));
        return;
    }
    bool adjacent = adjacent_value_ok_;
    adjacent_value_ok_ = false;
    tab_before_ = false;
    scan_to_next_token();
    stale_simple_keys();
    unwind_indent(static_cast<int>(mark_.column));
    std::uint32_t c = at();
    if (c == 0) return fetch_stream_end();
    if (mark_.column == 0 && c == '%') return fetch_directive();
    if (check_document_indicator('-')) return fetch_document_indicator(Token::DocumentStart);
    if (check_document_indicator('.')) return fetch_document_indicator(Token::DocumentEnd);
    switch (c) {
        case '[': return fetch_flow_collection_start(Token::FlowSequenceStart);
        case '{': return fetch_flow_collection_start(Token::FlowMappingStart);
        case ']': return fetch_flow_collection_end(Token::FlowSequenceEnd);
        case '}': return fetch_flow_collection_end(Token::FlowMappingEnd);
        case ',': return fetch_flow_entry();
        default: break;
    }
    if (c == '-' && blankz(1)) return fetch_block_entry();
    if (c == '?' && (blankz(1) || (flow_level_ && flow_indicator(1)))) return fetch_key();
    if (c == ':' && (blankz(1) || (flow_level_ && (flow_indicator(1) || adjacent)))) return fetch_value();
    if (c == '*') return fetch_anchor(Token::Alias);
    if (c == '&') return fetch_anchor(Token::Anchor);
    if (c == '!') return fetch_tag();
    if ((c == '|' || c == '>') && flow_level_ == 0) return fetch_block_scalar(c == '|');
    if (c == '\'' || c == '"') return fetch_flow_scalar(c == '"');
    if (check_plain()) return fetch_plain();
    fail("found a character that cannot start any token");
}

void Scanner::fetch_stream_end() {
    unwind_indent(-1);
    remove_simple_key();
    simple_key_allowed_ = false;
    if (flow_level_) fail("the stream ends inside a flow collection");
    push(make(Token::StreamEnd, mark_));
    stream_end_produced_ = true;
}

// Blanks, a comment, and a line break or the end of the stream.
void Scanner::skip_comment_and_break(const char* where) {
    while (blank()) forward();
    if (at() == '#') {
        std::uint32_t before = mark_.index ? text_[mark_.index - 1] : '\n';
        if (before != ' ' && before != '\t') fail("a comment must be separated from what comes before it by whitespace");
        while (!line_break() && at() != 0) forward();
    }
    if (!line_break() && at() != 0) fail(std::string("expected a comment or a line break after ") + where);
    if (line_break()) {
        take_break();
        if (flow_level_ == 0) simple_key_allowed_ = true;
    }
}

void Scanner::fetch_directive() {
    unwind_indent(-1);
    remove_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_;
    forward();
    std::string name;
    while (!blankz()) {
        append_utf8(name, at());
        forward();
    }
    if (name.empty()) fail("expected a directive name");
    if (name == "YAML") {
        if (!blank()) fail("expected whitespace after %YAML");
        while (blank()) forward();
        auto number = [&]() {
            if (at() < '0' || at() > '9') fail("expected a version number in the %YAML directive");
            int value = 0;
            while (at() >= '0' && at() <= '9') {
                value = value * 10 + static_cast<int>(at() - '0');
                if (value > 1000000) fail("the version number is too large");
                forward();
            }
            return value;
        };
        Token token;
        token.major = number();
        if (at() != '.') fail("expected '.' in the %YAML directive's version");
        forward();
        token.minor = number();
        if (!blankz()) fail("expected whitespace after the %YAML directive's version");
        token.type = Token::VersionDirective;
        token.start = start;
        token.end = mark_;
        skip_comment_and_break("a directive");
        push(std::move(token));
        return;
    }
    if (name == "TAG") {
        if (!blank()) fail("expected whitespace after %TAG");
        while (blank()) forward();
        Token token;
        token.handle = scan_tag_handle(true, start);
        if (!blank()) fail("expected whitespace after the %TAG directive's handle");
        while (blank()) forward();
        token.value = scan_tag_uri(false, true, start);
        if (token.value.empty()) fail("expected the %TAG directive's prefix");
        if (!blankz()) fail("expected whitespace after the %TAG directive's prefix");
        token.type = Token::TagDirective;
        token.start = start;
        token.end = mark_;
        skip_comment_and_break("a directive");
        push(std::move(token));
        return;
    }
    // A reserved directive: its parameters are ignored, as the specification asks.
    while (blank()) {
        while (blank()) forward();
        if (at() == '#') break;
        while (!blankz()) forward();
    }
    skip_comment_and_break("a directive");
}

void Scanner::fetch_document_indicator(Token::Type type) {
    unwind_indent(-1);
    remove_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_;
    forward(3);
    push(make(type, start));
    if (type == Token::DocumentEnd) skip_comment_and_break("'...'");
    // A block collection cannot start on the line of '---' (s-l-comments comes before it).
    else simple_key_allowed_ = false;
}

void Scanner::fetch_flow_collection_start(Token::Type type) {
    save_simple_key();
    ++flow_level_;
    if (flow_level_ > 100000) fail("flow collections are nested too deeply");
    simple_keys_.push_back(SimpleKey{});
    flow_mapping_.push_back(type == Token::FlowMappingStart);
    simple_key_allowed_ = true;
    Mark start = mark_;
    forward();
    push(make(type, start));
}

void Scanner::fetch_flow_collection_end(Token::Type type) {
    remove_simple_key();
    if (flow_level_ > 0) {
        --flow_level_;
        simple_keys_.pop_back();
        flow_mapping_.pop_back();
    }
    simple_key_allowed_ = false;
    Mark start = mark_;
    forward();
    push(make(type, start));
    adjacent_value_ok_ = true;
}

void Scanner::fetch_flow_entry() {
    simple_key_allowed_ = true;
    remove_simple_key();
    Mark start = mark_;
    forward();
    push(make(Token::FlowEntry, start));
}

void Scanner::fetch_block_entry() {
    if (flow_level_) fail("a block sequence entry cannot appear inside a flow collection");
    if (!simple_key_allowed_) fail("a block sequence entry is not allowed here");
    if (add_indent(static_cast<int>(mark_.column))) {
        if (tab_before_) fail("a tab cannot indent a block sequence");
        push(make(Token::BlockSequenceStart, mark_));
    }
    simple_key_allowed_ = true;
    remove_simple_key();
    Mark start = mark_;
    forward();
    push(make(Token::BlockEntry, start));
}

void Scanner::fetch_key() {
    if (flow_level_ == 0) {
        if (!simple_key_allowed_) fail("a mapping key is not allowed here");
        if (add_indent(static_cast<int>(mark_.column))) {
            if (tab_before_) fail("a tab cannot indent a block mapping");
            push(make(Token::BlockMappingStart, mark_));
        }
    }
    simple_key_allowed_ = flow_level_ == 0;
    remove_simple_key();
    Mark start = mark_;
    forward();
    push(make(Token::Key, start));
}

void Scanner::fetch_value() {
    SimpleKey& key = simple_keys_.back();
    if (key.possible) {
        auto position = tokens_.begin() + static_cast<std::ptrdiff_t>(key.token_number - tokens_taken_);
        Token key_token;
        key_token.type = Token::Key;
        key_token.start = key_token.end = key.mark;
        position = tokens_.insert(position, key_token);
        if (flow_level_ == 0 && add_indent(static_cast<int>(key.mark.column))) {
            if (key.after_tab) fail("a tab cannot indent a block mapping", key.mark);
            Token start;
            start.type = Token::BlockMappingStart;
            start.start = start.end = key.mark;
            tokens_.insert(position, start);
        }
        key.possible = false;
        simple_key_allowed_ = false;
    } else {
        if (flow_level_ == 0) {
            if (!simple_key_allowed_) fail("a mapping value is not allowed here");
            if (add_indent(static_cast<int>(mark_.column))) {
                if (tab_before_) fail("a tab cannot indent a block mapping");
                push(make(Token::BlockMappingStart, mark_));
            }
        }
        simple_key_allowed_ = flow_level_ == 0;
        remove_simple_key();
    }
    Mark start = mark_;
    forward();
    push(make(Token::Value, start));
}

void Scanner::fetch_anchor(Token::Type type) {
    save_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_;
    forward();
    std::string name;
    while (!blankz() && !flow_indicator()) {
        append_utf8(name, at());
        forward();
    }
    if (name.empty()) fail(type == Token::Alias ? "expected an alias name" : "expected an anchor name");
    Token token = make(type, start);
    token.value = std::move(name);
    push(std::move(token));
}

std::string Scanner::scan_tag_handle(bool directive, const Mark& start) {
    if (at() != '!') fail("expected '!' to start a tag handle", start);
    std::string handle = "!";
    forward();
    std::size_t k = 0;
    while ((at(k) >= '0' && at(k) <= '9') || (at(k) >= 'a' && at(k) <= 'z') || (at(k) >= 'A' && at(k) <= 'Z') ||
           at(k) == '-')
        ++k;
    if (at(k) == '!') {
        for (std::size_t j = 0; j <= k; ++j) {
            append_utf8(handle, at());
            forward();
        }
    } else if (directive && k > 0) {
        fail("expected '!' to end the tag handle");
    } else if (directive && !blank()) {
        fail("expected '!' to end the tag handle");
    }
    return handle;
}

std::string Scanner::scan_tag_uri(bool verbatim, bool directive, const Mark& start) {
    std::string out;
    bool first = true;
    while (true) {
        std::uint32_t c = at();
        if (c == '%') {
            std::string bytes;
            while (at() == '%') {
                auto hex = [&](std::uint32_t h) -> int {
                    if (h >= '0' && h <= '9') return static_cast<int>(h - '0');
                    if (h >= 'a' && h <= 'f') return static_cast<int>(h - 'a' + 10);
                    if (h >= 'A' && h <= 'F') return static_cast<int>(h - 'A' + 10);
                    return -1;
                };
                int hi = hex(at(1)), lo = hex(at(2));
                if (hi < 0 || lo < 0) fail("expected a URI escape %XX in a tag");
                bytes += static_cast<char>(hi * 16 + lo);
                forward(3);
            }
            try {
                std::vector<std::uint32_t> cps = decode_stream(bytes);
                for (std::uint32_t cp : cps) append_utf8(out, cp);
            } catch (const YamlError&) {
                fail("a tag's URI escapes are not UTF-8");
            }
            first = false;
            continue;
        }
        bool word = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
        bool uri = word || (c != 0 && std::string("#;/?:@&=+$,_.!~*'()[]").find(static_cast<char>(c)) !=
                                          std::string::npos && c < 0x80);
        if (!uri) break;
        bool excluded = !verbatim && !directive && (c == '!' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}');
        // A %TAG prefix that does not start with '!' starts with a tag character.
        if (directive && first && c != '!' && (c == ',' || c == '[' || c == ']' || c == '{' || c == '}')) excluded = true;
        if (excluded) break;
        append_utf8(out, c);
        forward();
        first = false;
    }
    (void)start;
    return out;
}

void Scanner::fetch_tag() {
    save_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_;
    Token token = make(Token::Tag, start);
    if (at(1) == '<') {
        forward(2);
        token.suffix = scan_tag_uri(true, false, start);
        if (at() != '>') fail("expected '>' to end a verbatim tag");
        if (token.suffix.empty()) fail("a verbatim tag is empty");
        forward();
        token.verbatim = true;
    } else if (blankz(1) || (flow_level_ && flow_indicator(1))) {
        forward();
        token.suffix = "!";
    } else {
        std::size_t k = 1;
        while (!blankz(k) && at(k) != '!' && !(flow_level_ && flow_indicator(k))) ++k;
        if (at(k) == '!') {
            token.handle = scan_tag_handle(false, start);
        } else {
            token.handle = "!";
            forward();
        }
        token.suffix = scan_tag_uri(false, false, start);
        if (token.suffix.empty()) fail("expected a tag suffix after the handle " + token.handle);
    }
    if (!blankz() && !(flow_level_ && flow_indicator())) fail("expected whitespace after a tag");
    token.end = mark_;
    push(std::move(token));
}

void Scanner::fetch_block_scalar(bool literal) {
    simple_key_allowed_ = true;
    remove_simple_key();
    Mark start = mark_;
    forward();
    int chomping = 0, increment = 0;
    auto chomp = [&]() {
        if (at() == '+' || at() == '-') {
            chomping = at() == '+' ? 1 : -1;
            forward();
            return true;
        }
        return false;
    };
    auto digit = [&]() {
        if (at() >= '1' && at() <= '9') {
            increment = static_cast<int>(at() - '0');
            forward();
            return true;
        }
        if (at() == '0') fail("a block scalar's indentation indicator cannot be 0");
        return false;
    };
    if (chomp()) digit();
    else if (digit()) chomp();
    if (!blankz()) fail("expected a chomping or indentation indicator, a comment or a line break");
    while (blank()) forward();
    if (at() == '#') {
        std::uint32_t before = text_[mark_.index - 1];
        if (before != ' ' && before != '\t') fail("a comment must be separated from what comes before it by whitespace");
        while (!line_break() && at() != 0) forward();
    }
    if (!line_break() && at() != 0) fail("expected a comment or a line break after a block scalar's header");
    if (at() == 0) {   // the stream ends with the header: an empty scalar
        Token token = make(Token::Scalar, start);
        token.style = literal ? ScalarStyle::Literal : ScalarStyle::Folded;
        push(std::move(token));
        return;
    }
    take_break();

    int min_indent = indent_ + 1;
    int indent;
    std::string breaks;
    // Empty lines up to the scalar's next line. The end of the stream ends a line as a line break
    // does, as the test suite reads it.
    auto scan_breaks = [&](int n) {
        std::string out;
        while (true) {
            while (static_cast<int>(mark_.column) < n && at() == ' ') forward();
            if (at() == '\t' && static_cast<int>(mark_.column) < n) {
                std::size_t k = 0;
                while (blank(k)) ++k;
                if (at(k) == '\r' || at(k) == '\n' || at(k) == 0)
                    if (mark_.column == 0 && n > 0) fail("a tab cannot indent a block scalar's line");
            }
            if (line_break()) {
                out += take_break();
                continue;
            }
            if (at() == 0 && mark_.column > 0) out += "\n";
            return out;
        }
    };
    if (increment) {
        indent = (indent_ >= 0 ? indent_ : 0) + increment;
        breaks = scan_breaks(indent);
    } else {
        // Auto-detected: the indentation of the first non-empty line. No leading empty line may
        // hold more spaces than it.
        int max_empty = 0;
        while (true) {
            while (at() == ' ') forward();
            if (at() == '\t' && static_cast<int>(mark_.column) < min_indent) {
                std::size_t k = 0;
                while (blank(k)) ++k;
                if ((at(k) == '\r' || at(k) == '\n' || at(k) == 0) && mark_.column == 0 && min_indent > 0)
                    fail("a tab cannot indent a block scalar's line");
            }
            if (line_break()) {
                max_empty = std::max(max_empty, static_cast<int>(mark_.column));
                breaks += take_break();
                continue;
            }
            if (at() == 0 && mark_.column > 0) {
                max_empty = std::max(max_empty, static_cast<int>(mark_.column));
                breaks += "\n";
            }
            break;
        }
        int content = static_cast<int>(mark_.column);
        bool has_content = at() != 0 && content >= min_indent && !check_document_indicator('-') &&
                           !check_document_indicator('.');
        if (has_content && max_empty > content)
            fail("a leading empty line of a block scalar has more spaces than its first line");
        if (has_content && at() == '\t' && content < min_indent) fail("a tab cannot be used for indentation");
        indent = has_content ? std::max(min_indent, content) : std::max(min_indent, max_empty);
        if (indent < 1 && !has_content) indent = std::max(indent, 0);
    }

    std::string chunks, line_break_text;
    while (static_cast<int>(mark_.column) == indent && at() != 0 && !check_document_indicator('-') &&
           !check_document_indicator('.')) {
        chunks += breaks;
        bool leading_non_space = !blank();
        while (!line_break() && at() != 0) {
            append_utf8(chunks, at());
            forward();
        }
        line_break_text = line_break() ? take_break() : "\n";
        breaks = scan_breaks(indent);
        if (static_cast<int>(mark_.column) == indent && at() != 0 && !check_document_indicator('-') &&
            !check_document_indicator('.')) {
            if (!literal && line_break_text == "\n" && leading_non_space && !blank()) {
                if (breaks.empty()) chunks += ' ';
            } else {
                chunks += line_break_text;
            }
        } else {
            break;
        }
    }
    if (chomping != -1) chunks += line_break_text;
    if (chomping == 1) chunks += breaks;
    // A line less indented than the scalar, holding only a tab before content, is a tab used as
    // indentation.
    Token token = make(Token::Scalar, start);
    token.value = std::move(chunks);
    token.style = literal ? ScalarStyle::Literal : ScalarStyle::Folded;
    push(std::move(token));
}

void Scanner::fetch_flow_scalar(bool double_quoted) {
    save_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_;
    std::uint32_t quote = at();
    forward();
    std::string out;
    // Empty lines and the leading whitespace of a continuation line; checks its indentation.
    auto scan_breaks = [&]() {
        std::string breaks;
        while (true) {
            if (check_document_indicator('-') || check_document_indicator('.'))
                fail("found a document marker inside a quoted scalar");
            std::size_t spaces = 0;
            while (at(spaces) == ' ') ++spaces;
            std::size_t k = spaces;
            while (blank(k)) ++k;
            if (at(k) == '\r' || at(k) == '\n') {
                forward(k);
                breaks += take_break();
                continue;
            }
            if (at(k) == 0) {
                forward(k);
                fail("found the end of the stream inside a quoted scalar");
            }
            if (flow_level_ == 0 && static_cast<int>(spaces) <= indent_) {
                forward(spaces);
                fail("a quoted scalar's continuation line must be indented more than its block context");
            }
            forward(k);
            return breaks;
        }
    };
    while (true) {
        while (true) {
            std::uint32_t c = at();
            if (c == 0) fail("found the end of the stream inside a quoted scalar");
            if (!double_quoted && c == '\'' && at(1) == '\'') {
                out += '\'';
                forward(2);
            } else if (c == quote) {
                break;
            } else if (double_quoted && c == '\\') {
                std::uint32_t e = at(1);
                if (e == '\r' || e == '\n') {
                    forward();
                    take_break();
                    out += scan_breaks();
                    continue;
                }
                std::uint32_t value = 0;
                int digits = 0;
                switch (e) {
                    case '0': value = 0; break;
                    case 'a': value = 7; break;
                    case 'b': value = 8; break;
                    case 't': case '\t': value = 9; break;
                    case 'n': value = 10; break;
                    case 'v': value = 11; break;
                    case 'f': value = 12; break;
                    case 'r': value = 13; break;
                    case 'e': value = 27; break;
                    case ' ': value = ' '; break;
                    case '"': value = '"'; break;
                    case '/': value = '/'; break;
                    case '\\': value = '\\'; break;
                    case 'N': value = 0x85; break;
                    case '_': value = 0xA0; break;
                    case 'L': value = 0x2028; break;
                    case 'P': value = 0x2029; break;
                    case 'x': digits = 2; break;
                    case 'u': digits = 4; break;
                    case 'U': digits = 8; break;
                    default: fail("found an unknown escape in a double-quoted scalar");
                }
                forward(2);
                if (digits) {
                    for (int k = 0; k < digits; ++k) {
                        std::uint32_t h = at();
                        int d = (h >= '0' && h <= '9') ? static_cast<int>(h - '0')
                                : (h >= 'a' && h <= 'f') ? static_cast<int>(h - 'a' + 10)
                                : (h >= 'A' && h <= 'F') ? static_cast<int>(h - 'A' + 10) : -1;
                        if (d < 0) fail("expected a hexadecimal digit in an escape");
                        value = value * 16 + static_cast<std::uint32_t>(d);
                        forward();
                    }
                    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
                        fail("an escape names no Unicode character");
                }
                append_utf8(out, value);
            } else if (blank() || line_break()) {
                break;
            } else {
                append_utf8(out, c);
                forward();
            }
        }
        if (at() == quote) {
            forward();
            break;
        }
        std::string whitespace;
        while (blank()) {
            append_utf8(whitespace, at());
            forward();
        }
        if (line_break()) {
            take_break();
            std::string breaks = scan_breaks();
            out += breaks.empty() ? std::string(" ") : breaks;
        } else {
            out += whitespace;
        }
    }
    Token token = make(Token::Scalar, start);
    token.value = std::move(out);
    token.style = double_quoted ? ScalarStyle::DoubleQuoted : ScalarStyle::SingleQuoted;
    push(std::move(token));
    adjacent_value_ok_ = true;
}

void Scanner::fetch_plain() {
    save_simple_key();
    simple_key_allowed_ = false;
    Mark start = mark_, end = mark_;
    int indent = indent_ + 1;
    std::string out, whitespace, breaks;
    bool leading_break = false;
    while (true) {
        if (check_document_indicator('-') || check_document_indicator('.')) break;
        if (at() == '#') break;
        std::string run;
        while (!blankz()) {
            std::uint32_t c = at();
            if (c == ':' && !plain_safe(1)) break;
            if (flow_level_ && flow_indicator()) break;
            append_utf8(run, c);
            forward();
        }
        if (run.empty()) break;
        simple_key_allowed_ = false;
        if (leading_break) out += breaks.empty() ? std::string(" ") : breaks;
        else out += whitespace;
        whitespace.clear();
        breaks.clear();
        leading_break = false;
        out += run;
        end = mark_;
        if (!blank() && !line_break()) break;
        bool stop = false;
        std::size_t spaces = 0;
        while (blank()) {
            append_utf8(whitespace, at());
            forward();
        }
        if (line_break()) {
            take_break();
            leading_break = true;
            simple_key_allowed_ = true;
            while (true) {
                if (check_document_indicator('-') || check_document_indicator('.')) {
                    stop = true;
                    break;
                }
                spaces = 0;
                while (at() == ' ') {
                    forward();
                    ++spaces;
                }
                while (blank()) forward();
                if (line_break()) {
                    breaks += take_break();
                    continue;
                }
                break;
            }
            if (stop) break;
            if (flow_level_ == 0 && static_cast<int>(spaces) < indent) break;
            if (flow_level_ && static_cast<int>(spaces) <= indent_ && at() != 0 && at() != '#') {
                fail("a flow collection's continuation line must be indented more than its block context");
            }
        }
    }
    Token token = make(Token::Scalar, start);
    token.end = end;
    token.value = std::move(out);
    token.style = ScalarStyle::Plain;
    push(std::move(token));
}

}  // namespace yaml
}  // namespace mira_bpe
