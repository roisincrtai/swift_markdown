#ifndef SWIFT_MARKDOWN_YAML_CONFIG_H
#define SWIFT_MARKDOWN_YAML_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The caller owns the returned UTF-8 string and frees it with md_yaml_free.
char *md_yaml_to_json(const char *source, size_t length);
void md_yaml_free(char *value);

#ifdef __cplusplus
}
#endif

#endif
