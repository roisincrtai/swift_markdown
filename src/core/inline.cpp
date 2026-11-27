#include "inline.hpp"
#include "tex.hpp"

#include <algorithm>
#include <cctype>

namespace markdown::detail {
namespace {

bool punctuation(char ch) { return std::ispunct(static_cast<unsigned char>(ch)); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::size_t closing(std::string_view text, std::size_t pos, std::string_view marker) {
    while ((pos = text.find(marker, pos)) != text.npos) {
        if (!escaped(text, pos)) return pos;
        pos += marker.size();
    }
    return text.npos;
}

std::size_t matching(std::string_view text, std::size_t start, char left, char right) {
    unsigned depth = 0;
    for (auto i = start; i < text.size() && i - start <= 8192; ++i) {
        if (text[i] == left && !escaped(text, i)) ++depth;
        else if (text[i] == right && !escaped(text, i)) {
            if (depth == 0) return i;
            --depth;
        }
    }
    return text.npos;
}

std::optional<std::pair<std::string_view, std::size_t>> target(std::string_view text, std::size_t open) {
    if (open >= text.size() || text[open] != '(') return std::nullopt;
    auto end = matching(text, open + 1, '(', ')');
    if (end == text.npos) return std::nullopt;
    auto value = trim(text.substr(open + 1, end - open - 1));
    if (value.starts_with("<")) {
        const auto angle = value.find('>');
        if (angle == value.npos) return std::nullopt;
        return {{value.substr(1, angle - 1), end + 1}};
    }
    return {{value.substr(0, value.find_first_of(" \t\n")), end + 1}};
}

bool allowedURL(std::string_view url, bool image) {
    if (url.starts_with("/") || url.starts_with("\\") || (image && url.empty())) return false;
    for (unsigned char c : url) {
        if (c < 0x20 || c == '\\' || c == 0x7f) return false;
    }
    const auto scheme = url.find(':');
    if (scheme == url.npos || scheme > url.find_first_of("/?#")) return true;
    if (image) return false;
    std::string name(url.substr(0, scheme));
    std::transform(name.begin(), name.end(), name.begin(), lower);
    return name == "https" || name == "http" || name == "mailto";
}

std::optional<std::pair<std::string_view, std::string_view>> entity(std::string_view tail) {
    auto end = tail.substr(0, 17).find(';');
    if (end == tail.npos) return std::nullopt;
    const auto name = tail.substr(1, end - 1);
    const auto raw = tail.substr(0, end + 1);
    if (name == "amp") return {{raw, "&"}};
    if (name == "lt") return {{raw, "<"}};
    if (name == "gt") return {{raw, ">"}};
    if (name == "quot") return {{raw, "\""}};
    if (name == "apos" || name == "#39") return {{raw, "'"}};
    if (name == "nbsp") return {{raw, " "}};
    if (name.starts_with("#")) {
        auto number = name.substr(1);
        bool hex = number.starts_with("x") || number.starts_with("X");
        if (hex) number.remove_prefix(1);
        if (!number.empty() && number.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789") == number.npos) {
            return {{raw, raw}};
        }
    }
    return std::nullopt;
}

}

bool white(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

std::size_t run(std::string_view text, std::size_t start, char marker) {
    auto end = start;
    while (end < text.size() && text[end] == marker) ++end;
    return end - start;
}

std::string escape(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output += c;
        }
    }
    return output;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && white(value.front())) value.remove_prefix(1);
    while (!value.empty() && white(value.back())) value.remove_suffix(1);
    return value;
}

bool escaped(std::string_view value, std::size_t position) {
    auto start = position;
    while (start && value[start - 1] == '\\') --start;
    return (position - start) % 2 != 0;
}

std::string slug(std::string_view text) {
    std::string output;
    bool separator = false;
    for (auto c : text) {
        if (white(c) || c == '-') separator = !output.empty();
        else if (digit(c) || (lower(c) >= 'a' && lower(c) <= 'z') || c == '_' ||
                 static_cast<unsigned char>(c) >= 0x80) {
            if (separator) output += '-';
            output += lower(c);
            separator = false;
        }
    }
    return output.empty() ? "section" : output;
}

