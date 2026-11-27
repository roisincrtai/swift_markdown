// yaml_parser.cpp -- tokens to events: the second half of yaml.hpp's reader.
//
// The states and their order follow libyaml's parser (and PyYAML's): an explicit stack of states
// rather than recursion, one event per step.
#include <map>

#include "yaml_scanner.hpp"

namespace common::yaml {

namespace {

class Parser {
public:
    Parser(std::string_view text, const Options& options) : scanner_(text), options_(options) {}

    std::vector<Event> run() {
        std::vector<Event> events;
        state_ = StreamStart;
        while (state_ != Done) events.push_back(step());
        return events;
    }

private:
    enum State {
        StreamStart, ImplicitDocumentStart, DocumentStart, DocumentContent, DocumentEnd, BlockNode,
        BlockSequenceFirstEntry, BlockSequenceEntry, IndentlessSequenceEntry, BlockMappingFirstKey,
        BlockMappingKey, BlockMappingValue, FlowSequenceFirstEntry, FlowSequenceEntry,
        FlowSequenceEntryMappingKey, FlowSequenceEntryMappingValue, FlowSequenceEntryMappingEnd,
        FlowSequenceEntryMappingEmptyKey, FlowMappingFirstKey, FlowMappingKey, FlowMappingValue,
        FlowMappingEmptyValue, Done
    };

    Scanner scanner_;
    Options options_;
    State state_ = StreamStart;
    std::vector<State> states_;
    std::map<std::string, std::string> handles_;
    bool document_ended_explicitly_ = true;
    bool directives_seen_ = false;

    [[noreturn]] void fail(const std::string& message, const Mark& mark) { throw YamlError(message, mark); }

    void push_state(State state) {
        if (states_.size() >= options_.max_depth) fail("the document is nested too deeply", scanner_.peek().start);
        states_.push_back(state);
    }
    State pop_state() {
        State state = states_.back();
        states_.pop_back();
        return state;
    }

    Event empty_scalar(const Mark& mark) {
        Event event;
        event.type = Event::Scalar;
        event.start = event.end = mark;
        event.style = ScalarStyle::Plain;
        return event;
    }

    Event simple(Event::Type type, const Mark& start, const Mark& end) {
        Event event;
        event.type = type;
        event.start = start;
        event.end = end;
        return event;
    }

