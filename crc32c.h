#ifndef crc32c_h
#define crc32c_h

#ifdef __SSE4_2__

#include <nmmintrin.h>

static inline uint32_t crc32c8(uint32_t crc, uint8_t v)
{
  return _mm_crc32_u8(crc, v);
}

static inline uint32_t crc32c16(uint32_t crc, uint16_t v)
{
  return _mm_crc32_u16(crc, v);
}

static inline uint32_t crc32c32(uint32_t crc, uint32_t v)
{
  return _mm_crc32_u32(crc, v);
}

static inline uint32_t crc32c64(uint32_t crc, uint64_t v)
{
  return _mm_crc32_u64(crc, v);
}


#elifdef __ARM_FEATURE_CRC32

#include <arm_acle.h>

static inline uint32_t crc32c8(uint32_t crc, uint8_t v)
{
  return __crc32cb(crc, v);
}

static inline uint32_t crc32c16(uint32_t crc, uint16_t v)
{
  return __crc32ch(crc, v);
}

static inline uint32_t crc32c32(uint32_t crc, uint32_t v)
{
  return __crc32cw(crc, v);
}

static inline uint32_t crc32c64(uint32_t crc, uint64_t v)
{
  return __crc32cd(crc, v);
}

#else
	#error "No CRC32C implementation for the CPU architecture"
#endif

#endif
