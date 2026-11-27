#include "markdown.hpp"
#include "inline.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace markdown {
namespace {

using detail::escape;
using detail::trim;
using detail::white;
using Lines = std::vector<std::string_view>;

std::size_t indent(std::string_view line) { return detail::run(line, 0, ' '); }

std::string_view content(std::string_view line) {
    auto offset = indent(line);
    return offset <= 3 ? line.substr(offset) : line;
}

bool all(std::string_view text, char ch) {
    return !text.empty() && text.find_first_not_of(ch) == text.npos;
}

std::pair<unsigned, std::string_view> heading(std::string_view line) {
    line = content(line);
    auto end = detail::run(line, 0, '#');
    if (end == 0 || end > 6 || (end < line.size() && !white(line[end]))) return {};
    auto title = trim(line.substr(end));
    auto last = title.find_last_not_of('#');
    if (last == title.npos) title = {};
    else if (last + 1 < title.size() && white(title[last])) title = trim(title.substr(0, last + 1));
    return {static_cast<unsigned>(end), title};
}

unsigned setext(std::string_view line) {
    if (indent(line) > 3) return 0;
    line = trim(line);
    if (all(line, '=')) return 1;
    if (all(line, '-')) return 2;
    return 0;
}

bool thematic(std::string_view line) {
    line = content(line);
    if (line.empty() || (line.front() != '*' && line.front() != '_' && line.front() != '-')) return false;
    unsigned count = 0;
    for (char c : line) {
        if (c == line.front()) ++count;
        else if (!white(c)) return false;
    }
    return count >= 3;
}

struct Fence {
    char marker;
    std::size_t length;
    std::size_t indentation;
    std::string_view info;
};

std::optional<Fence> fence(std::string_view line) {
    auto indentation = indent(line);
    if (indentation > 3) return std::nullopt;
    line.remove_prefix(indentation);
    if (line.empty() || (line.front() != 96 && line.front() != '~')) return std::nullopt;
    auto length = detail::run(line, 0, line.front());
    if (length < 3) return std::nullopt;
    auto info = trim(line.substr(length));
    if (line.front() == 96 && info.find(96) != info.npos) return std::nullopt;
    return Fence{line.front(), length, indentation, info};
}

bool closes(const Fence& opening, const Fence& candidate) {
    return candidate.marker == opening.marker && candidate.length >= opening.length && candidate.info.empty();
}

struct ListMarker {
    char kind;
    std::size_t indentation;
    std::size_t bodyOffset;
    std::string_view number;
};

std::optional<ListMarker> listMarker(std::string_view line) {
    auto indentation = indent(line);
    if (indentation > 3 || indentation == line.size()) return std::nullopt;
    auto token = indentation;
    while (token < line.size() && detail::digit(line[token]) && token - indentation < 9) ++token;
    if (token == line.size()) return std::nullopt;
    bool ordered = token != indentation;
    char kind = line[token];
    auto number = line.substr(indentation, token - indentation);
    if (ordered ? (kind != '.' && kind != ')') : (kind != '-' && kind != '+' && kind != '*')) return std::nullopt;
    if (token + 1 < line.size() && !white(line[token + 1])) return std::nullopt;
    auto offset = token + 1;
    while (offset < line.size() && white(line[offset]) && offset - token <= 4) ++offset;
    return ListMarker{kind, indentation, offset, number};
}

std::optional<std::pair<std::string, std::string>> definition(std::string_view line) {
    line = content(line);
    if (!line.starts_with("[")) return std::nullopt;
    auto close = line.find("]:");
    if (close == line.npos || close < 2) return std::nullopt;
    auto key = detail::referenceKey(line.substr(1, close - 1));
    auto destination = trim(line.substr(close + 2));
    if (key.empty() || destination.empty()) return std::nullopt;
    if (destination.starts_with("<")) {
        auto end = destination.find('>');
        if (end == destination.npos) return std::nullopt;
        destination = destination.substr(1, end - 1);
    } else destination = destination.substr(0, destination.find_first_of(" \t"));
    return {{std::move(key), std::string(destination)}};
}

std::vector<std::string_view> cells(std::string_view line) {
    std::vector<std::string_view> result;
    line = trim(line);
    if (line.starts_with("|")) line.remove_prefix(1);
    if (line.ends_with("|") && !detail::escaped(line, line.size() - 1)) line.remove_suffix(1);
    std::size_t start = 0;
    std::size_t code = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == 96 && !detail::escaped(line, i)) {
            auto length = detail::run(line, i, 96);
            if (code == 0 || code == length) code = code == 0 ? length : 0;
            i += length - 1;
        } else if (!code && (line[i] == '$' || line[i] == '\\') && !detail::escaped(line, i)) {
            if (auto math = detail::formulaAt(line, i)) i = math->end - 1;
        } else if (line[i] == '|' && !code && !detail::escaped(line, i)) {
            result.push_back(trim(line.substr(start, i - start)));
            start = i + 1;
        }
    }
    result.push_back(trim(line.substr(start)));
    return result;
}

