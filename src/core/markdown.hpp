#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace markdown {

struct Heading {
    std::string id;
    std::string title;
    unsigned level;
    std::size_t line;
};

struct Document {
    std::vector<std::string> blocks;
    std::vector<Heading> outline;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Document render(std::string_view source);

private:
    struct Cache;
    std::unique_ptr<Cache> cache_;
};

}
