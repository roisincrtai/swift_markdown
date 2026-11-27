#include "parser.hpp"
#include "mathml.hpp"
#include "symbols.hpp"

#include <unordered_map>
#include <utility>

namespace tex::detail {

std::string row(std::string_view content) { return element("mrow", content); }

std::string op(std::string_view value, std::string_view attributes) {
    return element("mo", escape(value), attributes);
}

std::string fence(std::string_view glyph, std::string_view form) {
    if (glyph.empty()) return "";
    return op(glyph, "stretchy=\"true\" fence=\"true\" form=\"" + std::string(form) + "\"");
}

std::string space(std::string_view width) {
    return element("mspace", "", "width=\"" + std::string(width) + "\"");
}

bool digit(const Token* token) {
    return token && token->kind == Kind::character && token->text.size() == 1 &&
           token->text[0] >= '0' && token->text[0] <= '9';
}

Parser::Parser(const Tokens& tokens, unsigned depth, std::string_view variant)
    : cursor_(tokens), depth_(depth), variant_(variant) {
    if (depth_ > 96) throw Error("Formula nesting limit exceeded");
}

Parser::Nesting::Nesting(unsigned& value) : value_(value) {
    if (++value_ > 96) throw Error("Formula nesting limit exceeded");
}

std::string Parser::complete() {
    auto body = sequence();
    if (cursor_.peek()) throw Error("Unexpected " + cursor_.peek()->text);
    return row(body);
}

std::string Parser::expression(const Tokens& tokens) {
    return Parser(tokens, depth_ + 1, variant_).complete();
}

bool Parser::boundary() const {
    return !cursor_.peek() || cursor_.at("}") || cursor_.at("&") || cursor_.atCommand("\\") ||
           cursor_.atCommand("cr") || cursor_.atCommand("end") || cursor_.atCommand("right") ||
           cursor_.atCommand("middle");
}

std::string Parser::sequence(bool acceptsFraction) {
    Nesting nested(depth_);
    std::string result;
    cursor_.skipSpace();
    while (!boundary()) {
        if (cursor_.atCommand("over") || cursor_.atCommand("atop") || cursor_.atCommand("choose")) {
            if (!acceptsFraction) throw Error("Multiple fraction operators in one group");
            auto mode = cursor_.take().text;
            auto top = row(result);
            auto bottom = row(sequence(false));
            auto fraction = element("mfrac", {top, bottom}, mode == "over" ? "" : "linethickness=\"0\"");
            return mode == "choose" ? fence("(", "prefix") + fraction + fence(")", "postfix") : fraction;
        }
        result += scripted(primary());
        cursor_.skipSpace();
    }
    return result;
}

std::string Parser::argument(std::string_view variant) {
    Nesting nested(depth_);
    cursor_.skipSpace();
    if (boundary()) throw Error("Missing math argument");
    std::swap(variant_, variant);
    auto result = primary(false).markup;
    variant_ = variant;
    return result;
}

Atom Parser::primary(bool combineDigits) {
    if (cursor_.match("{")) {
        auto body = sequence();
        if (!cursor_.match("}")) throw Error("Unclosed math group");
        return {row(body)};
    }
    if (cursor_.at("_") || cursor_.at("^") || cursor_.at("'")) {
        if (!combineDigits) throw Error("Expected math script argument");
        return {row("")};
    }
    const auto& token = cursor_.take();
    if (token.kind == Kind::command) return command(token.text);
    if (digit(&token)) {
        auto number = token.text;
        if (combineDigits) {
            while (digit(cursor_.peek())) number += cursor_.take().text;
            if (cursor_.match(".")) {
                number += '.';
                while (digit(cursor_.peek())) number += cursor_.take().text;
            }
        }
        return {element("mn", escape(styledIdentifier(number, variant_)))};
    }
    if (token.is("-")) return {op("−")};
    if (token.is("~")) return {space("0.3333em")};
    if (std::string_view("=+*/<>()[]|!?,.:;").find(token.text) != std::string_view::npos ||
        token.text == "≤" || token.text == "≥" || token.text == "×" || token.text == "÷" ||
        token.text == "−" || token.text == "∈" || token.text == "→" || token.text == "±") {
        return {op(token.text, "stretchy=\"false\"")};
    }
    return {identifier(token.text)};
}

std::string Parser::identifier(std::string_view value, bool upright) const {
    const auto styled = styledIdentifier(value, variant_);
    return element("mi", escape(styled), upright || styled != value || variant_ == "normal" ? "mathvariant=\"normal\"" : "");
}

std::string Parser::scripted(Atom base) {
    bool stacked = base.limits;
    std::string sub;
    std::string sup;
    std::string primes;
    cursor_.skipSpace();
    for (;;) {
        if (cursor_.matchCommand("limits")) {
            stacked = true;
            auto attribute = base.markup.find("movablelimits=\"true\"");
            if (attribute != base.markup.npos) {
                constexpr std::string_view prior = "movablelimits=\"true\"";
                base.markup.replace(attribute, prior.size(), "movablelimits=\"false\"");
            }
        } else if (cursor_.matchCommand("displaylimits")) stacked = true;
        else if (cursor_.matchCommand("nolimits")) stacked = false;
        else if (cursor_.match("_")) {
            if (!sub.empty()) throw Error("Double subscript");
            sub = argument();
        } else if (cursor_.match("^")) {
            if (!sup.empty()) throw Error("Double superscript");
            sup = argument();
        } else if (cursor_.match("'")) primes += "′";
        else break;
        cursor_.skipSpace();
    }
    if (!primes.empty()) sup = sup.empty() ? op(primes) : row(op(primes) + sup);
    if (sub.empty() && sup.empty()) return base.markup;
    if (sub.empty()) return element(stacked ? "mover" : "msup", {base.markup, sup});
    if (sup.empty()) return element(stacked ? "munder" : "msub", {base.markup, sub});
    return element(stacked ? "munderover" : "msubsup", {base.markup, sub, sup});
}

Atom Parser::command(std::string_view name) {
    if (const auto entry = symbol(name)) {
        switch (entry->role) {
            case Role::identifier: return {identifier(entry->glyph)};
            case Role::constant: return {identifier(entry->glyph, true)};
            case Role::function: return {op(entry->glyph, "stretchy=\"false\" lspace=\"0.12em\" rspace=\"0.12em\"")};
            case Role::limits: return {op(entry->glyph, "movablelimits=\"true\""), true};
            case Role::large: return {op(entry->glyph, "largeop=\"true\" movablelimits=\"false\"")};
            case Role::delimiter: return {op(entry->glyph, "stretchy=\"false\"")};
            case Role::oper: return {op(entry->glyph)};
        }
    }
    if (name == "frac" || name == "dfrac" || name == "tfrac" || name == "cfrac" || name == "genfrac" ||
        name == "binom" || name == "dbinom" || name == "tbinom") return {fraction(name)};
    if (name == "sqrt") {
        cursor_.skipSpace();
        bool indexed = cursor_.at("[");
        auto index = cursor_.optional();
        auto body = argument();
        return {indexed ? element("mroot", {body, expression(index)}) : element("msqrt", body)};
    }
    if (name == "left") return {scalableFence()};
    if (name == "begin") return {environment(literal(cursor_.group()))};
    if (name == "substack") {
        auto tokens = cursor_.group();
        Parser inner(tokens, depth_ + 1, variant_);
        auto result = inner.table({"center"});
        if (inner.cursor_.peek()) throw Error("Invalid substack");
        return {element("mstyle", result, "scriptlevel=\"1\"")};
    }
    if (name == "text" || name == "textnormal" || name == "textrm" || name == "textsf" ||
        name == "texttt" || name == "textbf" || name == "textit" || name == "emph" || name == "mbox" || name == "hbox") {
        return {text(name)};
    }
    if (name == "operatorname") {
        cursor_.skipSpace();
        bool limits = cursor_.match("*");
        return {op(literal(cursor_.group()), limits ? "movablelimits=\"true\"" : "lspace=\"0.12em\" rspace=\"0.12em\""), limits};
    }
    if (name == "overset" || name == "stackrel" || name == "underset") {
        auto label = argument();
        auto base = argument();
        return {element(name == "underset" ? "munder" : "mover", {base, label})};
    }
    static const std::unordered_map<std::string_view, std::string_view> arrows{
        {"xrightarrow", "⟶"}, {"xleftarrow", "⟵"}, {"xleftrightarrow", "⟷"}, {"xRightarrow", "⟹"},
        {"xLeftarrow", "⟸"}, {"xLeftrightarrow", "⟺"}, {"xmapsto", "⟼"}, {"xhookrightarrow", "↪"},
        {"xhookleftarrow", "↩"}, {"xrightleftharpoons", "⇌"}, {"xleftrightharpoons", "⇋"}
    };
    if (auto arrow = arrows.find(name); arrow != arrows.end()) {
        auto below = cursor_.optional();
        auto above = argument();
        auto base = op(arrow->second, "stretchy=\"true\" minsize=\"1.5em\"");
        return {below.empty() ? element("mover", {base, above}) :
                               element("munderover", {base, expression(below), above})};
    }
    if (auto result = decoration(name); !result.empty()) return {std::move(result)};
    if (auto result = styling(name); !result.empty()) return {std::move(result)};
    if (auto result = spacing(name); !result.empty()) return {std::move(result)};
    if (name == "pmod" || name == "pod") {
        auto label = name == "pmod" ? element("mtext", "mod") + space("0.25em") : "";
        return {row(space("0.8em") + fence("(", "prefix") + label + argument() + fence(")", "postfix"))};
    }
    if (name == "phantom" || name == "hphantom" || name == "vphantom" || name == "smash") {
        if (name == "smash") cursor_.optional();
        auto body = argument();
        if (name == "smash") return {element("mpadded", body, "height=\"0\" depth=\"0\"")};
        body = element("mphantom", body);
        if (name == "hphantom") return {element("mpadded", body, "height=\"0\" depth=\"0\"")};
        if (name == "vphantom") return {element("mpadded", body, "width=\"0\"")};
        return {std::move(body)};
    }
    if (name == "boxed" || name == "cancel" || name == "bcancel" || name == "xcancel" || name == "not") {
        std::string_view notation = name == "boxed" ? "box" : name == "bcancel" ? "downdiagonalstrike" :
                                    name == "xcancel" ? "updiagonalstrike downdiagonalstrike" : "updiagonalstrike";
        return {element("menclose", argument(), "notation=\"" + std::string(notation) + "\"")};
    }
    if (name == "tag") {
        cursor_.skipSpace();
        bool bare = cursor_.match("*");
        auto tag = literal(cursor_.group());
        return {row(space("1.2em") + element("mtext", escape(bare ? tag : "(" + tag + ")")))};
    }
    if (name == "label") cursor_.group();
    if (name == "label" || name == "nonumber" || name == "notag" || name == "relax") return {row("")};
    if (name == "ensuremath" || name == "mathop" || name == "mathord" || name == "mathbin" ||
        name == "mathrel" || name == "mathopen" || name == "mathclose" || name == "mathpunct" || name == "mathinner") {
        return {argument(), name == "mathop"};
    }
    if (name.starts_with("big") || name.starts_with("Big")) {
        auto sizeLevel = name.substr(3);
        bool tall = sizeLevel.starts_with("g");
        if (tall) sizeLevel.remove_prefix(1);
        if (sizeLevel.empty() || sizeLevel == "l" || sizeLevel == "r" || sizeLevel == "m") {
            std::string size = tall ? (name.front() == 'B' ? "2.6em" : "2.2em") :
                                      (name.front() == 'B' ? "1.8em" : "1.4em");
            return {op(readDelimiter(), "stretchy=\"true\" minsize=\"" + size + "\" maxsize=\"" + size + "\"")};
        }
    }
    throw Error("Unsupported TeX command: \\" + std::string(name));
}

}
