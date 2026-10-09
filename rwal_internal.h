#ifndef rwal_internal_h
#define rwal_internal_h

#include <stdint.h>

#include "rwal.h"

void initialize_header(char *buf, uint64_t blocks);
int parse_header(const char *buf, struct Header *header);
void write_header(char *buf, struct Header header);
void encode_rheader(char *buf, uint32_t iseq, uint32_t irnd, const char *bytes, int count);

const char* readu32le(const char *buf, uint32_t *i);
const char* readu64le(const char *buf, uint64_t *i);
char* writeu32le(char *buf, uint32_t val);
char* writeu64le(char *buf, uint64_t val);

uint32_t crc32c(const char *buf, int count);

#endif
