#define _GNU_SOURCE // O_DIRECT - has to be defined before includes
#include <assert.h>
#include <fcntl.h> // open
#include <stdio.h>
#include <stdlib.h> // exit
#include <unistd.h> // read

#define BUF_SIZE 1024

int main(int argc, char *argv[])
{
	char buf[BUF_SIZE];

	if (argc != 2) {
		fprintf(stderr, "usage: %s [blockdev]\n", argv[0]);
		exit(1);
	}

	int fd = open(argv[1], O_RDONLY | O_DIRECT);
	if (fd == -1)
	{
		perror("open failed");
		exit(1);
	}

	int bytesRead = read(fd, buf, BUF_SIZE);
	printf("read %d bytes from %s successfully\n", bytesRead, DEV_PATH);
	if (bytesRead == -1)
	{
		perror("open failed");
		exit(1);
	}

	for (int i = 0; i < bytesRead; ++i)
		assert(buf[i] == 0);
}
