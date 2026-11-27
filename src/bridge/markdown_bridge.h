#ifndef SWIFT_MARKDOWN_BRIDGE_H
#define SWIFT_MARKDOWN_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MDRenderer MDRenderer;
typedef struct MDResult MDResult;

MDRenderer *md_renderer_create(void);
void md_renderer_destroy(MDRenderer *renderer);
MDResult *md_render(MDRenderer *renderer, const char *source, size_t length);
void md_result_destroy(MDResult *result);

size_t md_block_count(const MDResult *result);
const char *md_block(const MDResult *result, size_t index);
size_t md_heading_count(const MDResult *result);
const char *md_heading_id(const MDResult *result, size_t index);
const char *md_heading_title(const MDResult *result, size_t index);
unsigned md_heading_level(const MDResult *result, size_t index);
size_t md_heading_line(const MDResult *result, size_t index);

#ifdef __cplusplus
}
#endif

#endif
