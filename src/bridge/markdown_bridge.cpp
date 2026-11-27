#include "markdown_bridge.h"
#include "markdown.hpp"

struct MDRenderer { markdown::Renderer renderer; };
struct MDResult { markdown::Document document; };

MDRenderer *md_renderer_create() {
    try { return new MDRenderer; }
    catch (...) { return nullptr; }
}

void md_renderer_destroy(MDRenderer *renderer) { delete renderer; }

MDResult *md_render(MDRenderer *renderer, const char *source, size_t length) {
    if (!renderer || !source) return nullptr;
    try { return new MDResult{renderer->renderer.render({source, length})}; }
    catch (...) { return nullptr; }
}

void md_result_destroy(MDResult *result) { delete result; }

size_t md_block_count(const MDResult *result) { return result->document.blocks.size(); }
const char *md_block(const MDResult *result, size_t index) { return result->document.blocks[index].c_str(); }
size_t md_heading_count(const MDResult *result) { return result->document.outline.size(); }
const char *md_heading_id(const MDResult *result, size_t index) { return result->document.outline[index].id.c_str(); }
const char *md_heading_title(const MDResult *result, size_t index) { return result->document.outline[index].title.c_str(); }
unsigned md_heading_level(const MDResult *result, size_t index) { return result->document.outline[index].level; }
size_t md_heading_line(const MDResult *result, size_t index) { return result->document.outline[index].line; }
