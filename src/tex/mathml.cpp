#include "mathml.hpp"

#include <array>

namespace tex::detail {

std::string escape(std::string_view value) {
    std::string result;
    for (char c : value) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t' || c == '\r') result += c;
        }
    }
    return result;
}

std::string element(std::string_view name, std::string_view content, std::string_view attributes) {
    std::string result;
    result.reserve(name.size() * 2 + attributes.size() + content.size() + 6);
    result += '<';
    result += name;
    if (!attributes.empty()) {
        result += ' ';
        result += attributes;
    }
    result += '>';
    result += content;
    result += "</";
    result += name;
    result += '>';
    return result;
}

std::string element(std::string_view name, std::initializer_list<std::string_view> children,
                    std::string_view attributes) {
    std::string content;
    for (auto child : children) content += child;
    return element(name, content, attributes);
}

std::string mathDocument(std::string_view body, std::string_view source, bool display) {
    auto annotation = element("annotation", escape(source), "encoding=\"application/x-tex\"");
    auto semantics = element("semantics", {body, annotation});
    return element("math", semantics, display ? "xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\""
                                            : "xmlns=\"http://www.w3.org/1998/Math/MathML\"");
}

namespace {

std::string utf8(char32_t c) {
    std::string bytes;
    if (c < 0x80) bytes += static_cast<char>(c);
    else if (c < 0x800) {
        bytes += static_cast<char>(0xC0 | (c >> 6));
        bytes += static_cast<char>(0x80 | (c & 63));
    } else {
        if (c >= 0x10000) {
            bytes += static_cast<char>(0xF0 | (c >> 18));
            bytes += static_cast<char>(0x80 | ((c >> 12) & 63));
        } else {
            bytes += static_cast<char>(0xE0 | (c >> 12));
        }
        bytes += static_cast<char>(0x80 | ((c >> 6) & 63));
        bytes += static_cast<char>(0x80 | (c & 63));
    }
    return bytes;
}

struct Alphabet {
    std::string_view name;
    char32_t uppercase;
    char32_t lowercase;
    char32_t digits;
    char32_t greek = 0;
};

constexpr Alphabet alphabets[] = {
    {"bold", 0x1D400, 0x1D41A, 0x1D7CE, 0x1D6A8}, {"italic", 0x1D434, 0x1D44E, 0, 0x1D6E2},
    {"bold-italic", 0x1D468, 0x1D482, 0, 0x1D71C}, {"script", 0x1D49C, 0x1D4B6, 0},
    {"fraktur", 0x1D504, 0x1D51E, 0}, {"double-struck", 0x1D538, 0x1D552, 0x1D7D8},
    {"sans-serif", 0x1D5A0, 0x1D5BA, 0x1D7E2}, {"monospace", 0x1D670, 0x1D68A, 0x1D7F6},
    {"bold-script", 0x1D4D0, 0x1D4EA, 0}, {"bold-fraktur", 0x1D56C, 0x1D586, 0},
    {"sans-serif-bold", 0x1D5D4, 0x1D5EE, 0x1D7EC, 0x1D756},
    {"sans-serif-italic", 0x1D608, 0x1D622, 0}, {"sans-serif-bold-italic", 0x1D63C, 0x1D656, 0, 0x1D790}
};

char32_t letterlikeSymbol(char32_t c) {
    constexpr std::array<std::pair<char32_t, char32_t>, 24> exceptions{{
        {0x1D455, 0x210E}, {0x1D49D, 0x212C}, {0x1D4A0, 0x2130}, {0x1D4A1, 0x2131},
        {0x1D4A3, 0x210B}, {0x1D4A4, 0x2110}, {0x1D4A7, 0x2112}, {0x1D4A8, 0x2133},
        {0x1D4AD, 0x211B}, {0x1D4BA, 0x212F}, {0x1D4BC, 0x210A}, {0x1D4C4, 0x2134},
        {0x1D506, 0x212D}, {0x1D50B, 0x210C}, {0x1D50C, 0x2111}, {0x1D515, 0x211C},
        {0x1D51D, 0x2128}, {0x1D53A, 0x2102}, {0x1D53F, 0x210D}, {0x1D545, 0x2115},
        {0x1D547, 0x2119}, {0x1D548, 0x211A}, {0x1D549, 0x211D}, {0x1D551, 0x2124}
    }};
    for (auto [from, to] : exceptions) if (c == from) return to;
    return c;
}

}

std::string styledIdentifier(std::string_view value, std::string_view variant) {
    for (const auto& alphabet : alphabets) {
        if (alphabet.name != variant) continue;
        if (alphabet.greek && (value.size() == 2 || value == "∇" || value == "∂")) {
            constexpr std::u32string_view glyphs =
                U"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡϴΣΤΥΦΧΨΩ∇αβγδεζηθικλμνξοπρςστυφχψω∂ϵϑϰϕϱϖ";
            for (std::size_t i = 0; i < glyphs.size(); ++i) {
                if (value == utf8(glyphs[i])) return utf8(alphabet.greek + i);
            }
        }
        std::string result;
        for (unsigned char c : value) {
            char32_t styled = 0;
            if (c >= 'A' && c <= 'Z') styled = alphabet.uppercase + c - 'A';
            if (c >= 'a' && c <= 'z') styled = alphabet.lowercase + c - 'a';
            if (c >= '0' && c <= '9' && alphabet.digits) styled = alphabet.digits + c - '0';
            result += styled ? utf8(letterlikeSymbol(styled)) : std::string(1, static_cast<char>(c));
        }
        return result;
    }
    return std::string(value);
}

}
