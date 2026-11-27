#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tex::detail {

enum class Kind { character, command, space };

struct Token {
    Kind kind;
    std::string text;
    bool is(std::string_view value) const { return kind == Kind::character && text == value; }
    bool command(std::string_view value) const { return kind == Kind::command && text == value; }
};

using Tokens = std::vector<Token>;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Tokens lex(std::string_view input);
std::string literal(const Tokens& tokens);

class Cursor {
public:
    explicit Cursor(const Tokens& tokens) : tokens_(tokens) {}
    const Token* peek() const;
    const Token& take();
    bool at(std::string_view value) const;
    bool atCommand(std::string_view value) const;
    bool match(std::string_view value);
    bool matchCommand(std::string_view value);
    void skipSpace();
    Tokens group();
    Tokens optional();
    Tokens argument();

private:
    Tokens delimited(std::string_view open, std::string_view close);
    const Tokens& tokens_;
    std::size_t next_ = 0;
};

}