    Event step() {
        switch (state_) {
            case StreamStart: {
                Token token = scanner_.next();
                state_ = ImplicitDocumentStart;
                return simple(Event::StreamStart, token.start, token.end);
            }
            case ImplicitDocumentStart: return document_start(true);
            case DocumentStart: return document_start(false);
            case DocumentContent: {
                Token::Type t = scanner_.peek().type;
                if (t == Token::VersionDirective || t == Token::TagDirective || t == Token::DocumentStart ||
                    t == Token::DocumentEnd || t == Token::StreamEnd) {
                    state_ = pop_state();
                    return empty_scalar(scanner_.peek().start);
                }
                return node(true, false);
            }
            case DocumentEnd: {
                const Token& token = scanner_.peek();
                Event event = simple(Event::DocumentEnd, token.start, token.start);
                if (token.type == Token::DocumentEnd) {
                    Token end = scanner_.next();
                    event.end = end.end;
                    event.explicit_marker = true;
                    document_ended_explicitly_ = true;
                } else {
                    if (token.type != Token::StreamEnd && token.type != Token::DocumentStart &&
                        token.type != Token::VersionDirective && token.type != Token::TagDirective)
                        fail(std::string("expected the end of the document, but found ") + token_name(token.type),
                             token.start);
                    document_ended_explicitly_ = false;
                }
                state_ = DocumentStart;
                return event;
            }
            case BlockNode: return node(true, false);
            case BlockSequenceFirstEntry:
                scanner_.next();
                [[fallthrough]];
            case BlockSequenceEntry: {
                if (scanner_.check(Token::BlockEntry)) {
                    Token token = scanner_.next();
                    if (!scanner_.check(Token::BlockEntry) && !scanner_.check(Token::BlockEnd)) {
                        push_state(BlockSequenceEntry);
                        return node(true, false);
                    }
                    state_ = BlockSequenceEntry;
                    return empty_scalar(token.end);
                }
                const Token& token = scanner_.peek();
                if (token.type != Token::BlockEnd)
                    fail(std::string("expected a block sequence entry, but found ") + token_name(token.type),
                         token.start);
                Token end = scanner_.next();
                state_ = pop_state();
                return simple(Event::SequenceEnd, end.start, end.end);
            }
            case IndentlessSequenceEntry: {
                if (scanner_.check(Token::BlockEntry)) {
                    Token token = scanner_.next();
                    Token::Type t = scanner_.peek().type;
                    if (t != Token::BlockEntry && t != Token::Key && t != Token::Value && t != Token::BlockEnd) {
                        push_state(IndentlessSequenceEntry);
                        return node(true, false);
                    }
                    state_ = IndentlessSequenceEntry;
                    return empty_scalar(token.end);
                }
                state_ = pop_state();
                const Token& token = scanner_.peek();
                return simple(Event::SequenceEnd, token.start, token.start);
            }
            case BlockMappingFirstKey:
                scanner_.next();
                [[fallthrough]];
            case BlockMappingKey: {
                if (scanner_.check(Token::Key)) {
                    Token token = scanner_.next();
                    Token::Type t = scanner_.peek().type;
                    if (t != Token::Key && t != Token::Value && t != Token::BlockEnd) {
                        push_state(BlockMappingValue);
                        return node(true, true);
                    }
                    state_ = BlockMappingValue;
                    return empty_scalar(token.end);
                }
                if (scanner_.check(Token::Value)) {
                    state_ = BlockMappingValue;
                    return empty_scalar(scanner_.peek().start);
                }
                const Token& token = scanner_.peek();
                if (token.type != Token::BlockEnd)
                    fail(std::string("expected a mapping key, but found ") + token_name(token.type), token.start);
                Token end = scanner_.next();
                state_ = pop_state();
                return simple(Event::MappingEnd, end.start, end.end);
            }
            case BlockMappingValue: {
                if (scanner_.check(Token::Value)) {
                    Token token = scanner_.next();
                    Token::Type t = scanner_.peek().type;
                    if (t != Token::Key && t != Token::Value && t != Token::BlockEnd) {
                        push_state(BlockMappingKey);
                        return node(true, true);
                    }
                    state_ = BlockMappingKey;
                    return empty_scalar(token.end);
                }
                state_ = BlockMappingKey;
                return empty_scalar(scanner_.peek().start);
            }
            case FlowSequenceFirstEntry:
                scanner_.next();
                return flow_sequence_entry(true);
            case FlowSequenceEntry: return flow_sequence_entry(false);
            case FlowSequenceEntryMappingKey: {
                Token token = scanner_.next();
                Token::Type t = scanner_.peek().type;
                if (t != Token::Value && t != Token::FlowEntry && t != Token::FlowSequenceEnd) {
                    push_state(FlowSequenceEntryMappingValue);
                    return node(false, false);
                }
                state_ = FlowSequenceEntryMappingValue;
                return empty_scalar(token.end);
            }
            case FlowSequenceEntryMappingEmptyKey:
                state_ = FlowSequenceEntryMappingValue;
                return empty_scalar(scanner_.peek().start);
            case FlowSequenceEntryMappingValue: {
                if (scanner_.check(Token::Value)) {
                    Token token = scanner_.next();
                    Token::Type t = scanner_.peek().type;
                    if (t != Token::FlowEntry && t != Token::FlowSequenceEnd) {
                        push_state(FlowSequenceEntryMappingEnd);
                        return node(false, false);
                    }
                    state_ = FlowSequenceEntryMappingEnd;
                    return empty_scalar(token.end);
                }
                state_ = FlowSequenceEntryMappingEnd;
                return empty_scalar(scanner_.peek().start);
            }
            case FlowSequenceEntryMappingEnd: {
                state_ = FlowSequenceEntry;
                const Token& token = scanner_.peek();
                return simple(Event::MappingEnd, token.start, token.start);
            }
            case FlowMappingFirstKey:
                scanner_.next();
                return flow_mapping_key(true);
            case FlowMappingKey: return flow_mapping_key(false);
            case FlowMappingValue: {
                if (scanner_.check(Token::Value)) {
                    Token token = scanner_.next();
                    Token::Type t = scanner_.peek().type;
                    if (t != Token::FlowEntry && t != Token::FlowMappingEnd) {
                        push_state(FlowMappingKey);
                        return node(false, false);
                    }
                    state_ = FlowMappingKey;
                    return empty_scalar(token.end);
                }
                state_ = FlowMappingKey;
                return empty_scalar(scanner_.peek().start);
            }
            case FlowMappingEmptyValue:
                state_ = FlowMappingKey;
                return empty_scalar(scanner_.peek().start);
            case Done: break;
        }
        fail("internal error: no state", scanner_.peek().start);
    }

