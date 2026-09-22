#include <string.h>
#include "rwal.h"
#include "rwal_internal.h"
#include "unity/unity.h"

// required by unity
void setUp() {}
void tearDown() {}

void _parse_init_header()
{
	struct Header h;
	TEST_ASSERT_EQUAL(0, parse_header(init_header, &h));

	TEST_ASSERT_EQUAL(1, h.version);
	TEST_ASSERT_EQUAL(0, h.iseq);
	TEST_ASSERT_EQUAL(0, h.irnd);
	TEST_ASSERT_EQUAL(4096, h.ioffset);
	TEST_ASSERT_EQUAL(4096, h.soffset);
	TEST_ASSERT_EQUAL(4096, h.eoffset);
	TEST_ASSERT_EQUAL(0, h.crc);
}

void _parse_header_invalid_magic()
{
	char buf[HEADER_SIZE];
	memcpy(buf, init_header, HEADER_SIZE);

	// mangle magic
	memcpy(buf, "HAL", 3);

	struct Header h;
	TEST_ASSERT_EQUAL(-1, parse_header(buf, &h));
}

void _initialize_header()
{
	char buffer[BUF_SIZE];
	initialize_header(buffer);
	const char *bp = buffer;

    // 0:	3b magic "RWL"
	TEST_ASSERT_EQUAL_STRING_LEN("RWL", bp, MAGIC_SIZE);

	// 3:	1b version (currently 1)
	bp += MAGIC_SIZE;
	TEST_ASSERT_EQUAL_UINT(1, bp[0]);
	bp += VERSION_SIZE;

	// 4:	4b iseq - incarnation sequence number
	uint32_t iseq;
	bp = readu32le(bp, &iseq);
	TEST_ASSERT_EQUAL_UINT32(0, iseq);

	// 8:	4b irnd - random incarnation salt
	uint32_t irnd_size = 4;
	TEST_ASSERT_NOT_EQUAL_MEMORY(0, bp, irnd_size);
	bp += irnd_size;

	// 12:	8b ioffset - starting incarnation offset
	uint64_t ioffset;
	bp = readu64le(bp, &ioffset);
	TEST_ASSERT_EQUAL_UINT64(4096, ioffset);

	// 20:	8b soffset - log start offset
	uint64_t soffset;
	bp = readu64le(bp, &soffset);
	TEST_ASSERT_EQUAL_UINT64(4096, soffset);

	// 28:	8b eoffset - log end offset
	uint64_t eoffset;
	bp = readu64le(bp, &eoffset);
	TEST_ASSERT_EQUAL_UINT64(4096, eoffset);

	// 36:	4b crc
	int crc_size = 4;
	TEST_ASSERT_EACH_EQUAL_HEX8(0x0, bp, crc_size);
	bp += crc_size;

	// 40:	zero padding until end of the block
	TEST_ASSERT_EACH_EQUAL_MEMORY(0, bp, 1, BUF_SIZE - HEADER_SIZE);
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(_parse_init_header);
	RUN_TEST(_parse_header_invalid_magic);
	RUN_TEST(_initialize_header);
	return UNITY_END();
}
