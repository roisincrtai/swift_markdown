// yaml_scanner.hpp -- characters to tokens: the first half of yaml.hpp's reader.
//
// The text is decoded to code points once (decode_stream), and the scanner reads them with one
// character of look-ahead beyond what each token needs. Like libyaml's, it keeps the indentation
// stack of the block collections and emits BlockSequenceStart, BlockMappingStart and BlockEnd
// itself, and it remembers at most one possible simple key per flow level: a scalar, alias, tag,
// anchor or flow collection that may turn out to be a key when a ':' follows on the same line, in
// which case a Key token (and, in block context, a BlockMappingStart) is inserted before it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "yaml.hpp"

namespace common::yaml {

// The code points of a UTF-8, UTF-16 or UTF-32 stream; throws YamlError on a malformed encoding or a
// character that is not c-printable.
std::vector<std::uint32_t> decode_stream(std::string_view bytes);
void append_utf8(std::string& out, std::uint32_t cp);

struct Token {
    enum Type {
        StreamStart, StreamEnd, VersionDirective, TagDirective, DocumentStart, DocumentEnd,
        BlockSequenceStart, BlockMappingStart, BlockEnd, FlowSequenceStart, FlowMappingStart,
        FlowSequenceEnd, FlowMappingEnd, BlockEntry, FlowEntry, Key, Value, Alias, Anchor, Tag, Scalar
    } type = StreamStart;
    Mark start, end;
    std::string value;           // Alias, Anchor: the name; Scalar: the value; TagDirective: prefix
    std::string handle, suffix;  // Tag: handle (empty for verbatim) and suffix; TagDirective: handle
    bool verbatim = false;       // Tag: !<...>
    int major = 0, minor = 0;    // VersionDirective
    ScalarStyle style = ScalarStyle::Plain;
};

const char* token_name(Token::Type type);

class Scanner {
public:
    explicit Scanner(std::string_view text);
    // The next token, without consuming it.
    const Token& peek();
    Token next();
    bool check(Token::Type type) { return peek().type == type; }

private:
    struct SimpleKey {
        bool possible = false, required = false;
        bool after_tab = false;   // separated from what came before it by a tab
        std::size_t token_number = 0;
        Mark mark;
    };

    std::vector<std::uint32_t> text_;
    Mark mark_;
    std::deque<Token> tokens_;
    std::size_t tokens_taken_ = 0;
    bool stream_start_produced_ = false, stream_end_produced_ = false;
    int indent_ = -1;
    std::vector<int> indents_;
    bool simple_key_allowed_ = false;
    std::vector<SimpleKey> simple_keys_;   // one per flow level, [0] for the block context
    std::vector<bool> flow_mapping_;       // per flow level: a mapping (true) or a sequence
    int flow_level_ = 0;
    // Whether the whitespace just before the current token, on its line, holds a tab: a tab may
    // separate, but not indent, so no block collection may start after one.
    bool tab_before_ = false;
    // Where the last token ended, to tell whether whitespace separates it from a '#' that follows.
    bool whitespace_before_ = true;
    // The line of a ':' in block context, after which no block collection may start on that line.
    std::size_t value_line_ = static_cast<std::size_t>(-1);
    // Whether the last token can be a JSON-like key (a quoted scalar or a flow collection's end),
    // after which ':' is a value indicator in flow context even without a space after it.
    bool adjacent_value_ok_ = false;

    std::uint32_t at(std::size_t k = 0) const {
        std::size_t i = mark_.index + k;
        return i < text_.size() ? text_[i] : 0;
    }
    bool blank(std::size_t k = 0) const { return at(k) == ' ' || at(k) == '\t'; }
    bool line_break(std::size_t k = 0) const { return at(k) == '\r' || at(k) == '\n'; }
    bool blankz(std::size_t k = 0) const { return blank(k) || line_break(k) || at(k) == 0; }
    bool flow_indicator(std::size_t k = 0) const {
        std::uint32_t c = at(k);
        return c == ',' || c == '[' || c == ']' || c == '{' || c == '}';
    }
    void forward(std::size_t n = 1);
    std::string take_break();
    [[noreturn]] void fail(const std::string& message) const;
    [[noreturn]] void fail(const std::string& message, const Mark& mark) const;

    bool need_more_tokens();
    void fetch_more_tokens();
    void scan_to_next_token();
    void stale_simple_keys();
    void save_simple_key();
    void remove_simple_key();
    void unwind_indent(int column);
    bool add_indent(int column);
    void push(Token token);
    Token make(Token::Type type, const Mark& start) const;

    void fetch_stream_end();
    void fetch_directive();
    void fetch_document_indicator(Token::Type type);
    void fetch_flow_collection_start(Token::Type type);
    void fetch_flow_collection_end(Token::Type type);
    void fetch_flow_entry();
    void fetch_block_entry();
    void fetch_key();
    void fetch_value();
    void fetch_anchor(Token::Type type);
    void fetch_tag();
    void fetch_block_scalar(bool literal);
    void fetch_flow_scalar(bool double_quoted);
    void fetch_plain();

    bool check_document_indicator(char c) const;
    bool check_plain() const;
    bool plain_safe(std::size_t k) const;
    void skip_comment_and_break(const char* where);
    std::string scan_tag_uri(bool verbatim, bool directive, const Mark& start);
    std::string scan_tag_handle(bool directive, const Mark& start);
    void scan_plain_spaces(std::string& whitespace, std::string& breaks, bool* leading_break, int indent);
};

}  // namespace common::yaml
