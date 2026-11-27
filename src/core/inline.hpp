#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace markdown::detail {

std::string escape(std::string_view value);
std::string_view trim(std::string_view value);
bool escaped(std::string_view value, std::size_t position);
std::string slug(std::string_view text);
std::string referenceKey(std::string_view text);
bool white(char ch);
bool digit(char ch);
std::size_t run(std::string_view text, std::size_t start, char marker);

using References = std::unordered_map<std::string, std::string>;

struct Formula {
    std::string_view source;
    std::size_t end;
    bool display;
};

std::optional<Formula> formulaAt(std::string_view text, std::size_t start);

class Inline {
public:
    void setReferences(References references);
    std::string render(std::string_view text, bool plain = false, unsigned depth = 0);
    std::string math(std::string_view formula, bool display);

private:
    std::optional<std::string_view> reference(std::string_view key) const;
    References references_;
    std::unordered_map<std::string, std::string> inlineCache_;
    std::unordered_map<std::string, std::string> mathCache_;
};

}
