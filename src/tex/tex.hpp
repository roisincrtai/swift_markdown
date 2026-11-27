#pragma once

#include <string>
#include <string_view>

namespace tex {

struct Result {
    std::string mathml;
    std::string error;
};

// Compiles a math expression to self-contained MathML, or returns a diagnostic.
Result render(std::string_view input, bool display = false);

}
