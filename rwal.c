#define _GNU_SOURCE // O_DIRECT - has to be defined before includes
#include <assert.h>
#include <linux/fs.h> // BLKPBSZGET
#include <fcntl.h> // open
#include <stdio.h>
#include <stdlib.h> // exit
#include <sys/ioctl.h>
#include <unistd.h> // read

#define BUF_SIZE 1024

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

	int fd = open(devPath, O_RDONLY | O_DIRECT);
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

	int bytesRead = read(fd, buf, BUF_SIZE);
	if (bytesRead == -1)
	{
		perror("read failed");
		exit(1);
	}
	printf("read %d bytes from %s successfully\n", bytesRead, devPath);

	for (int i = 0; i < bytesRead; ++i)
		assert(buf[i] == 0);
}
