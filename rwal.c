#define _GNU_SOURCE // O_DIRECT - has to be defined before includes

#include <assert.h>
#include <fcntl.h> // open
#include <linux/fs.h> // BLKPBSZGET
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // exit, aligned_alloc
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h> // statx
#include <unistd.h> // read

#include "rwal.h"
#include "rwal_internal.h"

const char init_header[HEADER_SIZE] = {
	0x52, 0x57, 0x4C, // magic: "RWL"
	0x01, // version: 1
	0x00, 0x00, 0x00, 0x00, // iseq: 0
	0xAA, 0xAA, 0xAA, 0xAA, // irnd: placeholder
	0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, // blocks: placeholder
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // ioffset: 4096
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // soffset: 4096
	0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // eoffset: 4096
	0xAA, 0xAA, 0xAA, 0xAA, // crc: placeholder
};

void write_header(char *buf, struct Header header);
void bread(int fd, char *buf);
void bwrite(int fd, char *buf);
void bseek(int fd, off_t offset);
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count);

// === Interface ===

struct Log lopen(char *dev_path)
{
	int fd = open(dev_path, O_RDWR | O_DIRECT);
	if (fd == -1)
	{
		perror("open failed");
		exit(1);
	}

	int block_size;
	if (ioctl(fd, BLKSSZGET, &block_size) == -1)
	{
		perror("failed to get logical block size");
		exit(1);
	}
	printf("logical block size: %d\n", block_size);

	struct statx stats;
	if (statx(0, dev_path, 0, STATX_DIOALIGN, &stats) == -1) {
		perror("failed to get O_DIRECT alignment requirements");
		exit(1);
	}
	if (stats.stx_dio_mem_align == 0) {
		printf("direct I/O is not supported for %s\n", dev_path);
		exit(2);
	}
	printf("DIO buffer alignment: %d\n", stats.stx_dio_mem_align);
	printf("DIO offset alignement: %d\n", stats.stx_dio_offset_align);

	char *buf = (char *) aligned_alloc(stats.stx_dio_mem_align, BSIZE);

	intptr_t bufptr_int = (intptr_t) buf;
	printf("buffer address: 0x%lx\n", bufptr_int);

	// Verify direct I/O requirements:
	// buffer size must be mutliple of block size
	assert(BSIZE % block_size == 0);
	// buffer must be aligned at the block size
	assert((bufptr_int & (block_size - 1)) == 0);

	struct Header header;
	bread(fd, buf);
	if (parse_header(buf, &header) == -1) {
		printf("magic mismatch, preparing a new log device\n");

		unsigned long long device_bytes;
		if (ioctl(fd, BLKGETSIZE64, &device_bytes) == -1)
		{
			perror("failed to get the size of the log device");
			exit(1);
		}
		printf("device size: %llu bytes\n", device_bytes);
		uint64_t blocks = device_bytes / BSIZE;

		initialize_header(buf, blocks);
		parse_header(buf, &header);

		bseek(fd, 0);
		bwrite(fd, buf);
		if (fsync(fd) == -1) {
			perror("fsyncing new header failed");
			exit(1);
		}

		printf("written the log device header block\n");
	}

	// read the current block
	bseek(fd, header.eoffset);
	bread(fd, buf);

	return (struct Log){ header, fd, buf };
}