std::vector<std::string_view> tableAlignments(std::string_view header, std::string_view rule) {
    if (rule.find('|') == rule.npos) return {};
    auto result = cells(rule);
    if (result.size() != cells(header).size()) return {};
    for (auto& marker : result) {
        bool leading = marker.starts_with(":");
        bool trailing = marker.ends_with(":");
        if (leading) marker.remove_prefix(1);
        if (trailing && !marker.empty()) marker.remove_suffix(1);
        if (marker.size() < 3 || !all(marker, '-')) return {};
        marker = leading && trailing ? "center" : trailing ? "right" : "left";
    }
    return result;
}

Lines split(std::string_view source) {
    Lines lines;
    while (!source.empty()) {
        auto n = source.find('\n');
        lines.push_back(source.substr(0, n));
        if (n == source.npos) break;
        source.remove_prefix(n + 1);
    }
    return lines;
}

std::string join(const Lines& lines, std::size_t start, std::size_t end) {
    std::string result;
    for (auto i = start; i < end; ++i) {
        if (i != start) result += '\n';
        result += lines[i];
    }
    return result;
}

std::string displayEnding(std::string_view line) {
    line = trim(line);
    if (line.starts_with("$$")) return "$$";
    if (line.starts_with("\\[")) return "\\]";
    if (!line.starts_with("\\begin{")) return "";
    auto end = line.find('}');
    if (end == line.npos) return "";
    auto name = line.substr(7, end - 7);
    if (name.ends_with("*")) name.remove_suffix(1);
    if (name == "equation" || name == "align" || name == "gather" || name == "multline" ||
        name == "alignat" || name == "flalign") {
        return "\\end{" + std::string(line.substr(7, end - 7)) + "}";
    }
    return "";
}

class Parser {
public:
    explicit Parser(detail::Inline& inlineRenderer) : inline_(inlineRenderer) {}

