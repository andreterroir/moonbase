#define _GNU_SOURCE // O_DIRECT - has to be defined before includes
#include <assert.h>
#include <fcntl.h> // open
#include <linux/fs.h> // BLKPBSZGET
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // exit
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h> // read

#define BUF_SIZE 4096
#define MAGIC_SIZE 4
#define HEADER_SIZE 21
static const char init_header[HEADER_SIZE] = {
	// 0x52, 0x57, 0x41, 0x4C
	'R', 'W', 'A', 'L', // magic
	0x0, // version byte
		 // 8 byte LE offsets support up to 16EB large log device.
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // start offset
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // end offset
};

struct Header {
	uint8_t version;
	// start of the log
	uint64_t soffset;
	// next record offset
	uint64_t eoffset;
};

void verify_buffer(int fd, char *buf);
int parse_header(char *buf, struct Header *header);
void bread(int fd, char *buf);
void bwrite(int fd, char *buf);
void bseek(int fd, off_t offset);
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count);
void printhex(const char* label, const char *buf, int count);

int main(int argc, char *argv[])
{
	// Enforce stricter buffer alignment than most physical block sizes, most
	// often 512.
	// TODO will the buffer take stack space or be allocated statically?
	char buf[BUF_SIZE] __attribute__((aligned (4096)));

	if (argc != 2) {
		fprintf(stderr, "usage: %s [blockdev]\n", argv[0]);
		exit(1);
	}
	const char *dev_path = argv[1];

	int fd = open(dev_path, O_RDWR | O_DIRECT);
	if (fd == -1)
	{
		perror("open failed");
		exit(1);
	}

	verify_buffer(fd, buf);

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

	printf("start offset: %lu, end offset: %lu\n",
			header.soffset, header.eoffset);

	bseek(fd, header.eoffset);
	// read the current block
	bread(fd, buf);

	// append two records
	char record1[] = { 0xde, 0xad, 0xbe, 0xef };
	append(fd, buf, &header.eoffset, record1,
			sizeof(record1));
	char record2[] = { 0xca, 0xfe, 0xba, 0xbe };
	append(fd, buf, &header.eoffset, record2,
			sizeof(record2));

	// flush the current block
	// overwrite the current block
	bseek(fd, header.eoffset);
	bwrite(fd, buf);

	// read the records back
	memset(buf, 0, BUF_SIZE);
	bseek(fd, header.eoffset);
	bread(fd, buf);
	char rbuf[4];
	memcpy(rbuf, buf, 4);
	printhex("record 1", rbuf, sizeof(rbuf));
	memcpy(rbuf, buf + 4, 4);
	printhex("record 2", rbuf, sizeof(rbuf));

	// checkpoint - update header and flush
	// seek to the header
	bseek(fd, 0);
	bread(fd, buf);
	int boffset = MAGIC_SIZE + 1 +
		sizeof(header.soffset);
	uint64_t eoffset = header.eoffset;
	for (int i = 0; i < sizeof(eoffset); i++) {
		buf[boffset++] = eoffset & 0xff;
		eoffset >>= 8;
	}
	bseek(fd, 0);
	bwrite(fd, buf);

	if (close(fd) == 1) {
		perror("an error on closing file");
		exit(1);
	}
}

// Verify that the buffer satisfies requirements of O_DIRECT with regards to
// the block size.
void verify_buffer(int fd, char *buf)
{
	int pblock_size;
	if (ioctl(fd, BLKPBSZGET, &pblock_size) == -1)
	{
		perror("failed to get physical block size");
		exit(1);
	}
	printf("physical block size for is %d\n", pblock_size);

	intptr_t bufptr_int = (intptr_t) buf;
	printf("buffer address: 0x%lx\n", bufptr_int);
	// Verify O_DIRECT requirements:
	// buffer size must be mutliple of block size
	assert(BUF_SIZE % pblock_size == 0);
	// buffer must be aligned at the block size
	assert((bufptr_int & (pblock_size - 1)) == 0);
}

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
