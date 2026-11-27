#include "yaml_config.h"
#include "yaml.hpp"

#include <cstdlib>
#include <cstring>

char *md_yaml_to_json(const char *source, size_t length) {
    constexpr size_t maximum_length = 2 * 1024 * 1024;
    if (!source || length > maximum_length) return nullptr;

    try {
        mira_bpe::yaml::Options options;
        options.max_depth = 64;
        auto yaml = mira_bpe::yaml::load_yaml({source, length}, options);
        if (!yaml.root || yaml.root->kind != mira_bpe::yaml::Node::Mapping) return nullptr;

        // Count expanded nodes, including aliases, before materializing the tree.
        std::vector<const mira_bpe::yaml::Node *> pending{yaml.root};
        size_t remaining = 50000;
        while (!pending.empty()) {
            auto node = pending.back();
            pending.pop_back();
            if (remaining-- == 0 || node->items.size() + pending.size() > remaining) return nullptr;
            pending.insert(pending.end(), node->items.begin(), node->items.end());
        }

        mira_bpe::DumpOptions serialization;
        serialization.ensure_ascii = false;
        serialization.allow_nan = false;
        auto json = mira_bpe::dump_json(mira_bpe::yaml::to_json(*yaml.root), serialization);
        if (json.size() > 2 * maximum_length) return nullptr;
        auto *result = static_cast<char *>(std::malloc(json.size() + 1));
        if (result) std::memcpy(result, json.c_str(), json.size() + 1);
        return result;
    } catch (...) {
        return nullptr;
    }
}

void md_yaml_free(char *value) { std::free(value); }
