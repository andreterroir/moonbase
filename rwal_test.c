#include <string.h>
#include "rwal.h"
#include "rwal_internal.h"
#include "unity/unity.h"

// required by unity
void setUp() {}
void tearDown() {}

void test_initialize_header()
{
	char buf[BUF_SIZE];
	initialize_header(buf);

    // 0:	3b magic "RWL"
	TEST_ASSERT_EQUAL_STRING_LEN("RWL", buf, MAGIC_SIZE);

	// 3:	1b version (currently 1)
	int offset = MAGIC_SIZE;
	TEST_ASSERT_EQUAL_UINT(1, buf[offset]);

	// 4:	4b iseq - incarnation sequence number
	offset += VERSION_SIZE;
	int iseq_size = 4;
	uint32_t iseq = readle(buf + offset, iseq_size);
	TEST_ASSERT_EQUAL_UINT32(0, iseq);

	// 8:	4b irnd - random incarnation salt
	offset += iseq_size;
	int irnd_size = 4;
	TEST_ASSERT_NOT_EQUAL_MEMORY(0, buf + offset, irnd_size);

	// 12:	8b ioffset - starting incarnation offset
	offset += irnd_size;
	int ioffset_size = 8;
	uint64_t ioffset = readle(buf + offset, ioffset_size);
	TEST_ASSERT_EQUAL_UINT64(4096, ioffset);

	// 20:	8b soffset - log start offset
	offset += ioffset_size;
	int soffset_size = 8;
	uint64_t soffset = readle(buf + offset, soffset_size);
	TEST_ASSERT_EQUAL_UINT64(4096, soffset);

	// 28:	8b eoffset - log end offset
	offset += soffset_size;
	int eoffset_size = 8;
	uint64_t eoffset = readle(buf + offset, eoffset_size);
	TEST_ASSERT_EQUAL_UINT64(4096, eoffset);

	// 36:	4b crc
	offset += eoffset_size;
	int crc_size = 4;
	TEST_ASSERT_EACH_EQUAL_HEX8(0x0, buf + offset, crc_size);

	// 40:	zero padding until end of the block
	TEST_ASSERT_EACH_EQUAL_MEMORY(0, buf + HEADER_SIZE, 1, BUF_SIZE - HEADER_SIZE);
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(test_initialize_header);
	return UNITY_END();
}
