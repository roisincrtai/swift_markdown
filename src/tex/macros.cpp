#include "macros.hpp"

#include <optional>
#include <unordered_map>

namespace tex::detail {
namespace {

struct Macro {
    unsigned arguments = 0;
    std::optional<Tokens> defaultArgument;
    Tokens body;
};

class Expander {
public:
    Tokens run(const Tokens& tokens) {
        // Expansion consumes a shared stream so a replacement can take arguments
        // from the tokens following the invocation.
        pending_.reserve(tokens.size());
        for (auto i = tokens.rbegin(); i != tokens.rend(); ++i) pending_.push_back(*i);
        scopes_.emplace_back();
        Tokens result;
        while (!pending_.empty()) {
            Token token = take();
            if (token.command("begingroup") || token.command("bgroup")) token = {Kind::character, "{"};
            if (token.command("endgroup") || token.command("egroup")) token = {Kind::character, "}"};
            if (token.is("{")) scopes_.emplace_back();
            if (token.is("}") && scopes_.size() > 1) scopes_.pop_back();

            if (token.command("newcommand") || token.command("renewcommand") ||
                token.command("providecommand") || token.command("DeclareMathOperator") ||
                token.command("def") || token.command("gdef")) {
                define(token.text);
            } else if (token.command("let")) {
                alias();
            } else if (token.kind == Kind::command) {
                if (const Macro* macro = lookup(token.text)) substitute(*macro);
                else result.push_back(std::move(token));
            } else {
                result.push_back(std::move(token));
            }
        }
        return result;
    }

private:
    const Macro* lookup(const std::string& name) const {
        for (auto i = scopes_.rbegin(); i != scopes_.rend(); ++i) {
            auto found = i->find(name);
            if (found != i->end()) return &found->second;
        }
        return nullptr;
    }

    bool at(std::string_view value) const {
        return !pending_.empty() && pending_.back().is(value);
    }

    Token take() {
        if (pending_.empty()) throw Error("Incomplete macro");
        if (++steps_ > 100000) throw Error("Macro expansion limit exceeded");
        Token token = std::move(pending_.back());
        pending_.pop_back();
        return token;
    }

    void skipSpace() {
        while (!pending_.empty() && pending_.back().kind == Kind::space) take();
    }

    Tokens group(std::string_view open, std::string_view close) {
        if (!at(open)) throw Error("Expected " + std::string(open) + " in macro");
        take();
        unsigned depth = 1;
        unsigned braces = 0;
        Tokens result;
        while (!pending_.empty()) {
            Token token = take();
            if (open == "[") {
                if (token.is("{")) ++braces;
                if (token.is("}") && braces) --braces;
            }
            if (!braces && token.is(open)) ++depth;
            if (!braces && token.is(close) && --depth == 0) return result;
            result.push_back(std::move(token));
        }
        throw Error("Unclosed macro argument");
    }

    static unsigned digit(const Token& token) {
        if (token.kind != Kind::character || token.text.size() != 1 ||
            token.text[0] < '0' || token.text[0] > '9') throw Error("Expected a macro parameter number");
        return static_cast<unsigned>(token.text[0] - '0');
    }

    void define(const std::string& kind) {
        skipSpace();
        const bool primitive = kind == "def" || kind == "gdef";
        const bool starred = !primitive && at("*");
        if (starred) take();
        skipSpace();
        Token name;
        if (!primitive && at("{")) {
            auto tokens = group("{", "}");
            if (tokens.size() != 1) throw Error("Expected one macro name");
            name = std::move(tokens.front());
        } else {
            name = take();
        }
        if (name.kind != Kind::command) throw Error("Expected a macro command name");

        Macro macro;
        skipSpace();
        if (primitive) {
            while (at("#")) {
                take();
                if (digit(take()) != ++macro.arguments) throw Error("Expected sequential macro parameters");
                skipSpace();
            }
        } else if (at("[")) {
            auto count = group("[", "]");
            if (count.size() != 1) throw Error("Expected a single macro argument count");
            macro.arguments = digit(count.front());
            skipSpace();
            if (macro.arguments && at("[")) macro.defaultArgument = group("[", "]");
        }
        skipSpace();
        macro.body = group("{", "}");
        if (kind == "providecommand" && lookup(name.text)) return;
        if (kind == "DeclareMathOperator") {
            macro.body.insert(macro.body.begin(), {Kind::character, "{"});
            macro.body.insert(macro.body.begin(), {Kind::command, "operatorname"});
            if (starred) macro.body.insert(macro.body.begin() + 1, {Kind::character, "*"});
            macro.body.push_back({Kind::character, "}"});
        }
        if (kind == "gdef") {
            for (auto scope = scopes_.begin() + 1; scope != scopes_.end(); ++scope) scope->erase(name.text);
            scopes_.front().insert_or_assign(std::move(name.text), std::move(macro));
        } else {
            scopes_.back().insert_or_assign(std::move(name.text), std::move(macro));
        }
    }

    void alias() {
        skipSpace();
        auto name = take();
        if (name.kind != Kind::command) throw Error("Expected an alias command name");
        skipSpace();
        if (at("=")) take();
        skipSpace();
        auto definition = take();
        Macro macro;
        if (definition.kind == Kind::command) {
            if (const auto* existing = lookup(definition.text)) macro = *existing;
            else macro.body.push_back(definition);
        } else {
            if (definition.is("#")) macro.body.push_back(definition);
            macro.body.push_back(definition);
        }
        scopes_.back().insert_or_assign(std::move(name.text), std::move(macro));
    }

    void substitute(const Macro& macro) {
        std::vector<Tokens> args;
        if (macro.defaultArgument) {
            skipSpace();
            args.push_back(at("[") ? group("[", "]") : *macro.defaultArgument);
        }
        while (args.size() < macro.arguments) {
            skipSpace();
            if (at("}")) throw Error("Missing macro argument");
            if (at("{")) args.push_back(group("{", "}"));
            else args.push_back({take()});
        }

        Tokens replacement;
        for (std::size_t i = 0; i < macro.body.size(); ++i) {
            if (!macro.body[i].is("#")) {
                replacement.push_back(macro.body[i]);
                continue;
            }
            if (++i == macro.body.size()) throw Error("Missing macro parameter number");
            if (macro.body[i].is("#")) {
                replacement.push_back(macro.body[i]);
                continue;
            }
            unsigned index = digit(macro.body[i]);
            if (index == 0 || index > args.size()) throw Error("Macro parameter is out of range");
            const auto& argument = args[index - 1];
            if (argument.size() + replacement.size() > 100000) throw Error("Macro expansion limit exceeded");
            replacement.insert(replacement.end(), argument.begin(), argument.end());
        }
        if (pending_.size() + replacement.size() > 100000) throw Error("Macro expansion limit exceeded");
        for (auto i = replacement.rbegin(); i != replacement.rend(); ++i) pending_.push_back(std::move(*i));
    }

    std::vector<Token> pending_;
    std::vector<std::unordered_map<std::string, Macro>> scopes_;
    std::size_t steps_ = 0;
};

}

Tokens expand(const Tokens& tokens) { return Expander().run(tokens); }

}
