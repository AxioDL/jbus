#ifndef JBUS_ENDPOINT_HPP
#define JBUS_ENDPOINT_HPP

#include "Common.hpp"
#include "Socket.hpp"
#include "optional.hpp"
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
            u32 x20_publicKey;

            /* Message authentication code */
            u32 x24_authInitCode;

            void ProcessGBACrypto()
            {
                /* Unwrap key from challenge using 'sedo' magic number (to encrypt JoyBoot program) */
                x20_publicKey = x0_gbaChallenge ^ 0x6f646573;

                /* Pack palette parameters */
                u16 paletteSpeedCoded;
                s16 logoSpeed = static_cast<s8>(x8_logoSpeed);
                if (logoSpeed < 0)
                  paletteSpeedCoded = ((-logoSpeed + 2) * 2) | (x4_logoPalette << 4);
                else if (logoSpeed == 0)
                  paletteSpeedCoded = (x4_logoPalette * 2) | 0x70;
                else  /* logo_speed > 0 */
                  paletteSpeedCoded = ((logoSpeed - 1) * 2) | (x4_logoPalette << 4);

                /* JoyBoot ROMs start with a padded header; this is the length beyond that header */
                s32 lengthNoHeader = ROUND_UP_8(xc_progLength) - 0x200;

                /* The JoyBus protocol transmits in 4-byte packets while flipping a state flag;
                 * so the GBA BIOS counts the program length in 8-byte packet-pairs */
                u16 packetPairCount = (lengthNoHeader < 0) ? 0 : lengthNoHeader / 8;
                paletteSpeedCoded |= (packetPairCount & 0x4000) >> 14;

                /* Pack together encoded transmission parameters */
                u32 t1 = (((packetPairCount << 16) | 0x3f80) & 0x3f80ffff) * 2;
                t1 += (static_cast<s16>(static_cast<s8>(t1 >> 8)) & packetPairCount) << 16;
                u32 t2 = ((paletteSpeedCoded & 0xff) << 16) + (t1 & 0xff0000) + ((t1 >> 8) & 0xffff00);
                u32 t3 = paletteSpeedCoded << 16 | ((t2 << 8) & 0xff000000) | (t1 >> 16) | 0x80808080;

                /* Wrap with 'Kawa' or 'sedo' (Kawasedo is the author of the BIOS cipher) */
                x24_authInitCode = t3 ^ ((t3 & 0x200) != 0 ? 0x6f646573 : 0x6177614b);
            }
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
        KawasedoChallenge(Endpoint& endpoint, s32 paletteColor, s32 paletteSpeed,
                          const u8* programp, s32 length, u8* status, FGBACallback&& callback);
        bool started() const { return m_started; }
        u8 percentComplete() const
        {
            if (!x64_totalBytes)
                return 0;
            return x34_bytesSent * 100 / x64_totalBytes;
        }
        bool isDone() const { return !x14_callback; }
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
    std::experimental::optional<KawasedoChallenge> m_joyBoot;
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
     *  Further use of this Endpoint is undefined behavior.
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
     *  @return SI channel */
    int GetChan() const { return m_chan; }

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
    int GetChan() const { return m_ep.GetChan(); }
};

}

#endif // JBUS_ENDPOINT_HPP
