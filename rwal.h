#ifndef rwal_h
#define rwal_h

#include <stdint.h>

struct Header {
	uint8_t version;
	// start of the log
	uint64_t soffset;
	// next record offset
	uint64_t eoffset;
};

struct Log {
	struct Header header;
	int fd;
	char *buf;
	// -1 when the log is not in a read mode
	int64_t roffset;
};

/*
   ===Record Format===
   0: 1b magic
   1: 1b version
   2: 4b epoch
   6: 2b payload length
   8: 4b CRC
   12: payload
 */

// Open an existing or initialize a new log device.
struct Log lopen(char *dev_path);
// Serialize payload into a record, computing the header.
void lappend_payload(struct Log *log, char *data, int count);
// Append serialized data directly (e.g. record header or already serialized record).
void lappend(struct Log *log, char *data, int count);
// Append data from socket, assuming the header is appended already or is a
// part of the data stream.
void lappend_from(struct Log *log, int fd_in, int count);
// Flush the buffer and disk cache.
void lfsync(struct Log log);
// Position the log offset for subsequent reads until an append. Does not
// change the append offset - data is always appended at the end of the log.
// which must be between soffset and eoffset (circular), at the beginning of a record.
// TODO support logical record offset instead?
void lrewind(struct Log *log, uint64_t offset);
void lread(struct Log *log, char *buf, int count);
// Discards the log data until the offset.
// Invariant: offset is between soffset and eoffset in the circular file.
// assert((soffset <= eoffset && offset > soffset && offset <= eoffset)
// || (soffset > eoffset && (offset <= soffset || offset > eoffset))
void ltruncate(struct Log *log, uint64_t offset);
// Fsync the log and free the resources.
void lclose(struct Log log);

#endif