    Event document_start(bool implicit) {
        bool skipped = false;
        while (scanner_.check(Token::DocumentEnd)) {
            scanner_.next();
            document_ended_explicitly_ = true;
            skipped = true;
        }
        // A bare document may open the stream, or follow a document that '...' ended.
        if (!implicit && document_ended_explicitly_) implicit = true;
        (void)skipped;
        if (implicit) {
            Token::Type t = scanner_.peek().type;
            if (t != Token::VersionDirective && t != Token::TagDirective && t != Token::DocumentStart &&
                t != Token::StreamEnd) {
                handles_ = {{"!", "!"}, {"!!", "tag:yaml.org,2002:"}};
                const Token& token = scanner_.peek();
                push_state(DocumentEnd);
                state_ = BlockNode;
                return simple(Event::DocumentStart, token.start, token.start);
            }
        }
        if (scanner_.check(Token::StreamEnd)) {
            Token token = scanner_.next();
            state_ = Done;
            return simple(Event::StreamEnd, token.start, token.end);
        }
        Mark start = scanner_.peek().start;
        if (!implicit && !document_ended_explicitly_ &&
            (scanner_.check(Token::VersionDirective) || scanner_.check(Token::TagDirective)))
            fail("directives after a document need '...' to end it first", start);
        process_directives();
        const Token& token = scanner_.peek();
        if (token.type != Token::DocumentStart)
            fail(std::string("expected '---' to start the document, but found ") + token_name(token.type), token.start);
        Token marker = scanner_.next();
        push_state(DocumentEnd);
        state_ = DocumentContent;
        Event event = simple(Event::DocumentStart, start, marker.end);
        event.explicit_marker = true;
        return event;
    }

    void process_directives() {
        handles_.clear();
        bool version = false;
        while (scanner_.check(Token::VersionDirective) || scanner_.check(Token::TagDirective)) {
            Token token = scanner_.next();
            if (token.type == Token::VersionDirective) {
                if (version) fail("found a second %YAML directive", token.start);
                if (token.major != 1) fail("found an incompatible YAML version", token.start);
                version = true;
            } else {
                if (handles_.count(token.handle)) fail("found a second %TAG directive for " + token.handle, token.start);
                handles_[token.handle] = token.value;
            }
        }
        handles_.emplace("!", "!");
        handles_.emplace("!!", "tag:yaml.org,2002:");
    }

