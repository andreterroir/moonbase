#ifndef rwal_internal_h
#define rwal_internal_h

#include <stdint.h>

#include "rwal.h"

void initialize_header(char *buf);
int parse_header(const char *buf, struct Header *header);
void write_header(char *buf, struct Header header);

const char* readu32le(const char *buf, uint32_t *i);
const char* readu64le(const char *buf, uint64_t *i);
char* writeu32le(char *buf, uint32_t val);
char* writeu64le(char *buf, uint64_t val);

#endif