std::string referenceKey(std::string_view text) {
    std::string result;
    bool separator = false;
    for (char c : trim(text)) {
        if (white(c)) separator = true;
        else {
            if (separator) result += ' ';
            result += lower(c);
            separator = false;
        }
    }
    return result;
}

std::optional<Formula> formulaAt(std::string_view text, std::size_t start) {
    auto tail = text.substr(start);
    std::string_view close;
    bool display = false;
    std::size_t opening = 2;
    if (tail.starts_with("$$")) {
        display = true;
        close = "$$";
    } else if (tail.starts_with("\\[")) {
        display = true;
        close = "\\]";
    } else if (tail.starts_with("\\(")) close = "\\)";
    else if (tail.starts_with("$") && tail.size() > 1 && !white(tail[1]) &&
             (start == 0 || text[start - 1] != '$')) {
        close = "$";
        opening = 1;
    } else return std::nullopt;

    const auto contentStart = start + opening;
    unsigned depth = 0;
    for (auto end = contentStart; end < text.size() && end - contentStart <= 65536; ++end) {
        char c = text[end];
        if (c != '{' && c != '}' && c != close.front()) continue;
        if (escaped(text, end)) continue;
        if (c == '{') ++depth;
        if (c == '}' && depth) --depth;
        if (depth || !text.substr(end).starts_with(close)) continue;
        const auto next = end + close.size();
        if (close == "$" && (end == contentStart || white(text[end - 1]) || text[end - 1] == '$' ||
            (next < text.size() && (digit(text[next]) || text[next] == '$')))) continue;
        auto source = trim(text.substr(contentStart, end - contentStart));
        if (!source.empty()) return Formula{source, next, display};
        break;
    }
    return std::nullopt;
}

void Inline::setReferences(References references) {
    if (references_ == references) return;
    references_ = std::move(references);
    inlineCache_.clear();
}

std::optional<std::string_view> Inline::reference(std::string_view key) const {
    auto found = references_.find(referenceKey(key));
    if (found == references_.end()) return std::nullopt;
    return found->second;
}

std::string Inline::math(std::string_view formula, bool display) {
    std::string key(1, display ? 'D' : 'I');
    key += formula;
    if (auto match = mathCache_.find(key); match != mathCache_.end()) return match->second;
    auto result = tex::render(formula, display);
    std::string markup = result.error.empty() ? std::move(result.mathml) :
        "<code class=\"tex-error\" title=\"" + escape(result.error) + "\">" + escape(formula) + "</code>";
    if (markup.size() <= 32768) {
        if (mathCache_.size() >= 512) mathCache_.clear();
        mathCache_.emplace(std::move(key), markup);
    }
    return markup;
}

