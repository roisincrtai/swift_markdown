// tokenizer/cpp/yaml.hpp -- a YAML 1.2 reader (yaml.org/spec/1.2.2), standard C++17 only.
//
// Three layers, each usable on its own:
//   - parse_events: the event stream of the text (stream, document, mapping, sequence, scalar and
//     alias events with their anchors, tags and styles), the representation the YAML test suite
//     (github.com/yaml/yaml-test-suite) checks; events_text writes it in the suite's format;
//   - load_yaml / load_yaml_all: the node graph of each document, anchors and aliases resolved
//     (an alias is a pointer to the node its anchor names), tags resolved through the %TAG
//     directives to full tag URIs;
//   - to_json / parse_yaml / parse_yaml_all: each document as a Json (json.hpp) under the YAML 1.2
//     core schema: null (~, null, Null, NULL, empty), bool (true/True/TRUE, false/False/FALSE), int
//     (decimal, 0o octal, 0x hexadecimal), float (decimal with fraction or exponent, .inf, .nan),
//     everything else a string; the tags !!null !!bool !!int !!float !!str !!seq !!map are honoured
//     and an unknown tag keeps the node as its kind gives it. A mapping key that is not a string
//     is written as its scalar text; a collection as a key, or an alias cycle, is an error.
//
// The input is UTF-8, UTF-16 or UTF-32 (by byte order mark, or by the pattern of zero bytes YAML
// 1.2 section 5.2 describes). Line breaks are CR, LF and CRLF only (YAML 1.2); every character must
// be printable (c-printable). Errors throw YamlError with the line and column (from 1) of the
// character where the text stops being YAML.
//
// The design follows libyaml (and PyYAML, its port): a scanner that turns characters into tokens,
// tracking indentation and the "simple keys" that are only known to be keys when their ':' comes,
// and a parser that turns the tokens into events by an explicit state stack, so no input can
// overflow the call stack; nesting deeper than max_depth is an error.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace mira_bpe {
namespace yaml {

struct Mark {
    std::size_t index = 0, line = 0, column = 0;   // code points; line and column from 0
};

struct YamlError : std::runtime_error {
    YamlError(const std::string& what, const Mark& where);
    Mark mark;
};

enum class ScalarStyle { Plain, SingleQuoted, DoubleQuoted, Literal, Folded };

struct Event {
    enum Type {
        StreamStart, StreamEnd, DocumentStart, DocumentEnd, Alias, Scalar,
        SequenceStart, SequenceEnd, MappingStart, MappingEnd
    } type = StreamStart;
    Mark start, end;
    std::string anchor;          // Alias: the anchor it names; nodes: their anchor, or empty
    std::string tag;             // the full tag, or empty when the node has none ("!" stays "!")
    std::string value;           // Scalar: its value, UTF-8
    ScalarStyle style = ScalarStyle::Plain;
    bool flow = false;           // SequenceStart, MappingStart: flow style
    bool explicit_marker = false;// DocumentStart: "---" was written; DocumentEnd: "..." was
};

struct Options {
    std::size_t max_depth = 10000;   // collections open at once
};

std::vector<Event> parse_events(std::string_view text, const Options& options = Options());
// The events in the test suite's tree format (test.event), one per line.
std::string events_text(const std::vector<Event>& events);

struct Node {
    enum Kind { Scalar, Sequence, Mapping } kind = Scalar;
    std::string tag;                        // full tag, or empty
    std::string anchor;
    std::string value;                      // Scalar
    ScalarStyle style = ScalarStyle::Plain;
    bool flow = false;
    std::vector<const Node*> items;         // Sequence items; Mapping keys and values alternating
    Mark start;
};

// The documents of a stream; the nodes live as long as the Document.
struct Document {
    const Node* root = nullptr;
    std::vector<std::unique_ptr<Node>> nodes;
};

std::vector<Document> load_yaml_all(std::string_view text, const Options& options = Options());
// The one document of `text`; an error when there is none or more than one.
Document load_yaml(std::string_view text, const Options& options = Options());

Json to_json(const Node& node);
Json parse_yaml(std::string_view text, const Options& options = Options());
std::vector<Json> parse_yaml_all(std::string_view text, const Options& options = Options());

}  // namespace yaml
}  // namespace mira_bpe
