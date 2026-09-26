#include <string.h>
#include "rwal.h"
#include "rwal_internal.h"
#include "unity/unity.h"

// required by unity
void setUp() {}
void tearDown() {}

void _parse_initialized_header()
{
	char buf[BUF_SIZE];
	struct Header h;
	uint64_t device_blocks = 1 << 30;
	initialize_header(buf, device_blocks);

	TEST_ASSERT_EQUAL(0, parse_header(buf, &h));

	TEST_ASSERT_EQUAL(1, h.version);
	TEST_ASSERT_EQUAL(0, h.iseq);
	TEST_ASSERT_NOT_EQUAL(0, h.irnd);
	TEST_ASSERT_EQUAL(device_blocks, h.blocks);
	TEST_ASSERT_EQUAL(4096, h.ioffset);
	TEST_ASSERT_EQUAL(4096, h.soffset);
	TEST_ASSERT_EQUAL(4096, h.eoffset);
	TEST_ASSERT_EQUAL_HEX(0xAAAAAAAA, h.crc); // placeholder value
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
	uint64_t device_blocks = 1 << 28; // 1TiB in 4096 (1<<12) device_blocks
	initialize_header(buffer, device_blocks);
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

	// 12:	8b blocks - size of the device in blocks
	uint64_t blocks;
	bp = readu64le(bp, &blocks);
	TEST_ASSERT_EQUAL_UINT64(device_blocks, blocks);

	// 20:	8b ioffset - starting incarnation offset
	uint64_t ioffset;
	bp = readu64le(bp, &ioffset);
	TEST_ASSERT_EQUAL_UINT64(4096, ioffset);

	// 28:	8b soffset - log start offset
	uint64_t soffset;
	bp = readu64le(bp, &soffset);
	TEST_ASSERT_EQUAL_UINT64(4096, soffset);

	// 36:	8b eoffset - log end offset
	uint64_t eoffset;
	bp = readu64le(bp, &eoffset);
	TEST_ASSERT_EQUAL_UINT64(4096, eoffset);

	// 44:	4b crc
	int crc_size = 4;
	TEST_ASSERT_EACH_EQUAL_HEX8(0xAA, bp, crc_size);
	bp += crc_size;

	// 48:	zero padding until end of the block
	TEST_ASSERT_EACH_EQUAL_MEMORY(0, bp, 1, BUF_SIZE - HEADER_SIZE);
}

void _write_initialized_header()
{
	char buf[BUF_SIZE];
	uint64_t device_blocks = 1UL << 36;
	initialize_header(buf, device_blocks);

	struct Header h;
	TEST_ASSERT_EQUAL(0, parse_header(buf, &h));

	uint8_t version = h.version;
	uint32_t iseq = h.iseq;
	uint32_t irnd = h.irnd;
	device_blocks = h.blocks;
	uint64_t ioffset = h.ioffset;
	uint64_t soffset = h.soffset;
	uint64_t eoffset = h.eoffset;
	uint32_t crc = h.crc;

	write_header(buf, h);
	parse_header(buf, &h);

	TEST_ASSERT_EQUAL(version, h.version);
	TEST_ASSERT_EQUAL(iseq, h.iseq);
	TEST_ASSERT_EQUAL(irnd, h.irnd);
	TEST_ASSERT_EQUAL_HEX64(device_blocks, h.blocks);
	TEST_ASSERT_EQUAL(ioffset, h.ioffset);
	TEST_ASSERT_EQUAL(soffset, h.soffset);
	TEST_ASSERT_EQUAL(eoffset, h.eoffset);
	TEST_ASSERT_EQUAL(crc, h.crc);
}

void _readu32le() {
	char bytes[4] = { 0x0F, 0x00, 0x00, 0xF0 };
	uint32_t val;
	readu32le(bytes, &val);
	TEST_ASSERT_EQUAL_HEX32(0xF000000FUL, val);

	char placeholder[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
	readu32le(placeholder, &val);
	TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAUL, val);
}

void _readu64le() {
	char bytes[8] = { 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0 };
	uint64_t val;
	readu64le(bytes, &val);
	TEST_ASSERT_EQUAL_HEX64(0xF00000000000000FUL, val);

	char placeholder[8] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
	readu64le(placeholder, &val);
	TEST_ASSERT_EQUAL_HEX64(0xAAAAAAAAAAAAAAAAUL, val);
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(_parse_initialized_header);
	RUN_TEST(_parse_header_invalid_magic);
	RUN_TEST(_initialize_header);
	RUN_TEST(_write_initialized_header);

	RUN_TEST(_readu32le);
	RUN_TEST(_readu64le);
	return UNITY_END();
}
