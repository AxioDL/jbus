#ifndef _WIN32
#include <unistd.h>
#if __APPLE__
#include <mach/mach_time.h>
#endif
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "jbus/Common.hpp"

namespace jbus
{

#if __APPLE__
static u64 MachToDolphinNum;
static u64 MachToDolphinDenom;
#endif

u64 GetGCTicks()
{
#if __APPLE__
    return mach_absolute_time() * MachToDolphinNum / MachToDolphinDenom;
#elif __linux__
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);

    return u64((tp.tv_sec * 1000000000ull) + tp.tv_nsec) * GetGCTicksPerSec() / 1000000000ull;
#else
    return 0;
#endif
}

void WaitGCTicks(u64 ticks)
{
    struct timeval tv = {};
    tv.tv_sec = ticks / GetGCTicksPerSec();
    tv.tv_usec = (ticks % GetGCTicksPerSec()) * 1000000 / GetGCTicksPerSec();
    select(0, NULL, NULL, NULL, &tv);
}

void Initialize()
{
#if __APPLE__
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    MachToDolphinNum = GetGCTicksPerSec() * timebase.numer;
    MachToDolphinDenom = 1000000000ull * timebase.denom;
#endif
}

}
