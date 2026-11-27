#include "tex.hpp"
#include "lexer.hpp"
#include "macros.hpp"
#include "mathml.hpp"
#include "parser.hpp"

namespace tex {

Result render(std::string_view input, bool display) {
    if (input.size() > 65536) return {"", "Formula exceeds 64 KiB"};
    try {
        auto tokens = detail::expand(detail::lex(input));
        return {detail::mathDocument(detail::Parser(tokens).complete(), input, display), ""};
    } catch (const detail::Error& error) {
        return {"", error.what()};
    }
}

}