void lappend(struct Log *log, char *data, int count) {
	if (log->roffset != -1) {
		if (log->header.eoffset / BSIZE != log->roffset / BSIZE) {
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
	if (log.header.eoffset % BSIZE != 0) {
		bwrite(log.fd, log.buf);
	}

	if (fsync(log.fd) == -11) {
		perror("lfscyn");
		exit(1);
	}
}

// Set the offset for subsequent reads and fill the buffer.
void lrewind(struct Log *log, uint64_t offset)
{
	log->roffset = offset;
	if (offset / BSIZE != log->header.eoffset / BSIZE) {
		bseek(log->fd, log->header.eoffset);
		bwrite(log->fd, log->buf);
		bseek(log->fd, offset);
		bread(log->fd, log->buf);
	}
}

// TODO move in-buffer read-position
void lread(struct Log *log, char *buf, int count)
{
	memcpy(buf, log->buf + (log->roffset % BSIZE), count);
	log->roffset += count;
}

void lclose(struct Log log)
{
	// TODO only if started
	// flush the current block
	bseek(log.fd, log.header.eoffset);
	bwrite(log.fd, log.buf);

	// checkpoint - update header and flush
	memset(log.buf, 0, BSIZE);
	write_header(log.buf, log.header);
	bseek(log.fd, 0);
	bwrite(log.fd, log.buf);

	if (fsync(log.fd) == -1) {
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

int parse_header(const char *buf, struct Header *header)
{
	printhex("read magic", buf, MAGIC_SIZE);
	if (strncmp(buf, init_header, MAGIC_SIZE) != 0) return -1;

	buf += MAGIC_SIZE;
	header->version = buf[0];
	buf += VERSION_SIZE;

	buf = readu32le(buf, &header->iseq);
	// TODO assert non-zero salt
	buf = readu32le(buf, &header->irnd);
	buf = readu64le(buf, &header->blocks);
	buf = readu64le(buf, &header->ioffset);
	buf = readu64le(buf, &header->soffset);
	buf = readu64le(buf, &header->eoffset);
	buf = readu32le(buf, &header->crc);

	return 0;
}

uint64_t readle(const char *buf, int count)
{
	assert(count <= sizeof(uint64_t));
	uint64_t res = 0;
	for (int i = 0; i < count; i++) {
		res |= (uint64_t) (unsigned char)buf[i] << i * 8;
	}
	return res;
}

const char* readu32le(const char *buf, uint32_t *i)
{
	*i = readle(buf, sizeof(uint32_t));
	return buf + sizeof(uint32_t);
}

const char* readu64le(const char *buf, uint64_t *i)
{
	*i = readle(buf, sizeof(uint64_t));
	return buf + sizeof(uint64_t);
}

void initialize_header(char *buf, uint64_t device_blocks)
{
	memset(buf, 0, BSIZE);
	memcpy(buf, init_header, HEADER_SIZE);

	// generate a non-zero incarnation salt
	long r; // long is at least 32 bits
	while ((r = random()) == 0);
	writeu32le(buf + IRND_OFFSET, r);

	// device size
	writeu64le(buf + BLOCKS_OFFSET, device_blocks);

	// TODO compute crc
}

char* writele(char *buf, uint64_t val, int count)
{
	assert(count <= sizeof(uint64_t));
	for (int i = 0; i < count; i++) {
		buf[i] = (char) (val >> i * 8);
	}
	return buf + count;
}

char* writeu32le(char *buf, uint32_t val)
{
	return writele(buf, val, sizeof(uint32_t));
}

char* writeu64le(char *buf, uint64_t val)
{
	return writele(buf, val, sizeof(uint64_t));
}

void write_header(char *buf, struct Header header) {
	memcpy(buf, init_header, HEADER_SIZE);
	char *bp = buf;
	bp += MAGIC_SIZE + VERSION_SIZE;
	bp = writeu32le(bp, header.iseq);
	bp = writeu32le(bp, header.irnd);
	bp = writeu64le(bp, header.blocks);
	bp = writeu64le(bp, header.ioffset);
	bp = writeu64le(bp, header.soffset);
	bp = writeu64le(bp, header.eoffset);
	bp = writeu32le(bp, header.crc);
}

// Read one block of data (BSIZE bytes) from fd into buf, which is asummed
// to have a size of least BSIZE.
void bread(int fd, char *buf)
{
	ssize_t bytes_read = read(fd, buf, BSIZE);
	if (bytes_read == -1)
	{
		perror("bread");
		exit(1);
	}
	// This generally shouldn't happen, unless the block device is too small or
	// the read was interrupted by a signal.
	assert(bytes_read == BSIZE);
}

// Write one block of data from buf (BSIZE bytes) to fd. The buffer size is
// assumed to be at least BSIZE.
void bwrite(int fd, char *buf)
{
	ssize_t bytes_written = write(fd, buf, BSIZE);
	if (bytes_written == -1) {
		perror("bwrite");
		exit(1);
	}
	assert(bytes_written == BSIZE);
}

// Seek over to the start of the current block.
void bseek(int fd, off_t offset)
{
	if (lseek(fd, offset / BSIZE * BSIZE, SEEK_SET) == -1) {
		perror("bseek");
		exit(1);
	}
}

// Append count bytes to fd at offset. The buffer is assumed to already contain
// the data up to offset % BSIZE and have the size of exactly BSIZE. When
// the buffer is filled in, it's written to the device. A partially filled
// buffer remains not flushed.
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count)
{
	// fill the rest of the buffer
	ssize_t buf_offset = *offset % BSIZE;
	ssize_t buf_free = BSIZE - buf_offset;
	assert(buf_offset + buf_free == BSIZE);
	ssize_t to_copy = count % (buf_free + 1); // up to buf_free bytes
	assert(buf_offset + to_copy <= BSIZE);
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

	// writes bytes in BSIZE chunks to directly to disk
	while (count / BSIZE > 0) {
		bwrite(fd, bytes);
		count -= BSIZE;
		bytes += BSIZE;
	}

	// buffer the remaining data if any
	if (count > 0) {
		memset(buf, 0, BSIZE);
		memcpy(buf, bytes, count);
	}
}

void printhex(const char *label, const char *buf, int count)
{
	printf("%s: ", label);
	for (int i = 0; i < count; i++)
	{
		if (!(i & 1)) printf("0x");
		printf("%02X", (unsigned char) buf[i]);
		if (i & 1) putchar(' ');
	}
	putchar('\n');
}
