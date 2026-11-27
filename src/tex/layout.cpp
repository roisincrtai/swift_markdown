#include "parser.hpp"
#include "mathml.hpp"
#include "symbols.hpp"

#include <algorithm>
#include <unordered_map>

namespace tex::detail {
namespace {

std::string dimension(std::string value) {
    if (value == "0") return "0";
    auto unit = value.find_first_not_of("0123456789.+-");
    if (unit == value.npos || unit == 0 || unit > 10) throw Error("Invalid math dimension");
    auto number = value.substr(0, unit);
    if (number.find_first_of("0123456789") == number.npos ||
        number.find_first_of("+-", 1) != number.npos || std::count(number.begin(), number.end(), '.') > 1) {
        throw Error("Invalid math dimension");
    }
    auto suffix = value.substr(unit);
    if (suffix == "mu") return std::to_string(std::stod(number) / 18) + "em";
    if (suffix != "px" && suffix != "em" && suffix != "ex" && suffix != "pt" &&
        suffix != "mm" && suffix != "cm" && suffix != "in" && suffix != "%") {
        throw Error("Unsupported math dimension unit");
    }
    return value;
}

bool validColor(std::string_view color) {
    if (color.empty() || color.size() > 32) return false;
    if (color.front() != '#') {
        return color.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == color.npos;
    }
    return (color.size() == 4 || color.size() == 7) &&
           color.find_first_not_of("0123456789abcdefABCDEF", 1) == color.npos;
}

std::string delimiter(const Token& token) {
    if (token.is(".")) return "";
    if (token.kind == Kind::command) {
        if (auto entry = symbol(token.text); entry &&
            (entry->role == Role::delimiter || token.text == "mid" || token.text == "parallel")) {
            return std::string(entry->glyph);
        }
    } else {
        if (token.is("<")) return "⟨";
        if (token.is(">")) return "⟩";
        if (std::string_view("()[]/|‖⟨⟩").find(token.text) != std::string_view::npos) return token.text;
    }
    throw Error("Invalid math delimiter");
}

struct Accent {
    std::string_view glyph;
    bool under = false;
    bool stretch = false;
    bool brace = false;
};

}

std::string_view style(std::string_view mode) {
    if (mode == "displaystyle" || mode == "0") return "displaystyle=\"true\" scriptlevel=\"0\"";
    if (mode == "textstyle" || mode == "1") return "displaystyle=\"false\" scriptlevel=\"0\"";
    if (mode == "scriptstyle" || mode == "2") return "displaystyle=\"false\" scriptlevel=\"1\"";
    if (mode == "scriptscriptstyle" || mode == "3") return "displaystyle=\"false\" scriptlevel=\"2\"";
    return "";
}

std::string Parser::readDelimiter(bool group) {
    cursor_.skipSpace();
    if (group && cursor_.at("{")) {
        auto tokens = cursor_.group();
        Cursor contents(tokens);
        contents.skipSpace();
        if (!contents.peek()) return "";
        auto glyph = delimiter(contents.take());
        contents.skipSpace();
        if (contents.peek()) throw Error("Expected a single delimiter");
        return glyph;
    }
    return delimiter(cursor_.take());
}

std::string Parser::scalableFence() {
    auto contents = fence(readDelimiter(), "prefix");
    contents += sequence();
    while (cursor_.matchCommand("middle")) {
        contents += fence(readDelimiter(), "infix");
        contents += sequence();
    }
    if (!cursor_.matchCommand("right")) throw Error("Missing \\right");
    contents += fence(readDelimiter(), "postfix");
    return row(contents);
}

std::string Parser::fraction(std::string_view name) {
    std::string left;
    std::string right;
    std::string thickness;
    std::string_view attrs;
    if (name.find("binom") != name.npos) {
        left = "(";
        right = ")";
        thickness = "0";
    }
    if (name == "dfrac" || name == "dbinom" || name == "cfrac") attrs = style("0");
    if (name == "tfrac" || name == "tbinom") attrs = style("1");
    if (name == "cfrac") cursor_.optional();
    if (name == "genfrac") {
        left = readDelimiter(true);
        right = readDelimiter(true);
        auto size = literal(cursor_.group());
        if (!size.empty()) thickness = dimension(size);
        auto mode = literal(cursor_.group());
        attrs = style(mode);
        if (!mode.empty() && attrs.empty()) throw Error("Invalid fraction style");
    }
    auto top = argument();
    auto bottom = argument();
    auto body = element("mfrac", {top, bottom}, thickness.empty() ? "" : "linethickness=\"" + thickness + "\"");
    if (!left.empty() || !right.empty()) body = row(fence(left, "prefix") + body + fence(right, "postfix"));
    return attrs.empty() ? body : element("mstyle", body, attrs);
}

std::string Parser::text(std::string_view name) {
    auto tokens = cursor_.group();
    Cursor contents(tokens);
    std::string result;
    std::string plain;
    auto flush = [&] {
        if (plain.empty()) return;
        std::string attributes = "style=\"white-space:pre-wrap";
        if (name == "textbf") attributes += ";font-weight:bold";
        if (name == "textit" || name == "emph") attributes += ";font-style:italic";
        if (name == "textsf") attributes += ";font-family:sans-serif";
        if (name == "texttt") attributes += ";font-family:monospace";
        result += element("mtext", escape(plain), attributes + '"');
        plain.clear();
    };
    while (contents.peek()) {
        const auto& token = contents.take();
        if (token.is("$") || token.command("(")) {
            flush();
            Tokens math;
            while (contents.peek() && !(token.is("$") ? contents.at("$") : contents.atCommand(")"))) {
                math.push_back(contents.take());
            }
            if (!contents.peek()) throw Error("Unclosed math inside text");
            contents.take();
            result += expression(math);
        } else if (token.kind == Kind::command && token.text.size() > 1) {
            if (token.text.starts_with("text") || token.text == "emph") {
                flush();
                auto nested = contents.group();
                nested.insert(nested.begin(), {Kind::character, "{"});
                nested.insert(nested.begin(), token);
                nested.push_back({Kind::character, "}"});
                result += expression(nested);
            } else throw Error("Unsupported command inside text: \\" + token.text);
        } else if (!token.is("{") && !token.is("}")) plain += token.text;
    }
    flush();
    return row(result);
}

std::string Parser::decoration(std::string_view name) {
    static const std::unordered_map<std::string_view, Accent> accents{
        {"hat", {"^"}}, {"widehat", {"^", false, true}},
        {"tilde", {"~"}}, {"widetilde", {"~", false, true}},
        {"bar", {"¯"}}, {"overline", {"¯", false, true}}, {"underline", {"_", true, true}},
        {"vec", {"→"}}, {"overrightarrow", {"→", false, true}}, {"overleftarrow", {"←", false, true}},
        {"overleftrightarrow", {"↔", false, true}}, {"underrightarrow", {"→", true, true}},
        {"underleftarrow", {"←", true, true}}, {"underleftrightarrow", {"↔", true, true}},
        {"dot", {"˙"}}, {"ddot", {"¨"}}, {"dddot", {"⃛"}}, {"ddddot", {"⃜"}},
        {"breve", {"˘"}}, {"acute", {"´"}}, {"grave", {"ˋ"}}, {"check", {"ˇ"}},
        {"overbrace", {"⏞", false, true, true}}, {"underbrace", {"⏟", true, true, true}},
        {"overgroup", {"⏠", false, true, true}}, {"undergroup", {"⏡", true, true, true}},
        {"overparen", {"⏜", false, true, true}}, {"underparen", {"⏝", true, true, true}}
    };
    auto match = accents.find(name);
    if (match == accents.end()) return "";
    const auto& accent = match->second;
    auto glyph = op(accent.glyph, accent.stretch ? "stretchy=\"true\"" : "stretchy=\"false\"");
    return element(accent.under ? "munder" : "mover", {argument(), glyph},
                   accent.brace ? "" : accent.under ? "accentunder=\"true\"" : "accent=\"true\"");
}

std::string Parser::styling(std::string_view name) {
    static const std::unordered_map<std::string_view, std::string_view> fonts{
        {"mathrm", "normal"}, {"mathnormal", ""}, {"mathit", "italic"}, {"mathbf", "bold"},
        {"boldsymbol", "bold-italic"}, {"bm", "bold-italic"}, {"mathbb", "double-struck"},
        {"Bbb", "double-struck"}, {"mathcal", "script"}, {"mathscr", "script"},
        {"mathfrak", "fraktur"}, {"mathsf", "sans-serif"}, {"mathtt", "monospace"},
        {"mathbfit", "bold-italic"}, {"mathbfscr", "bold-script"}, {"mathbffrak", "bold-fraktur"},
        {"mathsfbf", "sans-serif-bold"}, {"mathsfit", "sans-serif-italic"}, {"mathsfbfit", "sans-serif-bold-italic"}
    };
    if (auto it = fonts.find(name); it != fonts.end()) return argument(it->second);
    if (auto attrs = style(name); !attrs.empty()) return element("mstyle", row(sequence()), attrs);
    if (name == "color" || name == "textcolor") {
        auto color = literal(cursor_.group());
        if (!validColor(color)) throw Error("Invalid math color");
        return element("mstyle", name == "color" ? row(sequence()) : argument(), "mathcolor=\"" + color + "\"");
    }
    return "";
}

std::string Parser::spacing(std::string_view name) {
    static const std::unordered_map<std::string_view, std::string_view> widths{
        {",", "0.1667em"}, {":", "0.2222em"}, {">", "0.2222em"}, {";", "0.2778em"}, {"!", "-0.1667em"},
        {"thinspace", "0.1667em"}, {"medspace", "0.2222em"}, {"thickspace", "0.2778em"},
        {"negthinspace", "-0.1667em"}, {"negmedspace", "-0.2222em"}, {"negthickspace", "-0.2778em"},
        {" ", "0.3333em"}, {"enspace", "0.5em"}, {"enskip", "0.5em"}, {"quad", "1em"}, {"qquad", "2em"}
    };
    if (auto it = widths.find(name); it != widths.end()) return space(it->second);
    if (name != "hspace" && name != "kern" && name != "mkern" && name != "mskip") return "";
    cursor_.skipSpace();
    cursor_.match("*");
    if (cursor_.at("{")) return space(dimension(literal(cursor_.group())));
    std::string amount;
    while (digit(cursor_.peek()) || cursor_.at(".") || cursor_.at("+") || cursor_.at("-")) amount += cursor_.take().text;
    amount += cursor_.take().text;
    amount += cursor_.take().text;
    return space(dimension(std::move(amount)));
}

}
