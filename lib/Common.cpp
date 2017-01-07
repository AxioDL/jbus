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
#include <WinSock2.h>
#endif

#include "jbus/Common.hpp"

namespace jbus
{

#if __APPLE__
static u64 MachToDolphinNum;
static u64 MachToDolphinDenom;
#elif _WIN32
static LARGE_INTEGER PerfFrequency;
#endif

u64 GetGCTicks()
{
#if __APPLE__
    return mach_absolute_time() * MachToDolphinNum / MachToDolphinDenom;
#elif __linux__
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);

    return u64((tp.tv_sec * 1000000000ull) + tp.tv_nsec) * GetGCTicksPerSec() / 1000000000ull;
#elif _WIN32
    LARGE_INTEGER perf;
    QueryPerformanceCounter(&perf);
    perf.QuadPart *= GetGCTicksPerSec();
    perf.QuadPart /= PerfFrequency.QuadPart;
    return perf.QuadPart;
#else
    return 0;
#endif
}

void WaitGCTicks(u64 ticks)
{
#ifndef _WIN32
    struct timeval tv = {};
    tv.tv_sec = ticks / GetGCTicksPerSec();
    tv.tv_usec = (ticks % GetGCTicksPerSec()) * 1000000 / GetGCTicksPerSec();
    select(0, NULL, NULL, NULL, &tv);
#else
    Sleep(ticks * 1000 / GetGCTicksPerSec() +
          (ticks % GetGCTicksPerSec()) * 1000 / GetGCTicksPerSec());
#endif
}

void Initialize()
{
#if __APPLE__
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    MachToDolphinNum = GetGCTicksPerSec() * timebase.numer;
    MachToDolphinDenom = 1000000000ull * timebase.denom;
#elif _WIN32
    WSADATA initData;
    WSAStartup(MAKEWORD(2, 2), &initData);
    QueryPerformanceFrequency(&PerfFrequency);
#endif
}

}
