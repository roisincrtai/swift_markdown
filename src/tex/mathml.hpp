#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace tex::detail {

std::string escape(std::string_view value);
std::string element(std::string_view name, std::string_view content, std::string_view attributes = {});
std::string element(std::string_view name, std::initializer_list<std::string_view> children,
                    std::string_view attributes = {});
std::string mathDocument(std::string_view body, std::string_view source, bool display);
std::string styledIdentifier(std::string_view value, std::string_view variant);

}
