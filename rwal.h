#ifndef rwal_h
#define rwal_h

#include <stdint.h>

/*
   ===Header Format===
   0:	3b magic "RWL"
   3:	1b version
   4:	4b iseq - incarnation sequence number
   8:	4b irnd - random incarnation salt
   12:	8b ioffset - starting incarnation offset
   20:	8b soffset - log start offset
   28:	8b eoffset - log end offset
   36:	4b CRC
   40:	zero padding until end of the block

   The magic identifies the data as RWAL. The version controls both the header
   and the record format. A change in format requires checkpointing log data,
   truncating the log and rewriting the header.

   The start offset points to the first record in the log; end offset points to
   next free offset offset available for the next record. Log is circular - if
   start offset is larger than end offset, records data continues from the
   beginning of the first block (block zero is reserved for the log header).
   Start and end offsets are equal only when the log is empty. If an append
   would result in the end offset exceeding or being equal to the start offset
   than the log is full and must be truncated to free space.

   Truncation starts a new incarnation, which invalidates existing records,
   treated as free space that can be overridden. Records that belong to an
   incarnation are identified by an incremented sequence number combined with a
   random salt that prevents collisions on a wraparound.

   A prefix of the log checkpointed by the application can be truncated, which
   does not introduce a new incarnation or free up space, but reduces the
   number of log records to be reprocessed. Incarnation offset delimits the
   records from the current incarnation - when reached by end offset the log
   must be fully truncated.

   CRC detects log header corruption and is computed from all preceding bytes.
 */

/*
   ===Record Format===
   0:	4b incarnation seq
   4:	4b incarnation rnd
   8:	2b payload length
   10:	4b header CRC
   14:	4b payload CRC
   18:	payload

   The header CRC checksum allows to detect when a record header was written
   partially, for example when it's split across consecutive blocks. It's
   computed from all preceding record header bytes, importantly including
   payload length, and must be verified before reading the record payload.

   Similarly, the payload CRC allows to detect incomplete writes of the
   payload, making it safe to write over block boundaries.
 */

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
