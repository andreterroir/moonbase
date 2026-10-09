#include <assert.h>
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
	char record1[] = { 0xde, 0xad, 0xbe, 0xef };
	const int plen = sizeof(record1);
	lappend_record(&log, record1, sizeof(record1));
	char record2[] = { 0xca, 0xfe, 0xba, 0xbe };
	assert(sizeof(record2) == plen);
	lappend_record(&log, record2, sizeof(record2));
	lfsync(log);

	// read the records back
	int record_len = RHEADER_SIZE + plen;
	int record_offset = log.header.eoffset - 2 * record_len;
	printf("reading records at %d\n", record_offset);
	lrewind(&log, record_offset);
	char rbuf[record_len];
	lread(&log, rbuf, record_len);
	printhex("record 1", rbuf, record_len);
	lread(&log, rbuf, record_len);
	printhex("record 2", rbuf, record_len);

	// append and then read another record
	char record3[] = { 0xc0, 0xff, 0xee };
	lappend_record(&log, record3, sizeof(record3));
	record_len = RHEADER_SIZE + sizeof(record3);
	lrewind(&log, log.header.eoffset - record_len);
	lread(&log, rbuf, record_len);
	printhex("record 3", rbuf, record_len);

	lclose(log);
}
