#define _GNU_SOURCE // O_DIRECT - has to be defined before includes
#include "rwal.h"
#include <assert.h>
#include <fcntl.h> // open
#include <linux/fs.h> // BLKPBSZGET
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // exit, aligned_alloc
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h> // read

#define BUF_SIZE 4096
#define MAGIC_SIZE 4
#define VERSION_SIZE 1
#define HEADER_SIZE 21
static const char init_header[HEADER_SIZE] = {
	// 0x52, 0x57, 0x41, 0x4C
	'R', 'W', 'A', 'L', // magic
	0x0, // version byte
		 // 8 byte LE offsets support up to 16EB large log device.
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // start offset 4096
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // end offset 4096
};

void verify_buffer(int fd, char *buf);
int parse_header(char *buf, struct Header *header);
void write_header(char *buf, struct Header header);
void bread(int fd, char *buf);
void bwrite(int fd, char *buf);
void bseek(int fd, off_t offset);
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count);
void printhex(const char* label, const char *buf, int count);

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s [blockdev]\n", argv[0]);
		exit(1);
	}
	struct Log log = lopen(argv[1]);

	printf("start offset: %lu, end offset: %lu\n",
			log.header.soffset, log.header.eoffset);

	// append two records
	// TODO use lappend_payload
	char record1[] = { 0xde, 0xad, 0xbe, 0xef };
	lappend(&log, record1, sizeof(record1));
	char record2[] = { 0xca, 0xfe, 0xba, 0xbe };
	lappend(&log, record2, sizeof(record2));
	lfsync(log);

	// read the records back
	lrewind(&log, log.header.eoffset - sizeof(record1) - sizeof(record2));
	char rbuf[4];
	lread(&log, rbuf, sizeof(rbuf));
	printhex("record 1", rbuf, sizeof(rbuf));
	lread(&log, rbuf, sizeof(rbuf));
	printhex("record 2", rbuf, sizeof(rbuf));

	// append and then read another record
	char record3[] = { 0xc0, 0xff, 0xee };
	lappend(&log, record3, sizeof(record3));
	lrewind(&log, log.header.eoffset - sizeof(record3));
	lread(&log, rbuf, sizeof(record3));
	printhex("record 3", rbuf, sizeof(record3));

	lclose(log);
}

// === Interface ===

struct Log lopen(char *dev_path)
{
	int fd = open(dev_path, O_RDWR | O_DIRECT);
	if (fd == -1)
	{
		perror("open failed");
		exit(1);
	}

	int pblock_size;
	if (ioctl(fd, BLKPBSZGET, &pblock_size) == -1)
	{
		perror("failed to get physical block size");
		exit(1);
	}

	char *buf = (char *) aligned_alloc(pblock_size, BUF_SIZE);

	// Verify that the buffer satisfies requirements of O_DIRECT with regards to
	// the block size.

	intptr_t bufptr_int = (intptr_t) buf;
	printf("buffer address: 0x%lx\n", bufptr_int);
	// Verify O_DIRECT requirements:
	// buffer size must be mutliple of block size
	assert(BUF_SIZE % pblock_size == 0);
	// buffer must be aligned at the block size
	assert((bufptr_int & (pblock_size - 1)) == 0);

	struct Header header;
	bread(fd, buf);
	if (parse_header(buf, &header) == -1) {
		printf("magic mismatch, preparing a new log device\n");

		memset(buf, 0, BUF_SIZE); // reset the buffer
		memcpy(buf, init_header, HEADER_SIZE);

		bseek(fd, 0);
		bwrite(fd, buf);

		if (fsync(fd) == 1) {
			perror("fsyncing new header failed");
			exit(1);
		}

		parse_header(buf, &header);

		printf("written the log device header block\n");
	}

	// read the current block
	bseek(fd, header.eoffset);
	bread(fd, buf);

	return (struct Log){ header, fd, buf };
}

void lappend(struct Log *log, char *data, int count) {
	if (log->roffset != -1) {
		if (log->header.eoffset / BUF_SIZE != log->roffset / BUF_SIZE) {
			// seek and refill the buffer if the write block differs from the
			// read block
			bseek(log->fd, log->header.eoffset);
			bread(log->fd, log->buf);
		}
		log->roffset = -1; // no longer in read mode
	}
	// TODO ensure file offset is at the end of the log and buffer is filled
	append(log->fd, log->buf, &log->header.eoffset, data, count);
}

void lfsync(struct Log log)
{
	// flush the current block if not full
	// a full block is flushed on append
	if (log.header.eoffset % BUF_SIZE != 0) {
		bwrite(log.fd, log.buf);
	}

	if (fsync(log.fd) == 1) {
		perror("lfscyn");
		exit(1);
	}
}

// Set the offset for subsequent reads and fill the buffer.
void lrewind(struct Log *log, uint64_t offset)
{
	log->roffset = offset;
	if (offset / BUF_SIZE != log->header.eoffset / BUF_SIZE) {
		bseek(log->fd, log->header.eoffset);
		bwrite(log->fd, log->buf);
		bseek(log->fd, offset);
		bread(log->fd, log->buf);
	}
}

