#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rwal.h"

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s [blockdev]\n", argv[0]);
		exit(1);
	}

	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		perror("failed to get clock time for random seed");
		exit(1);
	}
	srandom(ts.tv_nsec);

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