    Event node(bool block, bool indentless_sequence) {
        const Token& first = scanner_.peek();
        if (first.type == Token::Alias) {
            Token token = scanner_.next();
            Event event = simple(Event::Alias, token.start, token.end);
            event.anchor = token.value;
            state_ = pop_state();
            return event;
        }
        std::string anchor, tag;
        bool tagged = false;
        Mark start = first.start, end = first.start, tag_mark;
        auto read_tag = [&]() {
            Token token = scanner_.next();
            tag_mark = token.start;
            end = token.end;
            tagged = true;
            if (token.verbatim) {
                tag = token.suffix;
            } else if (token.handle.empty()) {
                tag = token.suffix;   // "!", the non-specific tag
            } else {
                auto found = handles_.find(token.handle);
                if (found == handles_.end()) fail("found an undefined tag handle " + token.handle, token.start);
                tag = found->second + token.suffix;
            }
        };
        if (scanner_.check(Token::Anchor)) {
            Token token = scanner_.next();
            anchor = token.value;
            end = token.end;
            if (scanner_.check(Token::Tag)) read_tag();
        } else if (scanner_.check(Token::Tag)) {
            read_tag();
            if (scanner_.check(Token::Anchor)) {
                Token token = scanner_.next();
                anchor = token.value;
                end = token.end;
            }
        }
        if (scanner_.check(Token::Anchor) || scanner_.check(Token::Tag))
            fail("a node can have at most one anchor and one tag", scanner_.peek().start);
        const Token& token = scanner_.peek();
        Event event;
        event.anchor = anchor;
        event.tag = tag;
        event.start = start;
        if (indentless_sequence && token.type == Token::BlockEntry) {
            event.type = Event::SequenceStart;
            event.end = token.end;
            state_ = IndentlessSequenceEntry;
            return event;
        }
        if (token.type == Token::Scalar) {
            Token scalar = scanner_.next();
            event.type = Event::Scalar;
            event.value = std::move(scalar.value);
            event.style = scalar.style;
            event.end = scalar.end;
            state_ = pop_state();
            return event;
        }
        if (token.type == Token::FlowSequenceStart) {
            event.type = Event::SequenceStart;
            event.flow = true;
            event.end = token.end;
            state_ = FlowSequenceFirstEntry;
            return event;
        }
        if (token.type == Token::FlowMappingStart) {
            event.type = Event::MappingStart;
            event.flow = true;
            event.end = token.end;
            state_ = FlowMappingFirstKey;
            return event;
        }
        if (block && token.type == Token::BlockSequenceStart) {
            event.type = Event::SequenceStart;
            event.end = token.end;
            state_ = BlockSequenceFirstEntry;
            return event;
        }
        if (block && token.type == Token::BlockMappingStart) {
            event.type = Event::MappingStart;
            event.end = token.end;
            state_ = BlockMappingFirstKey;
            return event;
        }
        if (!anchor.empty() || tagged) {
            event.type = Event::Scalar;
            event.end = end;
            state_ = pop_state();
            return event;
        }
        fail(std::string(block ? "expected a block node, but found " : "expected a flow node, but found ") +
                 token_name(token.type),
             token.start);
    }

    Event flow_sequence_entry(bool first) {
        if (!scanner_.check(Token::FlowSequenceEnd)) {
            if (!first) {
                if (scanner_.check(Token::FlowEntry)) scanner_.next();
                else fail(std::string("expected ',' or ']', but found ") + token_name(scanner_.peek().type),
                          scanner_.peek().start);
            }
            const Token& token = scanner_.peek();
            if (token.type == Token::Key) {
                Event event = simple(Event::MappingStart, token.start, token.end);
                event.flow = true;
                state_ = FlowSequenceEntryMappingKey;
                return event;
            }
            if (token.type == Token::Value) {
                Event event = simple(Event::MappingStart, token.start, token.end);
                event.flow = true;
                state_ = FlowSequenceEntryMappingEmptyKey;
                return event;
            }
            if (token.type != Token::FlowSequenceEnd) {
                push_state(FlowSequenceEntry);
                return node(false, false);
            }
        }
        Token end = scanner_.next();
        state_ = pop_state();
        return simple(Event::SequenceEnd, end.start, end.end);
    }

    Event flow_mapping_key(bool first) {
        if (!scanner_.check(Token::FlowMappingEnd)) {
            if (!first) {
                if (scanner_.check(Token::FlowEntry)) scanner_.next();
                else fail(std::string("expected ',' or '}', but found ") + token_name(scanner_.peek().type),
                          scanner_.peek().start);
            }
            if (scanner_.check(Token::Key)) {
                Token token = scanner_.next();
                Token::Type t = scanner_.peek().type;
                if (t != Token::Value && t != Token::FlowEntry && t != Token::FlowMappingEnd) {
                    push_state(FlowMappingValue);
                    return node(false, false);
                }
                state_ = FlowMappingValue;
                return empty_scalar(token.end);
            }
            if (scanner_.check(Token::Value)) {
                state_ = FlowMappingValue;
                return empty_scalar(scanner_.peek().start);
            }
            if (!scanner_.check(Token::FlowMappingEnd)) {
                push_state(FlowMappingEmptyValue);
                return node(false, false);
            }
        }
        Token end = scanner_.next();
        state_ = pop_state();
        return simple(Event::MappingEnd, end.start, end.end);
    }
};

}  // namespace

std::vector<Event> parse_events(std::string_view text, const Options& options) {
    return Parser(text, options).run();
}

}  // namespace common::yaml