// TODO move in-buffer read-position
void lread(struct Log *log, char *buf, int count)
{
	memcpy(buf, log->buf + (log->roffset % BUF_SIZE), count);
	log->roffset += count;
}

void lclose(struct Log log)
{
	// TODO only if started
	// flush the current block
	bseek(log.fd, log.header.eoffset);
	bwrite(log.fd, log.buf);

	// checkpoint - update header and flush
	memset(log.buf, 0, BUF_SIZE);
	write_header(log.buf, log.header);
	bseek(log.fd, 0);
	bwrite(log.fd, log.buf);

	if (fsync(log.fd) == 1) {
		perror("lclose fsync");
		exit(1);
	}

	printf("final start offset: %lu, end offset: %lu\n",
			log.header.soffset, log.header.eoffset);

	if (close(log.fd) == 1) {
		perror("an error on closing file");
		exit(1);
	}
}

// === Internals ===

int parse_header(char *buf, struct Header *header)
{
	printhex("read magic", buf, MAGIC_SIZE);
	if (strncmp(buf, init_header, MAGIC_SIZE) != 0) return -1;

	int boffset = MAGIC_SIZE;
	header->version = buf[boffset];
	boffset += 1;

	uint64_t soffset = 0;
	for (int i = 0; i < sizeof(soffset); i++) {
		soffset += buf[boffset+i] << i * 8;
	}
	header->soffset = soffset;

	boffset += 8;
	uint64_t eoffset = 0;
	for (int i = 0; i < sizeof(eoffset); i++) {
		eoffset += buf[boffset+i] << i * 8;
	}
	header->eoffset = eoffset;

	return 0;
}

void write_header(char *buf, struct Header header) {
	memcpy(buf, init_header, HEADER_SIZE);
	int boffset = MAGIC_SIZE + VERSION_SIZE;
	uint64_t soffset = header.soffset;
	for (int i = 0; i < sizeof(soffset); i++) {
		buf[boffset++] = soffset & 0xff;
		soffset >>= 8;
	}
	uint64_t eoffset = header.eoffset;
	for (int i = 0; i < sizeof(eoffset); i++) {
		buf[boffset++] = eoffset & 0xff;
		eoffset >>= 8;
	}
}

// Read one block of data (BUF_SIZE bytes) from fd into buf, which is asummed
// to have a size of least BUF_SIZE.
void bread(int fd, char *buf)
{
	ssize_t bytes_read = read(fd, buf, BUF_SIZE);
	if (bytes_read == -1)
	{
		perror("bread");
		exit(1);
	}
	// This generally shouldn't happen, unless the block device is too small or
	// the read was interrupted by a signal.
	assert(bytes_read == BUF_SIZE);
}

// Write one block of data from buf (BUF_SIZE bytes) to fd. The buffer size is
// assumed to be at least BUF_SIZE.
void bwrite(int fd, char *buf)
{
	ssize_t bytes_written = write(fd, buf, BUF_SIZE);
	if (bytes_written == -1) {
		perror("bwrite");
		exit(1);
	}
	assert(bytes_written == BUF_SIZE);
}

// Seek over to the start of the current block.
void bseek(int fd, off_t offset)
{
	if (lseek(fd, offset / BUF_SIZE * BUF_SIZE, SEEK_SET) == -1) {
		perror("bseek");
		exit(1);
	}
}

// Append count bytes to fd at offset. The buffer is assumed to already contain
// the data up to offset % BUF_SIZE and have the size of exactly BUF_SIZE. When
// the buffer is filled in, it's written to the device. A partially filled
// buffer remains not flushed.
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count)
{
	// fill the rest of the buffer
	ssize_t buf_offset = *offset % BUF_SIZE;
	ssize_t buf_free = BUF_SIZE - buf_offset;
	assert(buf_offset + buf_free == BUF_SIZE);
	ssize_t to_copy = count % (buf_free + 1); // up to buf_free bytes
	assert(buf_offset + to_copy <= BUF_SIZE);
	memcpy(buf + buf_offset, bytes, to_copy);
	printf("to_copy: %ld at buf_offset: %ld, buf_free: %ld\n", to_copy,
			buf_offset, buf_free);
	// flush the buffer if full
	if (buf_free == to_copy) {
		bseek(fd, *offset);
		bwrite(fd, buf);
	}

	*offset += count; // the final offset

	count -= to_copy;
	bytes += to_copy;

	// writes bytes in BUF_SIZE chunks to directly to disk
	while (count / BUF_SIZE > 0) {
		bwrite(fd, bytes);
		count -= BUF_SIZE;
		bytes += BUF_SIZE;
	}

	// buffer the remaining data if any
	if (count > 0) {
		memset(buf, 0, BUF_SIZE);
		memcpy(buf, bytes, count);
	}
}

void printhex(const char *label, const char *buf, int count)
{
	printf("%s: ", label);
	for (int i = 0; i < count; i++)
	{
		if (!(i & 1)) printf("0x");
		printf("%x", (unsigned char) buf[i]);
		if (i & 1) putchar(' ');
	}
	putchar('\n');
}