    Document render(const Lines& lines) {
        detail::References references;
        std::optional<Fence> insideFence;
        for (auto line : lines) {
            if (auto mark = fence(line)) {
                if (!insideFence) insideFence = mark;
                else if (closes(*insideFence, *mark)) insideFence.reset();
            } else if (!insideFence) {
                if (auto record = definition(line)) references.try_emplace(std::move(record->first), std::move(record->second));
            }
        }
        inline_.setReferences(std::move(references));
        parse(lines, 0, [&](std::string block) { document_.blocks.push_back(std::move(block)); });
        return std::move(document_);
    }

private:
    void parse(const Lines& lines, unsigned depth, const std::function<void(std::string)>& append) {
        if (depth > 32) {
            append("<p>" + escape(join(lines, 0, lines.size())) + "</p>");
            return;
        }
        for (std::size_t i = 0; i < lines.size();) {
            auto line = lines[i];
            auto text = trim(line);
            if (text.empty() || definition(line)) {
                ++i;
                continue;
            }

            if (auto mark = fence(line)) {
                ++i;
                std::string code;
                while (i < lines.size()) {
                    auto closing = fence(lines[i]);
                    if (closing && closes(*mark, *closing)) {
                        ++i;
                        break;
                    }
                    code += lines[i].substr(std::min(mark->indentation, indent(lines[i])));
                    code += '\n';
                    ++i;
                }
                auto lang = mark->info.substr(0, mark->info.find_first_of(" \t"));
                append("<pre><code" + (lang.empty() ? "" : " data-language=\"" + escape(lang) + "\"") +
                       ">" + escape(code) + "</code></pre>");
                continue;
            }

            if (line.starts_with("    ") || line.starts_with("\t")) {
                std::string code;
                do {
                    if (lines[i].starts_with("    ")) code += lines[i].substr(4);
                    else if (lines[i].starts_with("\t")) code += lines[i].substr(1);
                    code += '\n';
                    ++i;
                } while (i < lines.size() && (lines[i].starts_with("    ") || lines[i].starts_with("\t") || trim(lines[i]).empty()));
                while (code.ends_with("\n\n")) code.pop_back();
                append("<pre><code>" + escape(code) + "</code></pre>");
                continue;
            }

            if (auto [level, title] = heading(line); level) {
                append(makeHeading(title, level, i + 1, depth));
                ++i;
                continue;
            }
            if (auto block = mathBlock(lines, i)) {
                append(std::move(*block));
                continue;
            }
            if (i + 1 < lines.size()) {
                if (auto level = setext(lines[i + 1]); level && !thematic(line) &&
                    !content(line).starts_with(">") && !listMarker(line)) {
                    append(makeHeading(text, level, i + 1, depth));
                    i += 2;
                    continue;
                }
                auto alignments = tableAlignments(line, lines[i + 1]);
                if (!alignments.empty()) {
                    append(table(lines, i, alignments));
                    continue;
                }
            }
            if (thematic(line)) {
                append("<hr>");
                ++i;
                continue;
            }
            if (content(line).starts_with(">")) {
                Lines quote;
                while (i < lines.size() && content(lines[i]).starts_with(">")) {
                    auto inner = content(lines[i++]).substr(1);
                    if (inner.starts_with(" ")) inner.remove_prefix(1);
                    quote.push_back(inner);
                }
                std::string body;
                parse(quote, depth + 1, [&](std::string fragment) { body += fragment; });
                append("<blockquote>" + body + "</blockquote>");
                continue;
            }
            if (auto marker = listMarker(line)) {
                append(list(lines, i, *marker, depth));
                continue;
            }

            auto start = i++;
            while (i < lines.size() && !trim(lines[i]).empty()) {
                if (auto level = setext(lines[i]); level) {
                    append(makeHeading(join(lines, start, i), level, start + 1, depth));
                    ++i;
                    start = i;
                    break;
                }
                if (interrupts(lines[i]) ||
                    (i + 1 < lines.size() && !tableAlignments(lines[i], lines[i + 1]).empty())) break;
                ++i;
            }
            if (start < i) append("<p>" + inline_.render(join(lines, start, i)) + "</p>");
        }
    }

    static bool interrupts(std::string_view line) {
        auto simple = content(line);
        return heading(line).first || thematic(line) || fence(line) || simple.starts_with(">") ||
               !displayEnding(line).empty() || listMarker(line) || definition(line);
    }

    std::string makeHeading(std::string_view title, unsigned level, std::size_t line, unsigned depth) {
        auto text = inline_.render(title, true);
        auto body = inline_.render(title);
        auto tag = "h" + std::to_string(level);
        std::string attribute;
        if (depth == 0) {
            auto base = detail::slug(text);
            auto id = base;
            if (!ids_.insert(id).second) {
                auto& suffix = lastSuffix_[base];
                do {
                    id = base + '-' + std::to_string(++suffix);
                } while (!ids_.insert(id).second);
            }
            attribute = " id=\"" + escape(id) + "\"";
            document_.outline.push_back({std::move(id), std::move(text), level, line});
        }
        return "<" + tag + attribute + ">" + body + "</" + tag + ">";
    }

    std::optional<std::string> mathBlock(const Lines& lines, std::size_t& i) {
        auto first = trim(lines[i]);
        auto close = displayEnding(first);
        if (close.empty()) return std::nullopt;
        bool environment = close.starts_with("\\end");
        auto starts = environment ? first : first.substr(2);
        if (starts.ends_with(close) && !detail::escaped(starts, starts.size() - close.size())) {
            auto formula = environment ? starts : trim(starts.substr(0, starts.size() - close.size()));
            ++i;
            return "<div class=\"display-math\">" + inline_.math(formula, true) + "</div>";
        }
        std::string formula(starts);
        auto end = i + 1;
        while (end < lines.size()) {
            auto current = trim(lines[end]);
            if (formula.size() + current.size() > 65536) return std::nullopt;
            formula += '\n';
            if (current.ends_with(close) && !detail::escaped(current, current.size() - close.size())) {
                formula += environment ? current : current.substr(0, current.size() - close.size());
                i = end + 1;
                return "<div class=\"display-math\">" + inline_.math(trim(formula), true) + "</div>";
            }
            formula += current;
            ++end;
        }
        return std::nullopt;
    }

