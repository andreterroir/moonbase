#ifndef rwal_h
#define rwal_h

#include <stdint.h>

/*
   LOG FORMAT

   All reads and writes are multiples of and memory aligned to the physical
   block size - a requirement imposed by O_DIRECT. WAL is written and read in
   logical blocks (further referred to as "block"), at least as large as the
   physical block. Block zero is reserved for the log header and any future
   extensions, such as index. The remaining blocks are used for WAL records
   written sequentially. The header fits within a single *physical* block which
   is assumed to be written atomically by the storage device. The records are
   allowed to be split across block boundaries. The record blocks are treated
   as continuous circular space without padding. Upon reaching the end of the
   last block, the data continues at the beginning of the first block.

   HEADER LAYOUT


   // TODO colocate with the struct
   0:	3b magic "RWL"
   3:	1b version (currently 1)
   4:	4b iseq - incarnation sequence number
   8:	4b irnd - random incarnation salt
   12:	8b blocks - size of the device in blocks
   20:	8b ioffset - starting incarnation offset
   28:	8b soffset - log start offset
   36:	8b eoffset - log end offset
   44:	4b crc
   48:	zero padding until end of the block

   Multi-byte values are stored in little-endian byte order. Given that
   most contemporary CPU architectures are LE, it allows for a future
   optimization to rely on the native byte order.

   LOG SEMANTICS

   The magic identifies the data as RWAL. The version (currently 1) controls
   both the header and the record format. A change in format requires
   checkpointing log data, truncating the log and rewriting the header.

   The start offset points to the first record in the log. End offset points to
   the offset after the last record. When the log is initialized both offsets
   point to the beginning of the first block. Start and end offsets are equal
   only when the log is empty.

   The header is not updated on each append - in a steady state the current end
   offset is tracked in memory. The on-disk value is out of date - log contains
   record data until at least the end offset recorded in the header. After
   restart, to find the next append offset, the application must read the log
   starting from the header end offset until the first invalid record. The end
   offset allows to find the append position without reprocessing the log from
   the beginning.

   Valid log records must match the log incarnation identified by an
   incremented sequence number combined with a salt (non-zero and randomized
   each incarnation), that prevents collisions on a wraparound. The initial
   incarnation is 0. Log truncation starts a new incarnation, which invalidates
   existing records, treated as free space that can be overridden.

   A prefix of the log checkpointed by the application can be trimmed, which
   moves the start offset without introducing a new incarnation or freeing up
   space, reducing the number of log records to be reprocessed. The incarnation
   offset delimits records from the current incarnation and is equal to the
   start offset when it began. When the end offset reaches the incarnation
   offset, the log must be fully truncated.

   CRC detects the log header corruption and is computed from all preceding
   bytes.

   RECORD LAYOUT

   0:	4b iseq - incarnation sequence number
   4:	4b irnd - random incarnation salt
   8:	4b plen - payload length
   12:	4b hcrc - header CRC
   16:	4b pcrc - payload CRC
   20:	payload

   The header CRC checksum allows to detect when a record header was written
   partially, for example when it's split across consecutive blocks. It's
   computed from all preceding record header bytes, importantly including
   payload length, and must be verified before reading the record payload.

   Similarly, the payload CRC allows to detect incomplete writes of the
   payload, making it safe to write over block boundaries.

   INVARIANTS

   - An initial header durable before any records are appended.
   - Record appends are durable before header.
   - An updated header is durable on a new incarnation.
   - An application must durably checkpoint the data before trimming or
   truncating the log.
   - Records from the current incarnation are never overwritten.
 */

// TODO rename
#define BUF_SIZE 4096 // WAL block size

#define MAGIC_SIZE 3
#define VERSION_SIZE 1
#define IRND_SIZE 4
#define IRND_OFFSET 8
#define BLOCKS_OFFSET 12
#define CRC_OFFSET 40
#define HEADER_SIZE 48

extern const char init_header[];

struct Header {
	uint8_t version;
	uint32_t iseq;
	uint32_t irnd;
	uint64_t blocks;
	uint64_t ioffset;
	uint64_t soffset;
	uint64_t eoffset;
	uint32_t crc;
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
void ltruncate(struct Log *log, uint64_t offset);
// Fsync the log and free the resources.
void lclose(struct Log log);

void printhex(const char* label, const char *buf, int count);

#endif
