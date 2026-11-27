#include "yaml_config.h"

#include <cstring>
#include <iostream>
#include <string_view>

int main() {
    constexpr std::string_view yaml = R"(
window: {x: 25.5, y: -16, width: 1080, height: 730}
recent_files:
  - path: "/Users/Study/Notes #2.md"
    bookmark: "AXZ+F8=="
last_opened: &last "/Users/Study/Notes #2.md"
views:
  - file: *last
    preview: {block_index: 35, offset: -18.5}
    editor: {location: 26, length: 1}
)";
    char *result = md_yaml_to_json(yaml.data(), yaml.size());
    const bool encoded = result && std::strstr(result, R"("last_opened": "/Users/Study/Notes #2.md")") &&
        std::strstr(result, R"("location": 26)") && std::strstr(result, R"("offset": -18.5)");
    md_yaml_free(result);

    constexpr std::string_view malformed = "recent: [";
    constexpr std::string_view cyclic = "recent: &list [*list]\n";
    constexpr std::string_view nonMapping = "- a\n- b";
    if (!encoded || md_yaml_to_json(malformed.data(), malformed.size()) ||
        md_yaml_to_json(cyclic.data(), cyclic.size()) ||
        md_yaml_to_json(nonMapping.data(), nonMapping.size())) {
        std::cerr << "YAML settings conversion or validation failed\n";
        return 1;
    }
}
