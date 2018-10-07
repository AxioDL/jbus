#pragma once

#include "Common.hpp"
#include "Socket.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

namespace jbus
{

/** Main class for performing JoyBoot and subsequent JoyBus I/O operations.
 *  Instances should be obtained though the jbus::Listener::accept method. */
class Endpoint
{
    /** Self-contained class for solving Kawasedo's GBA BootROM challenge.
     *  GBA will boot client_pad.bin code on completion.
     *
     *  This class shouldn't be used directly. JoyBoot operations are started
     *  via jbus::Endpoint::GBAJoyBootAsync. The JoyBoot status may be obtained
     *  via jbus::Endpoint::GBAGetProcessStatus. */
    class KawasedoChallenge
    {
        /** DSP-hosted public-key unwrap and initial message crypt
         *  Reference: https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/DSPHLE/UCodes/GBA.cpp */
        struct DSPSecParms
        {
            /* Nonce challenge (first read from GBA, hence already little-endian) */
            u32 x0_gbaChallenge;

            /* Palette of pulsing logo on GBA during transmission [0,6] */
            u32 x4_logoPalette;

            /* Speed and direction of palette interpolation [-4,4] */
            u32 x8_logoSpeed;

            /* Length of JoyBoot program to upload */
            u32 xc_progLength;

            /* Unwrapped public key */
            u32 x20_key;

            /* Message authentication code */
            u32 x24_authInitCode;

            void ProcessGBACrypto();
        } xf8_dspHmac;

        s32 x0_pColor;
        s32 x4_pSpeed;
        const u8* x8_progPtr;
        u32 xc_progLen;
        u8* x10_statusPtr;
        FGBACallback x14_callback;
        u8 x18_readBuf[4];
        u8 x1c_writeBuf[4];
        s32 x20_byteInWindow;
        u64 x28_ticksAfterXf;
        u32 x30_justStarted;
        u32 x34_bytesSent;
        u32 x38_crc;
        u32 x3c_checkStore[7];
        s32 x58_currentKey;
        s32 x5c_initMessage;
        s32 x60_gameId;
        u32 x64_totalBytes;
        bool m_started = true;
		bool m_initialized = false;

