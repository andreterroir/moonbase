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
	char buf[BUF_SIZE];

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

	int bytesRead = read(fd, buf, BUF_SIZE);
	printf("read %d bytes from %s successfully\n", bytesRead, devPath);
	if (bytesRead == -1)
	{
		perror("open failed");
		exit(1);
	}

	for (int i = 0; i < bytesRead; ++i)
		assert(buf[i] == 0);
}