std::string Inline::render(std::string_view text, bool plain, unsigned depth) {
    if (depth > 20) return plain ? std::string(text) : escape(text);
    const bool cache = !plain && depth == 0 && text.size() <= 4096;
    if (cache) {
        if (auto found = inlineCache_.find(std::string(text)); found != inlineCache_.end()) return found->second;
    }

    std::string output;
    for (std::size_t i = 0; i < text.size();) {
        const char ch = text[i];
        if (ch == '$' || ch == '\\') {
            if (auto formula = formulaAt(text, i)) {
                if (plain) output += formula->source;
                else if (formula->display) output += "<span class=\"inline-display\">" + math(formula->source, true) + "</span>";
                else output += math(formula->source, false);
                i = formula->end;
                continue;
            }
        }
        if (ch == '\\' && i + 1 < text.size() && (punctuation(text[i + 1]) || text[i + 1] == '\n')) {
            char next = text[i + 1];
            if (next == '\n') output += plain ? " " : "<br>";
            else output += plain ? std::string(1, next) : escape(text.substr(i + 1, 1));
            i += 2;
            continue;
        }
        if (ch == 96) {
            auto count = run(text, i, ch);
            auto end = i + count;
            while ((end = text.find(ch, end)) != text.npos) {
                auto length = run(text, end, ch);
                if (length == count) break;
                end += length;
            }
            if (end != text.npos) {
                auto code = std::string(text.substr(i + count, end - i - count));
                std::replace(code.begin(), code.end(), '\n', ' ');
                if (code.size() > 1 && code.front() == ' ' && code.back() == ' ' &&
                    code.find_first_not_of(' ') != code.npos) code = code.substr(1, code.size() - 2);
                output += plain ? code : "<code>" + escape(code) + "</code>";
                i = end + count;
                continue;
            }
            output.append(count, ch);
            i += count;
            continue;
        }
        const bool image = ch == '!' && i + 1 < text.size() && text[i + 1] == '[';
        if (ch == '[' || image) {
            auto start = i + (image ? 2 : 1);
            auto end = matching(text, start, '[', ']');
            if (end != text.npos) {
                auto label = text.substr(start, end - start);
                std::optional<std::string_view> url;
                std::size_t next = end + 1;
                if (auto link = target(text, next)) {
                    url = link->first;
                    next = link->second;
                } else if (next < text.size() && text[next] == '[') {
                    auto stop = closing(text, next + 1, "]");
                    if (stop != text.npos) {
                        auto key = text.substr(next + 1, stop - next - 1);
                        url = reference(key.empty() ? label : key);
                        if (url) next = stop + 1;
                    }
                } else url = reference(label);
                if (url) {
                    if (plain || !allowedURL(*url, image)) output += render(label, plain, depth + 1);
                    else if (image) output += "<img loading=\"lazy\" src=\"" + escape(*url) +
                                              "\" alt=\"" + escape(render(label, true, depth + 1)) + "\">";
                    else output += "<a href=\"" + escape(*url) + "\">" + render(label, false, depth + 1) + "</a>";
                    i = next;
                    continue;
                }
            }
        }
        if (ch == '<') {
            auto end = closing(text, i + 1, ">");
            if (end != text.npos) {
                auto url = text.substr(i + 1, end - i - 1);
                bool web = url.starts_with("https://") || url.starts_with("http://") || url.starts_with("mailto:");
                bool mail = !web && url.find('@') != url.npos && url.find_first_of("/@") == url.find('@');
                if ((web || mail) && url.find_first_of(" \t\n<>") == url.npos && allowedURL(url, false)) {
                    if (plain) output += url;
                    else output += "<a href=\"" + std::string(mail ? "mailto:" : "") + escape(url) + "\">" + escape(url) + "</a>";
                    i = end + 1;
                    continue;
                }
            }
        }
        if (ch == '*' || ch == '_' || ch == '~') {
            auto size = run(text, i, ch);
            auto count = std::min<std::size_t>(size, ch == '~' ? 2 : 3);
            bool opens = i + size < text.size() && !white(text[i + size]) &&
                         (ch != '_' || i == 0 || white(text[i - 1]) || punctuation(text[i - 1]));
            if (opens && (ch != '~' || count == 2)) {
                auto marker = text.substr(i, count);
                auto end = i + size;
                while ((end = closing(text, end, marker)) != text.npos) {
                    auto closingRun = run(text, end, ch);
                    bool boundary = ch != '_' || end + closingRun == text.size() ||
                                    white(text[end + closingRun]) || punctuation(text[end + closingRun]);
                    if (boundary && end > i + size && !white(text[end - 1])) {
                        end += closingRun - count;
                        break;
                    }
                    end += closingRun;
                }
                if (end != text.npos) {
                    auto body = render(text.substr(i + count, end - i - count), plain, depth + 1);
                    if (plain) output += body;
                    else if (ch == '~') output += "<del>" + body + "</del>";
                    else if (count == 1) output += "<em>" + body + "</em>";
                    else if (count == 2) output += "<strong>" + body + "</strong>";
                    else output += "<em><strong>" + body + "</strong></em>";
                    i = end + count;
                    continue;
                }
            }
            output.append(size, ch);
            i += size;
            continue;
        }
        if (ch == '&') {
            if (auto value = entity(text.substr(i))) {
                output += plain ? value->second : value->first;
                i += value->first.size();
                continue;
            }
        }
        if (ch == '\n') {
            if (!plain && output.ends_with("  ")) {
                while (output.ends_with(" ")) output.pop_back();
                output += "<br>";
            } else output += ' ';
        } else if (plain || (ch != '&' && ch != '<' && ch != '>' && ch != '"' && ch != '\'')) output += ch;
        else output += escape(text.substr(i, 1));
        ++i;
    }

    if (cache && output.size() <= 32768) {
        if (inlineCache_.size() >= 2048) inlineCache_.clear();
        inlineCache_.emplace(text, output);
    }
    return output;
}

}
