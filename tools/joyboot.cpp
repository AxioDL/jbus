#include <stdio.h>
#include "jbus/Listener.hpp"
#include "jbus/Endpoint.hpp"
#include <functional>

static void clientPadComplimentCheck(jbus::u8* buffer)
{
    jbus::u8 check = 0x19;
    for (int i=0xa0 ; i<0xbd ; ++i)
        check += buffer[i];
    buffer[0xbd] = -check;
}

static jbus::EJoyReturn BootStatus = jbus::GBA_JOYBOOT_ERR_INVALID;
static void JoyBootDone(jbus::ThreadLocalEndpoint& endpoint, jbus::EJoyReturn status)
{
    BootStatus = status;
}

static bool DonePoll(jbus::Endpoint& endpoint)
{
    jbus::u8 status;
    if (endpoint.GBAReset(&status) == jbus::GBA_NOT_READY)
        if (endpoint.GBAReset(&status) == jbus::GBA_NOT_READY)
            return false;

    if (endpoint.GBAGetStatus(&status) == jbus::GBA_NOT_READY)
        return false;
    if (status != (jbus::GBA_JSTAT_PSF1 | jbus::GBA_JSTAT_SEND))
        return false;

    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage: joyboot <client_pad.bin>\n");
        return 1;
    }

    FILE* fp = fopen(argv[1], "rb");
    if (!fp)
    {
        fprintf(stderr, "Unable to open %s\n", argv[1]);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    int fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::unique_ptr<jbus::u8[]> data(new jbus::u8[fsize]);
    fread(data.get(), 1, fsize, fp);
    fclose(fp);
    if (fsize < 512)
    {
        fprintf(stderr, "%s must be at least 512 bytes\n", argv[1]);
        return 1;
    }

    clientPadComplimentCheck(data.get());

    jbus::Initialize();
    printf("Listening for client\n");
    jbus::Listener listener;
    listener.start();
    std::unique_ptr<jbus::Endpoint> endpoint;
    while (true)
    {
        jbus::s64 frameStart = jbus::GetGCTicks();
        endpoint = listener.accept();
        if (endpoint)
            break;
        jbus::s64 frameEnd = jbus::GetGCTicks();
        jbus::s64 passedTicks = frameEnd - frameStart;
        jbus::s64 waitTicks = jbus::GetGCTicksPerSec() / 60 - passedTicks;
        if (waitTicks > 0)
            jbus::WaitGCTicks(waitTicks);
    }

    printf("Waiting 4 sec\n");
    jbus::WaitGCTicks(jbus::GetGCTicksPerSec() * 4);

    jbus::u8 status;
    if (endpoint->GBAJoyBootAsync(2, 2, data.get(), fsize,
        &status, std::bind(JoyBootDone,
                           std::placeholders::_1,
                           std::placeholders::_2)) != jbus::GBA_READY)
    {
        fprintf(stderr, "Unable to start JoyBoot\n");
        return 1;
    }

    jbus::s64 start = jbus::GetGCTicks();
    jbus::u8 percent = 0;
    jbus::u8 lastpercent = 0;
    while (endpoint->GBAGetProcessStatus(&percent) == jbus::GBA_BUSY)
    {
        if (percent != lastpercent)
        {
            lastpercent = percent;
            printf("\rUpload %d%%", percent);
            fflush(stdout);
        }
        jbus::s64 curTime = jbus::GetGCTicks();
        jbus::s64 passedTicks = curTime - start;
        if (passedTicks > jbus::GetGCTicksPerSec() * 10)
        {
            fprintf(stderr, "JoyBoot timeout\n");
            return 1;
        }
        jbus::WaitGCTicks(jbus::GetGCTicksPerSec() / 60);
    }
    printf("\nJoy Boot finished with %d status\n", status);

    while (!DonePoll(*endpoint))
    {
        jbus::s64 curTime = jbus::GetGCTicks();
        jbus::s64 passedTicks = curTime - start;
        if (passedTicks > jbus::GetGCTicksPerSec() * 15)
        {
            fprintf(stderr, "JoyBoot timeout\n");
            return 1;
        }
        jbus::WaitGCTicks(jbus::GetGCTicksPerSec() / 60);
    }

    return 0;
}
