#ifndef JBUS_ENDPOINT_HPP
#define JBUS_ENDPOINT_HPP

#include "Common.hpp"
#include "Socket.hpp"
#include "optional.hpp"
#include <thread>
#include <mutex>

namespace jbus
{

/** Self-contained class for solving Kawasedo's GBA BootROM challenge.
 *  GBA will boot client_pad.bin code on completion. */
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
    u8* x8_progPtr;
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

    void F23(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F25(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F27(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F29(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void GBAX02();
    void GBAX01(ThreadLocalEndpoint& endpoint);
    void F31(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F33(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F35(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F37(ThreadLocalEndpoint& endpoint, EJoyReturn status);
    void F39(ThreadLocalEndpoint& endpoint, EJoyReturn status);

    auto bindThis(void(KawasedoChallenge::*ptmf)(ThreadLocalEndpoint&, EJoyReturn))
    {
        return std::bind(ptmf, this, std::placeholders::_1, std::placeholders::_2);
    }

public:
    KawasedoChallenge(Endpoint& endpoint, s32 paletteColor, s32 paletteSpeed,
                      u8* programp, s32 length, u8* status, FGBACallback&& callback);
    bool started() const { return m_started; }
    u8 percentComplete() const
    {
        if (!x64_totalBytes)
            return 0;
        return x34_bytesSent * 100 / x64_totalBytes;
    }
    bool isDone() const { return !x14_callback; }
};

class Endpoint
{
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

    static u64 getTransferTime(u8 cmd);
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
    void stop();
    EJoyReturn GBAGetProcessStatus(u8* percentp);
    EJoyReturn GBAGetStatusAsync(u8* status, FGBACallback&& callback);
    EJoyReturn GBAGetStatus(u8* status);
    EJoyReturn GBAResetAsync(u8* status, FGBACallback&& callback);
    EJoyReturn GBAReset(u8* status);
    EJoyReturn GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback);
    EJoyReturn GBARead(u8* dst, u8* status);
    EJoyReturn GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback);
    EJoyReturn GBAWrite(const u8* src, u8* status);
    EJoyReturn GBAJoyBootAsync(s32 paletteColor, s32 paletteSpeed,
                               u8* programp, s32 length, u8* status,
                               FGBACallback&& callback);
    int GetChan() const { return m_chan; }
    Endpoint(u8 chan, net::Socket&& data, net::Socket&& clock);
    ~Endpoint();
};

class ThreadLocalEndpoint
{
    friend class Endpoint;
    Endpoint& m_ep;
    ThreadLocalEndpoint(Endpoint& ep) : m_ep(ep) {}
public:
    EJoyReturn GBAGetStatusAsync(u8* status, FGBACallback&& callback);
    EJoyReturn GBAResetAsync(u8* status, FGBACallback&& callback);
    EJoyReturn GBAReadAsync(u8* dst, u8* status, FGBACallback&& callback);
    EJoyReturn GBAWriteAsync(const u8* src, u8* status, FGBACallback&& callback);
    int GetChan() const { return m_ep.GetChan(); }
};

}

#endif // JBUS_ENDPOINT_HPP
