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
	// The offsets are relative to the start of the firs block.
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // start offset
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // end offset
};

void seekToBlock(int fd, off_t offset);

int main(int argc, char *argv[])
{
	// Enforce stricter buffer alignment than most physical block sizes, most
	// often 512.
	char buf[BUF_SIZE] __attribute__((aligned (4096)));

	if (argc != 2) {
		fprintf(stderr, "usage: %s [blockdev]\n", argv[0]);
		exit(1);
	}
	const char *devPath = argv[1];

	int fd = open(devPath, O_RDWR | O_DIRECT);
	if (fd == -1)
	{
		perror("open failed");
		exit(1);
	}

	int pblockSize;
	if (ioctl(fd, BLKPBSZGET, &pblockSize) == -1)
	{
		perror("failed to get physical block size");
		exit(1);
	}
	printf("physical block size for %s is %d\n", devPath, pblockSize);

	intptr_t bufptr_int = (intptr_t) buf;
	printf("buffer address: 0x%lx\n", bufptr_int);
	// Verify O_DIRECT requirements:
	// buffer size must be mutliple of block size
	assert(BUF_SIZE % pblockSize == 0);
	// buffer must be aligned at the block size
	assert((bufptr_int & (pblockSize - 1)) == 0);

	ssize_t bytesRead = read(fd, buf, BUF_SIZE);
	if (bytesRead == -1)
	{
		perror("read failed");
		exit(1);
	}
	// This generally shouldn't happen, unless the block device is too small or
	// the read was interrupted by a signal.
	assert(bytesRead == BUF_SIZE);
	printf("read %ld bytes from %s successfully\n", bytesRead, devPath);

	printf("read magic: '%4s'\n", buf);
	if (strncmp(header, buf, MAGIC_SIZE) != 0) {
		printf("magic mismatch, preparing a new log device\n");

		memset(buf, 0, BUF_SIZE); // reset the buffer
		memcpy(buf, header, HEADER_SIZE);

		// seek back to the beginning
		if (lseek(fd, 0, SEEK_SET) == -1) {
			perror("seek failed");
			exit(1);
		}
		ssize_t bytesWritten = write(fd, buf, BUF_SIZE);
		if (bytesWritten == -1) {
			perror("writing header failed");
			exit(1);
		}
		assert(bytesWritten == BUF_SIZE);

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

	seekToBlock(fd, eoffset);
	memset(buf, 0, BUF_SIZE); // reset the buffer
	// read the current block
	bytesRead = read(fd, buf, BUF_SIZE);
	assert(bytesRead == BUF_SIZE);

	// TODO handle records spilling into following blocks
	// append two records one by one
	char record1[] = { 0xde, 0xad, 0xbe, 0xef };
	printf("sizeof(record1): %ld\n", sizeof(record1));
	printf("record1 boffset: %ld\n", eoffset % BUF_SIZE);
	memcpy(buf + eoffset % BUF_SIZE, record1, sizeof(record1));
	eoffset += sizeof(record1);
	char record2[] = { 0xca, 0xfe, 0xba, 0xbe };
	memcpy(buf + eoffset % BUF_SIZE, record2, sizeof(record2));
	eoffset += sizeof(record2);

	// overwrite the current block
	seekToBlock(fd, eoffset);
	ssize_t bytesWritten = write(fd, buf, BUF_SIZE);
	if (bytesWritten == -1) {
		perror("failed to write to device");
		exit(1);
	}
	assert(bytesWritten == BUF_SIZE);

	// read the records back
	memset(buf, 0, BUF_SIZE);
	seekToBlock(fd, eoffset);
	bytesRead = read(fd, buf, BUF_SIZE);
	assert(bytesRead == BUF_SIZE);
	char rbuf[4];
	memcpy(rbuf, buf, 4);
	printf("record: %4s\n", rbuf);
	memcpy(rbuf, buf + 4, 4);
	printf("record: %4s\n", rbuf);

	// checkpoint - update header and flush
	// seek to the header
	if (lseek(fd, 0, SEEK_SET) == -1) {
		perror("seek failed");
		exit(1);
	}
	memset(buf, 0, BUF_SIZE);
	bytesRead = read(fd, buf, BUF_SIZE);
	assert(bytesRead == BUF_SIZE);
	boffset = MAGIC_SIZE + 1 + sizeof(soffset);
	for (int i = 0; i < sizeof(eoffset); i++) {
		buf[boffset++] = eoffset & 0xff;
		eoffset >>= 8;
	}
	if (lseek(fd, 0, SEEK_SET) == -1) {
		perror("seek failed");
		exit(1);
	}
	bytesWritten = write(fd, buf, BUF_SIZE);
	if (bytesWritten == -1) {
		perror("failed to write to device");
		exit(1);
	}

	if (close(fd) == 1) {
		perror("an error on closing file");
		exit(1);
	}
}

// Seek over to the start of the current block.
void seekToBlock(int fd, off_t offset) {
	if (lseek(fd, BUF_SIZE + offset / BUF_SIZE, SEEK_SET) == -1) {
		perror("seek failed");
		exit(1);
	}
}
