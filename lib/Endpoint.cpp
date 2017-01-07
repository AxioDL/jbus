#include "jbus/Endpoint.hpp"

#define LOG_TRANSFER 0

namespace jbus
{

void KawasedoChallenge::F23(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status != GBA_READY ||
        (status = endpoint.GBAResetAsync(x10_statusPtr,
                  bindThis(&KawasedoChallenge::F25))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F25(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status == GBA_READY)
        if (*x10_statusPtr != GBA_JSTAT_SEND)
            status = GBA_JOYBOOT_UNKNOWN_STATE;

    if (status != GBA_READY ||
        (status = endpoint.GBAGetStatusAsync(x10_statusPtr,
                  bindThis(&KawasedoChallenge::F27))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F27(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status == GBA_READY)
        if (*x10_statusPtr != (GBA_JSTAT_PSF0 | GBA_JSTAT_SEND))
            status = GBA_JOYBOOT_UNKNOWN_STATE;

    if (status != GBA_READY ||
        (status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr,
                  bindThis(&KawasedoChallenge::F29))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F29(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
    else
    {
        GBAX02();
        GBAX01(endpoint);
    }
}

void KawasedoChallenge::GBAX02()
{
    xf8_dspHmac.x0_gbaChallenge = reinterpret_cast<u32&>(x18_readBuf);
    xf8_dspHmac.x4_logoPalette = x0_pColor;
    xf8_dspHmac.x8_logoSpeed = x4_pSpeed;
    xf8_dspHmac.xc_progLength = xc_progLen;
    xf8_dspHmac.ProcessGBACrypto();
}

void KawasedoChallenge::GBAX01(ThreadLocalEndpoint& endpoint)
{
    x58_currentKey = xf8_dspHmac.x20_publicKey;
    x5c_initMessage = xf8_dspHmac.x24_authInitCode;

    x20_byteInWindow = ROUND_UP_8(xc_progLen);
    if (x20_byteInWindow < 512)
        x20_byteInWindow = 512;
    x64_totalBytes = x20_byteInWindow;
    x20_byteInWindow -= 512;
    x20_byteInWindow /= 8;

    reinterpret_cast<u32&>(x1c_writeBuf) = x5c_initMessage;

    x38_crc = 0x15a0;
    x34_bytesSent = 0;

    x28_ticksAfterXf = GetGCTicks();
    x30_justStarted = 1;

    EJoyReturn status;
    if ((status = endpoint.GBAWriteAsync(x1c_writeBuf, x10_statusPtr,
                  bindThis(&KawasedoChallenge::F31))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F31(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
        return;
    }

#if LOG_TRANSFER
    printf("PROG [%d/%d]\n", x34_bytesSent, x64_totalBytes);
#endif
    if (x30_justStarted)
    {
        x30_justStarted = 0;
    }
    else
    {
        if (!(*x10_statusPtr & GBA_JSTAT_PSF1) ||
            (*x10_statusPtr & GBA_JSTAT_PSF0) >> 4 != (x34_bytesSent & 4) >> 2)
        {
            x28_ticksAfterXf = 0;
            if (x14_callback)
            {
                x14_callback(endpoint, GBA_JOYBOOT_UNKNOWN_STATE);
                x14_callback = {};
            }
            return;
        }
        x34_bytesSent += 4;
    }

    if (x34_bytesSent <= x64_totalBytes)
    {
        u32 cryptWindow;
        if (x34_bytesSent != x64_totalBytes)
        {
            x20_byteInWindow = 0;
            cryptWindow = 0;
            while (x20_byteInWindow < 4)
            {
                if (xc_progLen)
                {
                    cryptWindow |= *x8_progPtr++ << (x20_byteInWindow * 8);
                    --xc_progLen;
                }
                ++x20_byteInWindow;
            }

            if (x34_bytesSent == 0xac)
            {
                x60_gameId = cryptWindow;
            }
            else if (x34_bytesSent == 0xc4)
            {
                cryptWindow = endpoint.GetChan() << 0x8;
            }

            if (x34_bytesSent >= 0xc0)
            {
                u32 shiftWindow = cryptWindow;
                u32 shiftCrc = x38_crc;
                for (int i=0 ; i<32 ; ++i)
                {
                    if ((shiftWindow ^ shiftCrc) & 0x1)
                        shiftCrc = (shiftCrc >> 1) ^ 0xa1c1;
                    else
                        shiftCrc >>= 1;

                    shiftWindow >>= 1;
                }
                x38_crc = shiftCrc;
            }

            if (x34_bytesSent == 0x1f8)
            {
                x3c_checkStore[0] = cryptWindow;
            }
            else if (x34_bytesSent == 0x1fc)
            {
                x20_byteInWindow = 1;
                x3c_checkStore[x20_byteInWindow] = cryptWindow;
            }
        }
        else
        {
            cryptWindow = x38_crc | x34_bytesSent << 16;
        }

        if (x34_bytesSent > 0xbf)
        {
            x58_currentKey = 0x6177614b * x58_currentKey + 1;

            cryptWindow ^= x58_currentKey;
            cryptWindow ^= -(0x2000000 + x34_bytesSent);
            cryptWindow ^= 0x20796220;
        }

        x1c_writeBuf[0] = cryptWindow >> 0;
        x1c_writeBuf[1] = cryptWindow >> 8;
        x1c_writeBuf[2] = cryptWindow >> 16;
        x1c_writeBuf[3] = cryptWindow >> 24;

        if (x34_bytesSent == 0x1f8)
            x3c_checkStore[2] = cryptWindow;

        if (x20_byteInWindow < 4)
        {
            x3c_checkStore[2 + x20_byteInWindow] = cryptWindow;
            x3c_checkStore[5 - x20_byteInWindow] =
                x3c_checkStore[1 + x20_byteInWindow] * x3c_checkStore[4 - x20_byteInWindow];
            x3c_checkStore[4 + x20_byteInWindow] =
                x3c_checkStore[1 + x20_byteInWindow] * x3c_checkStore[1 - x20_byteInWindow];
            x3c_checkStore[7 - x20_byteInWindow] =
                x3c_checkStore[-1 + x20_byteInWindow] * x3c_checkStore[4 - x20_byteInWindow];
        }

        if ((status = endpoint.GBAWriteAsync(x1c_writeBuf, x10_statusPtr,
                      bindThis(&KawasedoChallenge::F31))) != GBA_READY)
        {
            x28_ticksAfterXf = 0;
            if (x14_callback)
            {
                x14_callback(endpoint, status);
                x14_callback = {};
            }
        }
    }
    else // x34_bytesWritten > x64_totalBytes
    {
        if ((status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr,
                      bindThis(&KawasedoChallenge::F33))) != GBA_READY)
        {
            x28_ticksAfterXf = 0;
            if (x14_callback)
            {
                x14_callback(endpoint, status);
                x14_callback = {};
            }
        }
    }
}

void KawasedoChallenge::F33(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status != GBA_READY ||
        (status = endpoint.GBAGetStatusAsync(x10_statusPtr,
                  bindThis(&KawasedoChallenge::F35))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F35(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status == GBA_READY)
        if (*x10_statusPtr & (GBA_JSTAT_FLAGS_MASK | GBA_JSTAT_RECV))
            status = GBA_JOYBOOT_UNKNOWN_STATE;

    if (status != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
        return;
    }

    if (*x10_statusPtr != GBA_JSTAT_SEND)
    {
        if ((status = endpoint.GBAGetStatusAsync(x10_statusPtr,
                      bindThis(&KawasedoChallenge::F35))) != GBA_READY)
        {
            x28_ticksAfterXf = 0;
            if (x14_callback)
            {
                x14_callback(endpoint, status);
                x14_callback = {};
            }
        }
        return;
    }

    if ((status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr,
                  bindThis(&KawasedoChallenge::F37))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F37(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status != GBA_READY ||
        (status = endpoint.GBAWriteAsync(x18_readBuf, x10_statusPtr,
                  bindThis(&KawasedoChallenge::F39))) != GBA_READY)
    {
        x28_ticksAfterXf = 0;
        if (x14_callback)
        {
            x14_callback(endpoint, status);
            x14_callback = {};
        }
    }
}

void KawasedoChallenge::F39(ThreadLocalEndpoint& endpoint, EJoyReturn status)
{
    if (status == GBA_READY)
        *x10_statusPtr = 0;

    x28_ticksAfterXf = 0;

    if (x14_callback)
    {
        x14_callback(endpoint, status);
        x14_callback = {};
    }
}

KawasedoChallenge::KawasedoChallenge(Endpoint& endpoint, s32 paletteColor, s32 paletteSpeed,
                                     u8* programp, s32 length, u8* status, FGBACallback&& callback)
: x0_pColor(paletteColor), x4_pSpeed(paletteSpeed), x8_progPtr(programp), xc_progLen(length),
  x10_statusPtr(status), x14_callback(std::move(callback)), x34_bytesSent(0)
{
    if (endpoint.GBAGetStatusAsync(x10_statusPtr,
        bindThis(&KawasedoChallenge::F23)) != GBA_READY)
    {
        x14_callback = {};
        m_started = false;
    }
}

u64 Endpoint::getTransferTime(u8 cmd)
{
    u64 bytes = 0;

    switch (cmd)
    {
    case CMD_RESET:
    case CMD_STATUS:
    {
        bytes = 4;
        break;
    }
    case CMD_READ:
    {
        bytes = 6;
        break;
    }
    case CMD_WRITE:
    {
        bytes = 1;
        break;
    }
    default:
    {
        bytes = 1;
        break;
    }
    }
    return bytes * GetGCTicksPerSec() / BYTES_PER_SECOND;
}

void Endpoint::clockSync()
{
    if (!m_clockSocket)
    {
        m_running = false;
        return;
    }

    u32 TickDelta = 0;
    if (!m_lastGCTick)
    {
        m_lastGCTick = GetGCTicks();
        TickDelta = GetGCTicksPerSec() / 60;
    }
    else
        TickDelta = GetGCTicks() - m_lastGCTick;

    /* Scale GameCube clock into GBA clock */
    TickDelta = u32(u64(TickDelta) * 16777216 / GetGCTicksPerSec());
    m_lastGCTick = GetGCTicks();
    TickDelta = SBig(TickDelta);
    if (m_clockSocket.send(&TickDelta, 4) == net::Socket::EResult::Error)
        m_running = false;
}

void Endpoint::send(const u8* buffer)
{
    m_lastCmd = buffer[0];

    net::Socket::EResult result;
    size_t sentBytes;
    if (m_lastCmd == CMD_WRITE)
        result = m_dataSocket.send(buffer, 5, sentBytes);
    else
        result = m_dataSocket.send(buffer, 1, sentBytes);

    if (m_lastCmd != CMD_STATUS)
        m_booted = true;

    if (result != net::Socket::EResult::OK)
    {
        m_running = false;
    }
#if LOG_TRANSFER
    else
    {
        printf("Send %02x [> %02x%02x%02x%02x] (%lu)\n", buffer[0],
               buffer[1], buffer[2], buffer[3], buffer[4], sentBytes);
    }
#endif

    m_timeCmdSent = GetGCTicks();
}

size_t Endpoint::seceive(u8* buffer)
{
    if (!m_dataSocket)
    {
        m_running = false;
        return 5;
    }

    size_t recvBytes = 0;
    u64 transferTime = getTransferTime(m_lastCmd);
    bool block = (GetGCTicks() - m_timeCmdSent) > transferTime;
    if (m_lastCmd == CMD_STATUS && !m_booted)
        block = false;

    if (block)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_dataSocket.GetInternalSocket(), &fds);
        struct timeval tv = {};
        tv.tv_sec = 1;
        select(m_dataSocket.GetInternalSocket() + 1, &fds, NULL, NULL, &tv);
    }

    net::Socket::EResult result = m_dataSocket.recv(buffer, 5, recvBytes);
    if (result == net::Socket::EResult::Busy)
    {
        recvBytes = 0;
    }
    else if (result == net::Socket::EResult::Error)
    {
        m_running = false;
        return 5;
    }

    if (recvBytes > 5)
        recvBytes = 5;

#if LOG_TRANSFER
    if (recvBytes > 0)
    {
        if (m_lastCmd == CMD_STATUS || m_lastCmd == CMD_RESET)
        {
            printf("Stat/Reset [< %02x%02x%02x%02x%02x] (%lu)\n",
                   (u8)buffer[0], (u8)buffer[1], (u8)buffer[2],
                   (u8)buffer[3], (u8)buffer[4], recvBytes);
        }
        else
        {
            printf("Receive [< %02x%02x%02x%02x%02x] (%lu)\n",
                   (u8)buffer[0], (u8)buffer[1], (u8)buffer[2],
                   (u8)buffer[3], (u8)buffer[4], recvBytes);
        }
    }
#endif

    return recvBytes;
}

size_t Endpoint::runBuffer(u8* buffer, u64& remTicks, EWaitResp resp)
{
    if (m_waitingResp == EWaitResp::NoWait)
    {
        m_dataReceivedBytes = 0;
        clockSync();
        send(buffer);
        m_timeSent = GetGCTicks();
        m_waitingResp = resp;
    }

    if (m_waitingResp != EWaitResp::NoWait && m_dataReceivedBytes == 0)
    {
        m_dataReceivedBytes = seceive(buffer);
    }

    u64 ticksSinceSend = GetGCTicks() - m_timeSent;
    u64 targetTransferTime = getTransferTime(m_lastCmd);
    if (targetTransferTime > ticksSinceSend)
    {
        remTicks = targetTransferTime - ticksSinceSend;
        return 0;
    }
    else
    {
        remTicks = 0;
        if (m_dataReceivedBytes != 0)
            m_waitingResp = EWaitResp::NoWait;
        return m_dataReceivedBytes;
    }
}

bool Endpoint::idleGetStatus(u64& remTicks)
{
    u8 buffer[] = { CMD_STATUS, 0, 0, 0, 0 };
    return runBuffer(buffer, remTicks, EWaitResp::WaitIdle);
}

void Endpoint::transferProc()
{
#if LOG_TRANSFER
    printf("Starting JoyBus transfer thread for channel %d\n", m_chan);
#endif

    std::unique_lock<std::mutex> lk(m_syncLock);
    while (m_running)
    {
        u64 remTicks;
        if ((m_cmdIssued && m_waitingResp != EWaitResp::WaitIdle) ||
            m_waitingResp == EWaitResp::WaitCmd)
        {
            /* This inner loop performs first-response on commands invoked from callback
             * on this thread; avoiding excessive locking */
            do
            {
                m_cmdIssued = false;
                if (runBuffer(m_buffer, remTicks, EWaitResp::WaitCmd) || !m_running)
                {
                    EJoyReturn xferStatus;
                    if (m_running)
                    {
                        xferStatus = GBA_READY;
                    }
                    else
                    {
                        xferStatus = GBA_NOT_READY;
                        remTicks = 0;
                    }

                    /* Handle message response */
                    switch (m_lastCmd) {
                    case CMD_RESET:
                    case CMD_STATUS:
                        if (m_statusPtr)
                            *m_statusPtr = m_buffer[2];
                        break;
                    case CMD_WRITE:
                        if (m_statusPtr)
                            *m_statusPtr = m_buffer[0];
                        break;
                    case CMD_READ:
                        if (m_statusPtr)
                            *m_statusPtr = m_buffer[4];
                        if (m_readDstPtr)
                            memmove(m_readDstPtr, m_buffer, 4);
                        break;
                    default:
                        break;
                    }

                    m_statusPtr = nullptr;
                    m_readDstPtr = nullptr;
                    if (m_callback)
                    {
                        FGBACallback cb = std::move(m_callback);
                        m_callback = {};
                        ThreadLocalEndpoint ep(*this);
                        cb(ep, xferStatus);
                    }

                    if (!m_running)
                        break;
                }
            } while (m_cmdIssued);
        }
        else if (!m_booted)
        {
            /* Poll bus with status messages when inactive */
            if (idleGetStatus(remTicks))
                remTicks = GetGCTicksPerSec() * 4 / 60;
        }
        else
        {
            /* Wait for the duration of a write otherwise */
            remTicks = getTransferTime(CMD_WRITE);
        }

        if (m_running && remTicks)
        {
            lk.unlock();
            WaitGCTicks(remTicks);
            lk.lock();
        }
    }

    m_syncCv.notify_all();
    m_dataSocket.close();
    m_clockSocket.close();

#if LOG_TRANSFER
    printf("Stopping JoyBus transfer thread for channel %d\n", m_chan);
#endif
}

void Endpoint::transferWakeup(ThreadLocalEndpoint& endpoint, u8 status)
{
    m_syncCv.notify_all();
}

void Endpoint::stop()
{
    m_running = false;
    if (m_transferThread.joinable())
        m_transferThread.join();
}

EJoyReturn Endpoint::GBAGetProcessStatus(u8* percentp)
{
    if (m_joyBoot)
    {
        *percentp = m_joyBoot->percentComplete();
        if (!m_joyBoot->isDone())
            return GBA_BUSY;
    }

    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_BUSY;

    return GBA_READY;
}

EJoyReturn Endpoint::GBAGetStatusAsync(u8* status, FGBACallback&& callback)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_STATUS;
    m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAGetStatus(u8* status)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_STATUS;
    m_callback = bindSync();

    m_syncCv.wait(lk);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAResetAsync(u8* status, FGBACallback&& callback)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_RESET;
    m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAReset(u8* status)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_RESET;
    m_callback = bindSync();

    m_syncCv.wait(lk);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_readDstPtr = dst;
    m_buffer[0] = CMD_READ;
    m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn Endpoint::GBARead(u8* dst, u8* status)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_readDstPtr = dst;
    m_buffer[0] = CMD_READ;
    m_callback = bindSync();

    m_syncCv.wait(lk);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_WRITE;
    for (int i=0 ; i<4 ; ++i)
        m_buffer[i+1] = src[i];
    m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAWrite(const u8* src, u8* status)
{
    std::unique_lock<std::mutex> lk(m_syncLock);
    if (m_cmdIssued || m_waitingResp == EWaitResp::WaitCmd)
        return GBA_NOT_READY;

    m_cmdIssued = true;
    m_statusPtr = status;
    m_buffer[0] = CMD_WRITE;
    for (int i=0 ; i<4 ; ++i)
        m_buffer[i+1] = src[i];
    m_callback = bindSync();

    m_syncCv.wait(lk);

    return GBA_READY;
}

EJoyReturn Endpoint::GBAJoyBootAsync(s32 paletteColor, s32 paletteSpeed,
                                     u8* programp, s32 length, u8* status,
                                     FGBACallback&& callback)
{
    if (m_chan > 3)
        return GBA_JOYBOOT_ERR_INVALID;

    if (!length || length >= 0x40000)
        return GBA_JOYBOOT_ERR_INVALID;

    if (paletteSpeed < -4 || paletteSpeed > 4)
        return GBA_JOYBOOT_ERR_INVALID;

    if (paletteColor < 0 || paletteColor > 6)
        return GBA_JOYBOOT_ERR_INVALID;

    if (programp[0xac] * programp[0xac] * programp[0xac] * programp[0xac] == 0)
        return GBA_JOYBOOT_ERR_INVALID;

    m_joyBoot.emplace(*this, paletteColor, paletteSpeed, programp, length, status,
                      std::move(callback));
    if (!m_joyBoot->started())
        return GBA_NOT_READY;

    return GBA_READY;
}

Endpoint::Endpoint(u8 chan, net::Socket&& data, net::Socket&& clock)
: m_dataSocket(std::move(data)), m_clockSocket(std::move(clock)), m_chan(chan)
{
    m_transferThread = std::thread(std::bind(&Endpoint::transferProc, this));
}

Endpoint::~Endpoint() { stop(); }

EJoyReturn ThreadLocalEndpoint::GBAGetStatusAsync(u8* status, FGBACallback&& callback)
{
    if (m_ep.m_cmdIssued)
        return GBA_NOT_READY;

    m_ep.m_cmdIssued = true;
    m_ep.m_statusPtr = status;
    m_ep.m_buffer[0] = Endpoint::CMD_STATUS;
    m_ep.m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAResetAsync(u8* status, FGBACallback&& callback)
{
    if (m_ep.m_cmdIssued)
        return GBA_NOT_READY;

    m_ep.m_cmdIssued = true;
    m_ep.m_statusPtr = status;
    m_ep.m_buffer[0] = Endpoint::CMD_RESET;
    m_ep.m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback)
{
    if (m_ep.m_cmdIssued)
        return GBA_NOT_READY;

    m_ep.m_cmdIssued = true;
    m_ep.m_statusPtr = status;
    m_ep.m_readDstPtr = dst;
    m_ep.m_buffer[0] = Endpoint::CMD_READ;
    m_ep.m_callback = std::move(callback);

    return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback)
{
    if (m_ep.m_cmdIssued)
        return GBA_NOT_READY;

    m_ep.m_cmdIssued = true;
    m_ep.m_statusPtr = status;
    m_ep.m_buffer[0] = Endpoint::CMD_WRITE;
    for (int i=0 ; i<4 ; ++i)
        m_ep.m_buffer[i+1] = src[i];
    m_ep.m_callback = std::move(callback);

    return GBA_READY;
}

}
