#include "parser.hpp"
#include "mathml.hpp"

#include <algorithm>
#include <unordered_map>

namespace tex::detail {

std::string Parser::table(const std::vector<std::string_view>& alignment, std::string_view lines, bool repeating) {
    std::string rows;
    std::string rowLines;
    for (std::size_t rowIndex = 0;; ++rowIndex) {
        cursor_.skipSpace();
        bool rule = cursor_.matchCommand("hline");
        bool dash = cursor_.matchCommand("hdashline");
        cursor_.skipSpace();
        if (!cursor_.peek() || cursor_.atCommand("end")) break;
        if (rowIndex > 255) throw Error("Too many math table rows");
        if (rowIndex) {
            if (!rowLines.empty()) rowLines += ' ';
            rowLines += rule ? "solid" : dash ? "dashed" : "none";
        }
        std::string cells;
        std::size_t col = 0;
        do {
            if (col >= 256) throw Error("Too many math table columns");
            auto align = alignment[repeating ? col % alignment.size() : std::min(col, alignment.size() - 1)];
            cells += element("mtd", row(sequence()), "style=\"text-align:" + std::string(align) + "\"");
            ++col;
        } while (cursor_.match("&"));
        rows += element("mtr", cells);
        if (!cursor_.matchCommand("\\") && !cursor_.matchCommand("cr")) break;
        cursor_.match("*");
        cursor_.optional();
    }
    std::string attrs = "columnalign=\"";
    for (auto align : alignment) {
        if (!attrs.ends_with('"')) attrs += ' ';
        attrs += align;
    }
    attrs += '"';
    if (repeating) attrs += " columnspacing=\"0.2em 1em\"";
    if (!lines.empty()) attrs += " columnlines=\"" + std::string(lines) + "\"";
    if (!rowLines.empty()) attrs += " rowlines=\"" + rowLines + "\"";
    return element("mtable", rows, attrs);
}

std::string Parser::environment(const std::string& name) {
    static const std::unordered_map<std::string_view, std::pair<std::string_view, std::string_view>> environments{
        {"matrix", {"", ""}}, {"matrix*", {"", ""}}, {"smallmatrix", {"", ""}},
        {"pmatrix", {"(", ")"}}, {"bmatrix", {"[", "]"}}, {"Bmatrix", {"{", "}"}},
        {"vmatrix", {"|", "|"}}, {"Vmatrix", {"‖", "‖"}},
        {"pmatrix*", {"(", ")"}}, {"bmatrix*", {"[", "]"}}, {"Bmatrix*", {"{", "}"}},
        {"vmatrix*", {"|", "|"}}, {"Vmatrix*", {"‖", "‖"}},
        {"cases", {"{", ""}}, {"dcases", {"{", ""}}, {"cases*", {"{", ""}}, {"dcases*", {"{", ""}},
        {"rcases", {"", "}"}}, {"drcases", {"", "}"}},
        {"aligned", {"", ""}}, {"align", {"", ""}}, {"align*", {"", ""}},
        {"alignat", {"", ""}}, {"alignat*", {"", ""}}, {"alignedat", {"", ""}},
        {"flalign", {"", ""}}, {"flalign*", {"", ""}}, {"split", {"", ""}},
        {"gathered", {"", ""}}, {"gather", {"", ""}}, {"gather*", {"", ""}},
        {"multline", {"", ""}}, {"multline*", {"", ""}},
        {"equation", {"", ""}}, {"equation*", {"", ""}},
        {"array", {"", ""}}, {"subarray", {"", ""}}
    };
    auto found = environments.find(name);
    if (found == environments.end()) throw Error("Unsupported math environment: " + name);
    const auto& [left, right] = found->second;
    bool alternating = name.find("align") != name.npos || name == "split";
    if (name == "aligned" || name == "gathered" || name == "alignedat" || name == "array") cursor_.optional();
    std::vector<std::string_view> alignment{ name.find("cases") == name.npos ? "center" : "left" };
    std::string lines;
    if (alternating) alignment = {"right", "left"};
    if (name.find("alignat") != name.npos) cursor_.group();
    if (name.find("matrix*") != name.npos) {
        auto align = literal(cursor_.optional());
        if (align == "l") alignment = {"left"};
        if (align == "r") alignment = {"right"};
    }
    if (name == "array" || name == "subarray") {
        alignment.clear();
        bool rule = false;
        for (const auto& token : cursor_.group()) {
            if (token.is("|")) rule = true;
            if (!token.is("l") && !token.is("c") && !token.is("r")) continue;
            if (!alignment.empty()) {
                if (!lines.empty()) lines += ' ';
                lines += rule ? "solid" : "none";
            }
            rule = false;
            alignment.push_back(token.is("l") ? "left" : token.is("r") ? "right" : "center");
        }
        if (alignment.empty()) throw Error("Expected array column specifiers");
    }
    auto body = name == "equation" || name == "equation*" ? sequence() : table(alignment, lines, alternating);
    if (!cursor_.matchCommand("end") || literal(cursor_.group()) != name) throw Error("Expected \\end{" + name + "}");
    if (name == "smallmatrix" || name == "subarray") body = element("mstyle", body, "scriptlevel=\"1\"");
    if (name.starts_with("dcases") || name == "drcases") body = element("mstyle", body, style("0"));
    return row(fence(left, "prefix") + body + fence(right, "postfix"));
}

}
