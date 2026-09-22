#ifndef rwal_internal_h
#define rwal_internal_h

#include <stdint.h>

#include "rwal.h"

int parse_header(const char *buf, struct Header *header);
void initialize_header(char *buf);

const char* readu32le(const char *buf, uint32_t *i);
const char* readu64le(const char *buf, uint64_t *i);

#endif