    std::string table(const Lines& lines, std::size_t& i, const std::vector<std::string_view>& alignment) {
        auto renderRow = [&](std::string_view line, std::string_view tag) {
            auto columns = cells(line);
            std::string row = "<tr>";
            for (std::size_t j = 0; j < alignment.size(); ++j) {
                row += "<" + std::string(tag) + " class=\"align-" + std::string(alignment[j]) + "\">";
                if (j < columns.size()) row += inline_.render(columns[j]);
                row += "</" + std::string(tag) + ">";
            }
            return row + "</tr>";
        };

        std::string result = "<div class=\"table-scroll\"><table><thead>" + renderRow(lines[i], "th") + "</thead><tbody>";
        i += 2;
        while (i < lines.size() && !trim(lines[i]).empty() && lines[i].find('|') != lines[i].npos &&
               !interrupts(lines[i])) result += renderRow(lines[i++], "td");
        return result + "</tbody></table></div>";
    }

    std::string list(const Lines& lines, std::size_t& i, ListMarker first, unsigned depth) {
        bool ordered = !first.number.empty();
        auto tag = ordered ? "ol" : "ul";
        std::string markup = "<" + std::string(tag);
        if (ordered && first.number != "1") markup += " start=\"" + std::string(first.number) + "\"";
        markup += '>';
        while (i < lines.size()) {
            auto marker = listMarker(lines[i]);
            if (!marker || marker->kind != first.kind || marker->indentation != first.indentation) break;
            auto item = lines[i].substr(marker->bodyOffset);
            std::string attributes;
            std::string checkbox;
            if (item.size() >= 3 && item[0] == '[' && item[2] == ']' &&
                (item[1] == ' ' || item[1] == 'x' || item[1] == 'X') &&
                (item.size() == 3 || white(item[3]))) {
                attributes = " class=\"task\"";
                checkbox = item[1] == ' ' ? "<input type=\"checkbox\" disabled aria-label=\"Incomplete\">" :
                                           "<input type=\"checkbox\" disabled checked aria-label=\"Complete\">";
                item = trim(item.substr(3));
            }
            auto pad = std::max(marker->bodyOffset, marker->indentation + 2);
            Lines inner{item};
            ++i;
            while (i < lines.size()) {
                if (trim(lines[i]).empty()) {
                    auto next = i + 1;
                    while (next < lines.size() && trim(lines[next]).empty()) ++next;
                    if (next == lines.size()) break;
                    if (indent(lines[next]) < pad) {
                        auto nextMarker = listMarker(lines[next]);
                        if (nextMarker && nextMarker->kind == first.kind && nextMarker->indentation == first.indentation) i = next;
                        break;
                    }
                    i = next;
                    inner.push_back({});
                } else if (indent(lines[i]) >= pad) {
                    inner.push_back(lines[i++].substr(pad));
                } else break;
            }
            std::string body;
            parse(inner, depth + 1, [&](std::string fragment) { body += fragment; });
            markup += "<li" + attributes + ">" + checkbox + body + "</li>";
        }
        return markup + "</" + tag + ">";
    }

    Document document_;
    detail::Inline& inline_;
    std::unordered_set<std::string> ids_;
    std::unordered_map<std::string, unsigned> lastSuffix_;
};

}

struct Renderer::Cache { detail::Inline inlineRenderer; };

Renderer::Renderer() : cache_(std::make_unique<Cache>()) {}
Renderer::~Renderer() = default;

Document Renderer::render(std::string_view source) {
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    std::string normalized;
    if (source.find('\r') != source.npos || source.find('\0') != source.npos) {
        normalized.reserve(source.size());
        for (std::size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '\0') normalized += "\xEF\xBF\xBD";
            else if (source[i] != '\r') normalized += source[i];
            else if (i + 1 == source.size() || source[i + 1] != '\n') normalized += '\n';
        }
        source = normalized;
    }
    return Parser(cache_->inlineRenderer).render(split(source));
}

}
