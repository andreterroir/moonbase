#include <stdlib.h>
#include <string.h>
#include "rwal.h"
#include "rwal_internal.h"
#include "unity/unity.h"

// required by unity
void setUp() {}
void tearDown() {}

void _parse_initialized_header()
{
	char buf[BSIZE];
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
	TEST_ASSERT_EQUAL_HEX(0xD594A75F, h.crc);
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
	char buffer[BSIZE];
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
	uint32_t crc;
	bp = readu32le(bp, &crc);
	TEST_ASSERT_EQUAL_HEX32(0x5E0EFE16, crc);

	// 48:	zero padding until end of the block
	TEST_ASSERT_EACH_EQUAL_MEMORY(0, bp, 1, BSIZE - HEADER_SIZE);
}

void _write_initialized_header()
{
	char buf[BSIZE];
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

void _encode_rheader()
{
	const char payload[] = { 0xca, 0xfe, 0xba, 0xbe };
	const int plen = sizeof(payload);
	char buf[RHEADER_SIZE];
	const int iseq = 92;
	const int irnd = 0xfeed;

	encode_rheader(buf, iseq, irnd, payload, plen);
	printhex("record", buf, sizeof(buf));

	const char *bp = buf;

	uint32_t val;
	bp = readu32le(bp, &val);
	TEST_ASSERT_EQUAL(iseq, val);

	bp = readu32le(bp, &val);
	TEST_ASSERT_EQUAL(irnd, val);

	bp = readu32le(bp, &val);
	TEST_ASSERT_EQUAL(plen, val);

	// header CRC
	bp = readu32le(bp, &val);
	TEST_ASSERT_EQUAL_HEX(0x7A606998u, val);

	// payload CRC
	bp = readu32le(bp, &val);
	TEST_ASSERT_EQUAL_HEX(0xD3B7F26Cu, val);
}

void _readu32le()
{
	char bytes[4] = { 0x0F, 0x00, 0x00, 0xF0 };
	uint32_t val;
	readu32le(bytes, &val);
	TEST_ASSERT_EQUAL_HEX32(0xF000000FUL, val);

	char placeholder[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
	readu32le(placeholder, &val);
	TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAUL, val);
}

void _readu64le()
{
	char bytes[8] = { 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0 };
	uint64_t val;
	readu64le(bytes, &val);
	TEST_ASSERT_EQUAL_HEX64(0xF00000000000000FUL, val);

	char placeholder[8] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
	readu64le(placeholder, &val);
	TEST_ASSERT_EQUAL_HEX64(0xAAAAAAAAAAAAAAAAUL, val);
}

void _crc32c()
{
	char beef[4] = { 0xde, 0xad, 0xbe, 0xef };
	TEST_ASSERT_EQUAL_HEX32(0xF1DC778E, crc32c(beef, 4));

	TEST_ASSERT_EQUAL_HEX32(0x0, crc32c(beef, 0));
	char zero[1] = { 0x00 };
	TEST_ASSERT_EQUAL_HEX32(0x527D5351, crc32c(zero, 1));

	char five32[4] = {0x05, 0x00, 0x00, 0x00};
	TEST_ASSERT_EQUAL_HEX32(0xEE00D08C, crc32c(five32, 4));
	char five8[1] = {0x05};
	TEST_ASSERT_EQUAL_HEX32(0x678C474D, crc32c(five8, 1));

	// make the CRC computation to go through each step to reach 8 byte
	// alignment boundary and to process the remaining tail data
	// 1 byte + 2 bytes + 4 bytes + 40 bytes + 4 bytes + 2 bytes + 1 byte
	const char bytes[54] = {
		0x60, 0xea, 0xe5, 0x09, 0xc6, 0x9b, 0xfd, 0x1d, 0x2d, 0x56, 0x99, 0xd6, 0xd0,
		0x69, 0x15, 0x93, 0x15, 0xc6, 0x6d, 0x52, 0xf9, 0x84, 0x6e, 0x24, 0x07, 0x93,
		0xc6, 0xfb, 0x42, 0x39, 0x01, 0xe6, 0x43, 0x3a, 0x0d, 0x8d, 0x73, 0x23, 0x77,
		0x39, 0xf5, 0x8e, 0x6f, 0xda, 0x40, 0x1f, 0xbb, 0x61, 0x06, 0xfd, 0x31, 0x72,
		0xe2, 0x0c
	};
	for (int i = 0; i < 8; i++) {
		char *misaligned = (char *) aligned_alloc(8, sizeof(bytes) + i);
		char *ptr = misaligned + i;
		memcpy(ptr, bytes, 54);
		TEST_ASSERT_EQUAL_HEX32(0x33BBC033, crc32c(ptr, 1));
		TEST_ASSERT_EQUAL_HEX32(0xF92FD2F6, crc32c(ptr, 2));
		TEST_ASSERT_EQUAL_HEX32(0x518A4818, crc32c(ptr, 3));
		TEST_ASSERT_EQUAL_HEX32(0xB0199D75, crc32c(ptr, 4));
		TEST_ASSERT_EQUAL_HEX32(0xF388CBF1, crc32c(ptr, 5));
		TEST_ASSERT_EQUAL_HEX32(0x58AA60C0, crc32c(ptr, 6));
		TEST_ASSERT_EQUAL_HEX32(0xDDEEFC53, crc32c(ptr, 7));
		TEST_ASSERT_EQUAL_HEX32(0xBFA31F36, crc32c(ptr, 8));
		TEST_ASSERT_EQUAL_HEX32(0xDB159C1A, crc32c(ptr, 9));
		TEST_ASSERT_EQUAL_HEX32(0x6707AC08, crc32c(ptr, 54));
	}
}

int main()
{
	UNITY_BEGIN();
	RUN_TEST(_parse_initialized_header);
	RUN_TEST(_parse_header_invalid_magic);
	RUN_TEST(_initialize_header);
	RUN_TEST(_write_initialized_header);
	RUN_TEST(_encode_rheader);
	RUN_TEST(_readu32le);
	RUN_TEST(_readu64le);
	RUN_TEST(_crc32c);
	return UNITY_END();
}
