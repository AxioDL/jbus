#pragma once

#include <functional>
#include <cstdint>
#include <cstdlib>

namespace jbus {

using s8 = int8_t;
using u8 = uint8_t;
using s16 = int16_t;
using u16 = uint16_t;
using s32 = int32_t;
using u32 = uint32_t;
using s64 = int64_t;
using u64 = uint64_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS

#undef bswap16
#undef bswap32
#undef bswap64

/* Type-sensitive byte swappers */
template <typename T>
constexpr T bswap16(T val) {
#if __GNUC__
  return __builtin_bswap16(val);
#elif _WIN32
  return _byteswap_ushort(val);
#else
  return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
constexpr T bswap32(T val) {
#if __GNUC__
  return __builtin_bswap32(val);
#elif _WIN32
  return _byteswap_ulong(val);
#else
  val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
  val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
  return val;
#endif
}

template <typename T>
constexpr T bswap64(T val) {
#if __GNUC__
  return __builtin_bswap64(val);
#elif _WIN32
  return _byteswap_uint64(val);
#else
  return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
         ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
         ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
         ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
#endif
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr int16_t SBig(int16_t val) { return bswap16(val); }
constexpr uint16_t SBig(uint16_t val) { return bswap16(val); }
constexpr int32_t SBig(int32_t val) { return bswap32(val); }
constexpr uint32_t SBig(uint32_t val) { return bswap32(val); }
constexpr int64_t SBig(int64_t val) { return bswap64(val); }
constexpr uint64_t SBig(uint64_t val) { return bswap64(val); }
constexpr float SBig(float val) {
  union { float f; int32_t i; } uval1 = {val};
  union { int32_t i; float f; } uval2 = {bswap32(uval1.i)};
  return uval2.f;
}
constexpr double SBig(double val) {
  union { double f; int64_t i; } uval1 = {val};
  union { int64_t i; double f; } uval2 = {bswap64(uval1.i)};
  return uval2.f;
}
#ifndef SBIG
#define SBIG(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SLittle(int16_t val) { return val; }
constexpr uint16_t SLittle(uint16_t val) { return val; }
constexpr int32_t SLittle(int32_t val) { return val; }
constexpr uint32_t SLittle(uint32_t val) { return val; }
constexpr int64_t SLittle(int64_t val) { return val; }
constexpr uint64_t SLittle(uint64_t val) { return val; }
constexpr float SLittle(float val) { return val; }
constexpr double SLittle(double val) { return val; }
#ifndef SLITTLE
#define SLITTLE(q) (q)
#endif
#else
constexpr int16_t SLittle(int16_t val) { return bswap16(val); }
constexpr uint16_t SLittle(uint16_t val) { return bswap16(val); }
constexpr int32_t SLittle(int32_t val) { return bswap32(val); }
constexpr uint32_t SLittle(uint32_t val) { return bswap32(val); }
constexpr int64_t SLittle(int64_t val) { return bswap64(val); }
constexpr uint64_t SLittle(uint64_t val) { return bswap64(val); }
constexpr float SLittle(float val) {
  int32_t ival = bswap32(*((int32_t*)(&val)));
  return *((float*)(&ival));
}
constexpr double SLittle(double val) {
  int64_t ival = bswap64(*((int64_t*)(&val)));
  return *((double*)(&ival));
}
#ifndef SLITTLE
#define SLITTLE(q) (((q)&0x000000FF) << 24 | ((q)&0x0000FF00) << 8 | ((q)&0x00FF0000) >> 8 | ((q)&0xFF000000) >> 24)
#endif

constexpr int16_t SBig(int16_t val) { return val; }
constexpr uint16_t SBig(uint16_t val) { return val; }
constexpr int32_t SBig(int32_t val) { return val; }
constexpr uint32_t SBig(uint32_t val) { return val; }
constexpr int64_t SBig(int64_t val) { return val; }
constexpr uint64_t SBig(uint64_t val) { return val; }
constexpr float SBig(float val) { return val; }
constexpr double SBig(double val) { return val; }
#ifndef SBIG
#define SBIG(q) (q)
#endif
#endif

class Endpoint;
class ThreadLocalEndpoint;

#endif

enum EJStatFlags {
  GBA_JSTAT_MASK = 0x3a,
  GBA_JSTAT_FLAGS_SHIFT = 4,
  GBA_JSTAT_FLAGS_MASK = 0x30,
  GBA_JSTAT_PSF1 = 0x20,
  GBA_JSTAT_PSF0 = 0x10,
  GBA_JSTAT_SEND = 0x08,
  GBA_JSTAT_RECV = 0x02
};

enum EJoyReturn {
  GBA_READY = 0,
  GBA_NOT_READY = 1,
  GBA_BUSY = 2,
  GBA_JOYBOOT_UNKNOWN_STATE = 3,
  GBA_JOYBOOT_ERR_INVALID = 4
};

/** @brief Standard callback for asynchronous jbus::Endpoint APIs.
 *  @param endpoint Thread-local Endpoint interface for optionally issuing next command in sequence.
 *  @param status GBA_READY if connection is still open, GBA_NOT_READY if connection lost. */
using FGBACallback = std::function<void(ThreadLocalEndpoint& endpoint, EJoyReturn status)>;

/** @brief Get host system's timebase scaled into Dolphin ticks.
 *  @return Scaled ticks from host timebase. */
u64 GetGCTicks();

/** @brief Wait an approximate Dolphin tick duration (avoid using, it's rather inaccurate).
 *  @param ticks CPU ticks to wait. */
void WaitGCTicks(u64 ticks);

/** @brief Obtain CPU ticks per second of Dolphin hardware (clock speed).
 *  @return 486Mhz - always. */
constexpr u64 GetGCTicksPerSec() { return 486000000ull; }

/** @brief Initialize platform specifics of JBus library */
void Initialize();

} // namespace jbus
