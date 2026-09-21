#ifndef rwal_internal_h
#define rwal_internal_h

void initialize_header(char *buf);

uint64_t readle(const char *buf, int count);

#endif
