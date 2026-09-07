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
static const char header[HEADER_SIZE] = {
	// 0x52, 0x57, 0x41, 0x4C
	'R', 'W', 'A', 'L', // magic
	0x0, // version byte
	// 8 byte LE offsets support up to 16EB large log device.
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // start offset
	0x0, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // end offset
};

void verify_buffer(int fd, char *buf);
void bread(int fd, char *buf);
void bwrite(int fd, char *buf);
void bseek(int fd, off_t offset);
void append(int fd, char *buf, uint64_t *offset, char *bytes, int count);

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

	// read the header block
	bread(fd, buf);
	printf("read magic: '%4s'\n", buf);
	if (strncmp(header, buf, MAGIC_SIZE) != 0) {
		printf("magic mismatch, preparing a new log device\n");

		memset(buf, 0, BUF_SIZE); // reset the buffer
		memcpy(buf, header, HEADER_SIZE);

		bseek(fd, 0);
		bwrite(fd, buf);

		if (fsync(fd) == 1) {
			perror("fsyncing new header failed");
			exit(1);
		}
		printf("written the log device header block\n");
	}

	// extract the version and the offsets
	int boffset = MAGIC_SIZE;
	int version = buf[boffset];
	boffset += 1;

	uint64_t soffset = 0; // start of the log
	for (int i = 0; i < sizeof(soffset); i++) {
		soffset += buf[boffset+i] << i * 8;
	}
	boffset += 8;
	uint64_t eoffset = 0; // next record offset
	for (int i = 0; i < sizeof(eoffset); i++) {
		eoffset += buf[boffset+i] << i * 8;
	}
	printf("start offset: %lu, end offset: %lu\n", soffset, eoffset);

	bseek(fd, eoffset);
	// read the current block
	bread(fd, buf);

	// append two records
	char record1[] = { 0xde, 0xad, 0xbe, 0xef };
	append(fd, buf, &eoffset, record1, sizeof(record1));
	char record2[] = { 0xca, 0xfe, 0xba, 0xbe };
	append(fd, buf, &eoffset, record2, sizeof(record2));

	// overwrite the current block
	// bflush
	bseek(fd, eoffset);
	bwrite(fd, buf);

	// read the records back
	memset(buf, 0, BUF_SIZE);
	bseek(fd, eoffset);
	bread(fd, buf);
	char rbuf[4];
	memcpy(rbuf, buf, 4);
	printf("record: %4s\n", rbuf);
	memcpy(rbuf, buf + 4, 4);
	printf("record: %4s\n", rbuf);

	// checkpoint - update header and flush
	// seek to the header
	bseek(fd, 0);
	bread(fd, buf);
	boffset = MAGIC_SIZE + 1 + sizeof(soffset);
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
	int pblockSize;
	if (ioctl(fd, BLKPBSZGET, &pblockSize) == -1)
	{
		perror("failed to get physical block size");
		exit(1);
	}
	printf("physical block size for is %d\n", pblockSize);

	intptr_t bufptr_int = (intptr_t) buf;
	printf("buffer address: 0x%lx\n", bufptr_int);
	// Verify O_DIRECT requirements:
	// buffer size must be mutliple of block size
	assert(BUF_SIZE % pblockSize == 0);
	// buffer must be aligned at the block size
	assert((bufptr_int & (pblockSize - 1)) == 0);
}

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

void bwrite(int fd, char *buf)
{
		ssize_t bytesWritten = write(fd, buf, BUF_SIZE);
		if (bytesWritten == -1) {
			perror("bwrite");
			exit(1);
		}
		assert(bytesWritten == BUF_SIZE);
}

// Seek over to the start of the current block.
void bseek(int fd, off_t offset)
{
	if (lseek(fd, offset / BUF_SIZE * BUF_SIZE, SEEK_SET) == -1) {
		perror("bseek");
		exit(1);
	}
}

void append(int fd, char *buf, uint64_t *offset, char *bytes, int count)
{
	// TODO handle records spilling into following blocks:
	// consume bytes in BUF_SIZE chunks and flush full buffers
	memcpy(buf + *offset % BUF_SIZE, bytes, count);
	*offset += count;
}
