#include "lexer.hpp"

namespace tex::detail {
namespace {

bool letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

std::size_t characterSize(std::string_view input, std::size_t i) {
    const auto c = static_cast<unsigned char>(input[i]);
    std::size_t size = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    if (i + size > input.size() || (c >= 0x80 && c < 0xC2) || c > 0xF4) {
        throw Error("Invalid UTF-8 in formula");
    }
    if (size >= 3) {
        const auto second = static_cast<unsigned char>(input[i + 1]);
        if ((c == 0xE0 && second < 0xA0) || (c == 0xED && second >= 0xA0) ||
            (c == 0xF0 && second < 0x90) || (c == 0xF4 && second >= 0x90)) {
            throw Error("Invalid UTF-8 in formula");
        }
    }
    for (std::size_t j = 1; j < size; ++j) {
        if ((static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
            throw Error("Invalid UTF-8 in formula");
        }
    }
    return size;
}

}

Tokens lex(std::string_view input) {
    Tokens tokens;
    for (std::size_t i = 0; i < input.size();) {
        char c = input[i];
        if (c == '%') {
            while (i < input.size() && input[i] != '\n') ++i;
            if (i < input.size()) ++i;
        } else if (c == '\\') {
            if (++i == input.size()) throw Error("Incomplete TeX command");
            std::size_t start = i;
            if (letter(input[i])) {
                while (i < input.size() && letter(input[i])) ++i;
                tokens.push_back({Kind::command, std::string(input.substr(start, i - start))});
                while (i < input.size() && space(input[i])) ++i;
            } else {
                i += characterSize(input, i);
                tokens.push_back({Kind::command,
                                  space(input[start]) ? " " : std::string(input.substr(start, i - start))});
            }
        } else if (space(c)) {
            while (i < input.size() && space(input[i])) ++i;
            tokens.push_back({Kind::space, " "});
        } else {
            auto size = characterSize(input, i);
            tokens.push_back({Kind::character, std::string(input.substr(i, size))});
            i += size;
        }
    }
    return tokens;
}

std::string literal(const Tokens& tokens) {
    std::string result;
    for (const auto& token : tokens) {
        if (token.kind == Kind::command && token.text.size() > 1) result += '\\';
        result += token.text;
    }
    return result;
}

const Token* Cursor::peek() const {
    return next_ == tokens_.size() ? nullptr : &tokens_[next_];
}

const Token& Cursor::take() {
    if (!peek()) throw Error("Unexpected end of formula");
    return tokens_[next_++];
}

bool Cursor::at(std::string_view value) const { return peek() && peek()->is(value); }
bool Cursor::atCommand(std::string_view value) const { return peek() && peek()->command(value); }

bool Cursor::match(std::string_view value) {
    if (!at(value)) return false;
    take();
    return true;
}

bool Cursor::matchCommand(std::string_view value) {
    if (!atCommand(value)) return false;
    take();
    return true;
}

void Cursor::skipSpace() {
    while (peek() && peek()->kind == Kind::space) take();
}

Tokens Cursor::delimited(std::string_view open, std::string_view close) {
    if (!match(open)) throw Error("Expected " + std::string(open));
    Tokens result;
    unsigned depth = 1;
    unsigned braces = 0;
    while (peek()) {
        const auto& token = take();
        if (open == "[") {
            if (token.is("{")) ++braces;
            if (token.is("}") && braces) --braces;
        }
        if (!braces && token.is(open)) ++depth;
        if (!braces && token.is(close) && --depth == 0) return result;
        result.push_back(token);
    }
    throw Error("Missing " + std::string(close));
}

Tokens Cursor::group() {
    skipSpace();
    return delimited("{", "}");
}

Tokens Cursor::optional() {
    skipSpace();
    return at("[") ? delimited("[", "]") : Tokens{};
}

Tokens Cursor::argument() {
    skipSpace();
    if (at("{")) return group();
    if (at("}") || at("&")) throw Error("Missing command argument");
    return {take()};
}

}
