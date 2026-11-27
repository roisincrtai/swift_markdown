#pragma once

#include <optional>
#include <string_view>

namespace tex::detail {

enum class Role { identifier, constant, oper, large, limits, function, delimiter };

struct Symbol {
    std::string_view glyph;
    Role role;
};

std::optional<Symbol> symbol(std::string_view name);

}
