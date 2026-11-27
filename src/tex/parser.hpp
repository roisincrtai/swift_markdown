#pragma once

#include "lexer.hpp"

namespace tex::detail {

struct Atom {
    std::string markup;
    bool limits = false;
};

class Parser {
public:
    Parser(const Tokens& tokens, unsigned depth = 0, std::string_view variant = {});
    std::string complete();

private:
    class Nesting {
    public:
        explicit Nesting(unsigned& value);
        ~Nesting() { --value_; }
    private:
        unsigned& value_;
    };

    std::string expression(const Tokens& tokens);
    bool boundary() const;
    std::string sequence(bool acceptsFraction = true);
    std::string argument(std::string_view variant);
    std::string argument() { return argument(variant_); }
    Atom primary(bool combineDigits = true);
    Atom command(std::string_view name);
    std::string scripted(Atom base);
    std::string identifier(std::string_view value, bool upright = false) const;

    std::string readDelimiter(bool group = false);
    std::string scalableFence();
    std::string fraction(std::string_view name);
    std::string text(std::string_view name);
    std::string decoration(std::string_view name);
    std::string styling(std::string_view name);
    std::string spacing(std::string_view name);
    std::string environment(const std::string& name);
    std::string table(const std::vector<std::string_view>& alignment, std::string_view lines = {}, bool repeating = false);

    Cursor cursor_;
    unsigned depth_;
    std::string_view variant_;
};

std::string row(std::string_view content);
std::string op(std::string_view value, std::string_view attributes = {});
std::string fence(std::string_view glyph, std::string_view form);
std::string space(std::string_view width);
std::string_view style(std::string_view mode);
bool digit(const Token* token);

}
