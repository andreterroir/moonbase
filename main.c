#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "rwal.h"
#include "rwal_internal.h"

#define MAX_RECORD 1024

// returns next LSN
int read_records_from(struct Log *log, int lsn, uint64_t offset);

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
	print_header(log.header);

	printf("reading existing records starting at %ld\n", log.roffset);
	int lsn = read_records_from(&log, 0/*lsn*/, log.roffset);
	printf("roffset: %ld, eoffset: %ld\n", log.roffset, log.header.eoffset);

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
	lsn = read_records_from(&log, lsn, record_offset);

	// append and then read another record
	char record3[] = { 0xc0, 0xff, 0xee };
	lappend_record(&log, record3, sizeof(record3));
	record_len = RHEADER_SIZE + sizeof(record3);
	record_offset = log.header.eoffset - record_len;
	lsn = read_records_from(&log, lsn, record_offset);
	printf("roffset: %ld, eoffset: %ld\n", log.roffset, log.header.eoffset);

	// Simulate a crash after a successfully completed append but without
	// flushing the log header.
	lfsync(log);
	// lclose(log);
}

int read_records_from(struct Log *log, int lsn, uint64_t offset)
{
	struct RecordHeader rheader;
	char rbuf[MAX_RECORD];

	if (offset != log->roffset) {
		lrewind(log, offset);
	}
	while(lread_record(log, &rheader, rbuf) != -1) {
		print_record(lsn++, rheader, rbuf);
	}
	return lsn;
}