        void _0Reset(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _1GetStatus(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _2ReadChallenge(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _3DSPCrypto(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _DSPCryptoInit();
        void _DSPCryptoDone(ThreadLocalEndpoint& endpoint);
        void _4TransmitProgram(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _5StartBootPoll(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _6BootPoll(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _7BootAcknowledge(ThreadLocalEndpoint& endpoint, EJoyReturn status);
        void _8BootDone(ThreadLocalEndpoint& endpoint, EJoyReturn status);

        auto bindThis(void(KawasedoChallenge::*ptmf)(ThreadLocalEndpoint&, EJoyReturn))
        {
            return std::bind(ptmf, this, std::placeholders::_1, std::placeholders::_2);
        }

    public:
		KawasedoChallenge() = default;
        KawasedoChallenge(s32 paletteColor, s32 paletteSpeed,
                          const u8* programp, s32 length, u8* status, FGBACallback&& callback);
        void start(Endpoint& endpoint);
        bool started() const { return m_started; }
        u8 percentComplete() const
        {
            if (!x64_totalBytes)
                return 0;
            return x34_bytesSent * 100 / x64_totalBytes;
        }
        bool isDone() const { return !x14_callback; }
		operator bool() const { return m_initialized; }
    };

    friend class ThreadLocalEndpoint;

    enum EJoybusCmds
    {
        CMD_RESET = 0xff,
        CMD_STATUS = 0x00,
        CMD_READ = 0x14,
        CMD_WRITE = 0x15
    };

    static const u64 BITS_PER_SECOND = 115200;
    static const u64 BYTES_PER_SECOND = BITS_PER_SECOND / 8;

    net::Socket m_dataSocket;
    net::Socket m_clockSocket;
    std::thread m_transferThread;
    std::mutex m_syncLock;
    std::condition_variable m_syncCv;
    std::condition_variable m_issueCv;
    KawasedoChallenge m_joyBoot;
    FGBACallback m_callback;
    u8 m_buffer[5];
    u8* m_readDstPtr = nullptr;
    u8* m_statusPtr = nullptr;
    u64 m_lastGCTick = 0;
    u8 m_lastCmd = 0;
    u8 m_chan;
    bool m_booted = false;
    bool m_cmdIssued = false;
    bool m_running = true;

    void clockSync();
    void send(const u8* buffer);
    size_t receive(u8* buffer);
    size_t runBuffer(u8* buffer, std::unique_lock<std::mutex>& lk);
    bool idleGetStatus(std::unique_lock<std::mutex>& lk);
    void transferProc();
    void transferWakeup(ThreadLocalEndpoint& endpoint, u8 status);

    auto bindSync()
    {
        return std::bind(&Endpoint::transferWakeup, this,
                         std::placeholders::_1, std::placeholders::_2);
    }

public:
    /** @brief Request stop of I/O thread and block until joined.
     *  Further use of this Endpoint will return GBA_NOT_READY.
     *  The destructor calls this implicitly. */
    void stop();

    /** @brief Get status of last asynchronous operation.
     *  @param percentOut Reference to output transfer percent of GBAJoyBootAsync.
     *  @return GBA_READY when idle, or GBA_BUSY when operation in progress. */
    EJoyReturn GBAGetProcessStatus(u8& percentOut);

    /** @brief Get JOYSTAT register from GBA asynchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAGetStatusAsync(u8* status, FGBACallback&& callback);

    /** @brief Get JOYSTAT register from GBA synchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAGetStatus(u8* status);

    /** @brief Send RESET command to GBA asynchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAResetAsync(u8* status, FGBACallback&& callback);

    /** @brief Send RESET command to GBA synchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAReset(u8* status);

    /** @brief Send READ command to GBA asynchronously.
     *  @param dst Destination pointer for 4-byte packet of data.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback);

    /** @brief Send READ command to GBA synchronously.
     *  @param dst Destination pointer for 4-byte packet of data.
     *  @param status Destination pointer for EJStatFlags.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBARead(u8* dst, u8* status);

    /** @brief Send WRITE command to GBA asynchronously.
     *  @param src Source pointer for 4-byte packet of data. It is not required to keep resident.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback);

    /** @brief Send WRITE command to GBA synchronously.
     *  @param src Source pointer for 4-byte packet of data. It is not required to keep resident.
     *  @param status Destination pointer for EJStatFlags.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAWrite(const u8* src, u8* status);

    /** @brief Initiate JoyBoot sequence on this endpoint.
     *  @param paletteColor Palette for displaying logo in ROM header [0,6].
     *  @param paletteSpeed Palette interpolation speed for displaying logo in ROM header [-4,4].
     *  @param programp Pointer to program ROM data.
     *  @param length Length of program ROM data.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAJoyBootAsync(s32 paletteColor, s32 paletteSpeed,
                               const u8* programp, s32 length, u8* status,
                               FGBACallback&& callback);

    /** @brief Get virtual SI channel assigned to this endpoint.
     *  @return SI channel [0,3] */
    unsigned getChan() const { return m_chan; }

    /** @brief Set virtual SI channel assigned to this endpoint.
     *  @param chan SI channel [0,3] */
    void setChan(unsigned chan)
    {
        if (chan > 3)
            chan = 3;
        m_chan = chan;
    }

    /** @brief Get connection status of this endpoint
     *  @return true if connected */
    bool connected() const { return m_running; }

    Endpoint(u8 chan, net::Socket&& data, net::Socket&& clock);
    ~Endpoint();
};

/** Lockless wrapper interface for jbus::Endpoint.
 *  This class is constructed internally and supplied as a callback argument.
 *  It should not be constructed directly. */
class ThreadLocalEndpoint
{
    friend class Endpoint;
    Endpoint& m_ep;
    ThreadLocalEndpoint(Endpoint& ep) : m_ep(ep) {}

public:
    /** @brief Get JOYSTAT register from GBA asynchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAGetStatusAsync(u8* status, FGBACallback&& callback);

    /** @brief Send RESET command to GBA asynchronously.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAResetAsync(u8* status, FGBACallback&& callback);

    /** @brief Send READ command to GBA asynchronously.
     *  @param dst Destination pointer for 4-byte packet of data.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback);

    /** @brief Send WRITE command to GBA asynchronously.
     *  @param src Source pointer for 4-byte packet of data. It is not required to keep resident.
     *  @param status Destination pointer for EJStatFlags.
     *  @param callback Functor to execute when operation completes.
     *  @return GBA_READY if submitted, or GBA_NOT_READY if another operation in progress. */
    EJoyReturn GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback);

    /** @brief Get virtual SI channel assigned to this endpoint.
     *  @return SI channel */
    int getChan() const { return m_ep.getChan(); }
};

}

