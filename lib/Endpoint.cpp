#include "jbus/Endpoint.hpp"

#include <algorithm>

#define LOG_TRANSFER 0

#if LOG_TRANSFER
#include <cstdio>
#endif

namespace jbus {

#define ROUND_UP_8(val) (((val) + 7) & ~7)

void Endpoint::KawasedoChallenge::DSPSecParms::ProcessGBACrypto() {
  /* Unwrap key from challenge using 'sedo' magic number (to encrypt JoyBoot program) */
  x20_key = x0_gbaChallenge ^ 0x6f646573;

  /* Pack palette parameters */
  u16 paletteSpeedCoded;
  s16 logoSpeed = static_cast<s8>(x8_logoSpeed);
  if (logoSpeed < 0)
    paletteSpeedCoded = ((-logoSpeed + 2) * 2) | (x4_logoPalette << 4);
  else if (logoSpeed == 0)
    paletteSpeedCoded = (x4_logoPalette * 2) | 0x70;
  else /* logo_speed > 0 */
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

void Endpoint::KawasedoChallenge::_0Reset(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status != GBA_READY ||
      (status = endpoint.GBAResetAsync(x10_statusPtr, bindThis(&KawasedoChallenge::_1GetStatus))) != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_1GetStatus(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status == GBA_READY)
    if (*x10_statusPtr != GBA_JSTAT_SEND)
      status = GBA_JOYBOOT_UNKNOWN_STATE;

  if (status != GBA_READY || (status = endpoint.GBAGetStatusAsync(
                                  x10_statusPtr, bindThis(&KawasedoChallenge::_2ReadChallenge))) != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_2ReadChallenge(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status == GBA_READY)
    if (*x10_statusPtr != (GBA_JSTAT_PSF0 | GBA_JSTAT_SEND))
      status = GBA_JOYBOOT_UNKNOWN_STATE;

  if (status != GBA_READY || (status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr,
                                                             bindThis(&KawasedoChallenge::_3DSPCrypto))) != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_3DSPCrypto(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  } else {
    _DSPCryptoInit();
    _DSPCryptoDone(endpoint);
  }
}

void Endpoint::KawasedoChallenge::_DSPCryptoInit() {
  xf8_dspHmac.x0_gbaChallenge = reinterpret_cast<u32&>(x18_readBuf);
  xf8_dspHmac.x4_logoPalette = x0_pColor;
  xf8_dspHmac.x8_logoSpeed = x4_pSpeed;
  xf8_dspHmac.xc_progLength = xc_progLen;
  xf8_dspHmac.ProcessGBACrypto();
}

void Endpoint::KawasedoChallenge::_DSPCryptoDone(ThreadLocalEndpoint& endpoint) {
  x58_currentKey = xf8_dspHmac.x20_key;
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
  if ((status = endpoint.GBAWriteAsync(x1c_writeBuf, x10_statusPtr, bindThis(&KawasedoChallenge::_4TransmitProgram))) !=
      GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_4TransmitProgram(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
    return;
  }

#if LOG_TRANSFER
  printf("PROG [%d/%d]\n", x34_bytesSent, x64_totalBytes);
#endif
  if (x30_justStarted) {
    x30_justStarted = 0;
  } else {
    if (!(*x10_statusPtr & GBA_JSTAT_PSF1) || (*x10_statusPtr & GBA_JSTAT_PSF0) >> 4 != (x34_bytesSent & 4) >> 2) {
      x28_ticksAfterXf = 0;
      if (x14_callback) {
        x14_callback(endpoint, GBA_JOYBOOT_UNKNOWN_STATE);
        x14_callback = {};
      }
      return;
    }
    x34_bytesSent += 4;
  }

  if (x34_bytesSent <= x64_totalBytes) {
    u32 cryptWindow;
    if (x34_bytesSent != x64_totalBytes) {
      x20_byteInWindow = 0;
      cryptWindow = 0;
      while (x20_byteInWindow < 4) {
        if (xc_progLen) {
          cryptWindow |= *x8_progPtr++ << (x20_byteInWindow * 8);
          --xc_progLen;
        }
        ++x20_byteInWindow;
      }

      if (x34_bytesSent == 0xac) {
        x60_gameId = cryptWindow;
      } else if (x34_bytesSent == 0xc4) {
        cryptWindow = endpoint.getChan() << 0x8;
      }

      if (x34_bytesSent >= 0xc0) {
        u32 shiftWindow = cryptWindow;
        u32 shiftCrc = x38_crc;
        for (int i = 0; i < 32; ++i) {
          if ((shiftWindow ^ shiftCrc) & 0x1)
            shiftCrc = (shiftCrc >> 1) ^ 0xa1c1;
          else
            shiftCrc >>= 1;

          shiftWindow >>= 1;
        }
        x38_crc = shiftCrc;
      }

      if (x34_bytesSent == 0x1f8) {
        x3c_checkStore[0] = cryptWindow;
      } else if (x34_bytesSent == 0x1fc) {
        x20_byteInWindow = 1;
        x3c_checkStore[x20_byteInWindow] = cryptWindow;
      }
    } else {
      cryptWindow = x38_crc | x34_bytesSent << 16;
    }

    if (x34_bytesSent > 0xbf) {
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

    if (x20_byteInWindow < 4) {
      x3c_checkStore[2 + x20_byteInWindow] = cryptWindow;
      x3c_checkStore[5 - x20_byteInWindow] =
          x3c_checkStore[1 + x20_byteInWindow] * x3c_checkStore[4 - x20_byteInWindow];
      x3c_checkStore[4 + x20_byteInWindow] =
          x3c_checkStore[1 + x20_byteInWindow] * x3c_checkStore[1 - x20_byteInWindow];
      x3c_checkStore[7 - x20_byteInWindow] =
          x3c_checkStore[-1 + x20_byteInWindow] * x3c_checkStore[4 - x20_byteInWindow];
    }

    if ((status = endpoint.GBAWriteAsync(x1c_writeBuf, x10_statusPtr,
                                         bindThis(&KawasedoChallenge::_4TransmitProgram))) != GBA_READY) {
      x28_ticksAfterXf = 0;
      if (x14_callback) {
        x14_callback(endpoint, status);
        x14_callback = {};
      }
    }
  } else // x34_bytesWritten > x64_totalBytes
  {
    if ((status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr, bindThis(&KawasedoChallenge::_5StartBootPoll))) !=
        GBA_READY) {
      x28_ticksAfterXf = 0;
      if (x14_callback) {
        x14_callback(endpoint, status);
        x14_callback = {};
      }
    }
  }
}

void Endpoint::KawasedoChallenge::_5StartBootPoll(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status != GBA_READY ||
      (status = endpoint.GBAGetStatusAsync(x10_statusPtr, bindThis(&KawasedoChallenge::_6BootPoll))) != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_6BootPoll(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status == GBA_READY)
    if (*x10_statusPtr & (GBA_JSTAT_FLAGS_MASK | GBA_JSTAT_RECV))
      status = GBA_JOYBOOT_UNKNOWN_STATE;

  if (status != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
    return;
  }

  if (*x10_statusPtr != GBA_JSTAT_SEND) {
    if ((status = endpoint.GBAGetStatusAsync(x10_statusPtr, bindThis(&KawasedoChallenge::_6BootPoll))) != GBA_READY) {
      x28_ticksAfterXf = 0;
      if (x14_callback) {
        x14_callback(endpoint, status);
        x14_callback = {};
      }
    }
    return;
  }

  if ((status = endpoint.GBAReadAsync(x18_readBuf, x10_statusPtr, bindThis(&KawasedoChallenge::_7BootAcknowledge))) !=
      GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_7BootAcknowledge(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status != GBA_READY || (status = endpoint.GBAWriteAsync(x18_readBuf, x10_statusPtr,
                                                              bindThis(&KawasedoChallenge::_8BootDone))) != GBA_READY) {
    x28_ticksAfterXf = 0;
    if (x14_callback) {
      x14_callback(endpoint, status);
      x14_callback = {};
    }
  }
}

void Endpoint::KawasedoChallenge::_8BootDone(ThreadLocalEndpoint& endpoint, EJoyReturn status) {
  if (status == GBA_READY)
    *x10_statusPtr = 0;

  x28_ticksAfterXf = 0;

  if (x14_callback) {
    x14_callback(endpoint, status);
    x14_callback = {};
  }
}

Endpoint::KawasedoChallenge::KawasedoChallenge(s32 paletteColor, s32 paletteSpeed, const u8* programp, s32 length,
                                               u8* status, FGBACallback&& callback)
: x0_pColor(paletteColor)
, x4_pSpeed(paletteSpeed)
, x8_progPtr(programp)
, xc_progLen(length)
, x10_statusPtr(status)
, x14_callback(std::move(callback))
, x34_bytesSent(0)
, m_initialized(true) {}

void Endpoint::KawasedoChallenge::start(Endpoint& endpoint) {
  if (endpoint.GBAGetStatusAsync(x10_statusPtr, bindThis(&KawasedoChallenge::_0Reset)) != GBA_READY) {
    x14_callback = {};
    m_started = false;
  }
}

void Endpoint::clockSync() {
  if (!m_clockSocket) {
    m_running = false;
    return;
  }

  u32 TickDelta = 0;
  if (!m_lastGCTick) {
    m_lastGCTick = GetGCTicks();
    TickDelta = GetGCTicksPerSec() / 60;
  } else
    TickDelta = GetGCTicks() - m_lastGCTick;

  /* Scale GameCube clock into GBA clock */
  TickDelta = u32(u64(TickDelta) * 16777216 / GetGCTicksPerSec());
  m_lastGCTick = GetGCTicks();
  TickDelta = SBig(TickDelta);
  if (m_clockSocket.send(&TickDelta, 4) == net::Socket::EResult::Error)
    m_running = false;
}

void Endpoint::send(Buffer buffer) {
  m_lastCmd = buffer[0];

  net::Socket::EResult result;
  size_t sentBytes;
  if (m_lastCmd == CMD_WRITE) {
    result = m_dataSocket.send(buffer.data(), buffer.size(), sentBytes);
  } else {
    result = m_dataSocket.send(buffer.data(), 1, sentBytes);
  }

  if (m_lastCmd != CMD_STATUS) {
    m_booted = true;
  }

  if (result != net::Socket::EResult::OK) {
    m_running = false;
  }
#if LOG_TRANSFER
  else {
    printf("Send %02x [> %02x%02x%02x%02x] (%lu)\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], sentBytes);
  }
#endif
}

size_t Endpoint::receive(Buffer& buffer) {
  if (!m_dataSocket) {
    m_running = false;
    return buffer.size();
  }

  size_t recvBytes = 0;
  const net::Socket::EResult result = m_dataSocket.recv(buffer.data(), buffer.size(), recvBytes);
  if (result == net::Socket::EResult::Error) {
    m_running = false;
    return buffer.size();
  }

  if (recvBytes > buffer.size()) {
    recvBytes = buffer.size();
  }

#if LOG_TRANSFER
  if (recvBytes > 0) {
    if (m_lastCmd == CMD_STATUS || m_lastCmd == CMD_RESET) {
      printf("Stat/Reset [< %02x%02x%02x%02x%02x] (%lu)\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
             recvBytes);
    } else {
      printf("Receive [< %02x%02x%02x%02x%02x] (%lu)\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
             recvBytes);
    }
  }
#endif

  return recvBytes;
}

size_t Endpoint::runBuffer(Buffer& buffer, std::unique_lock<std::mutex>& lk) {
  Buffer tmpBuffer = buffer;

  lk.unlock();
  clockSync();
  send(tmpBuffer);
  const size_t receivedBytes = receive(tmpBuffer);
  lk.lock();

  buffer = tmpBuffer;
  return receivedBytes;
}

bool Endpoint::idleGetStatus(std::unique_lock<std::mutex>& lk) {
  Buffer buffer{u8(CMD_STATUS), 0, 0, 0, 0};
  return runBuffer(buffer, lk) != 0;
}

void Endpoint::transferProc() {
#if LOG_TRANSFER
  printf("Starting JoyBus transfer thread for channel %d\n", m_chan);
#endif

  /* This lock is relinquished on I/O cycles or when waiting for next request */
  std::unique_lock<std::mutex> lk(m_syncLock);
  while (m_running) {
    if (m_cmdIssued) {
      /* Synchronous command write/read cycle */
      runBuffer(m_buffer, lk);
      m_cmdIssued = false;

      EJoyReturn xferStatus = m_running ? GBA_READY : GBA_NOT_READY;

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
        if (m_statusPtr != nullptr) {
          *m_statusPtr = m_buffer[4];
        }
        if (m_readDstPtr != nullptr) {
          std::copy(m_buffer.cbegin(), m_buffer.cbegin() + 4, m_readDstPtr);
        }
        break;
      default:
        break;
      }

      m_statusPtr = nullptr;
      m_readDstPtr = nullptr;
      if (m_callback) {
        FGBACallback cb = std::move(m_callback);
        m_callback = {};
        ThreadLocalEndpoint ep(*this);
        cb(ep, xferStatus);
      }
    } else if (!m_booted) {
      /* Poll bus with status messages when inactive */
      if (idleGetStatus(lk)) {
        lk.unlock();
        WaitGCTicks(GetGCTicksPerSec() * 4 / 60);
        lk.lock();
      }
    } else {
      /* Wait for next user request */
      m_issueCv.wait(lk);
    }
  }

  m_syncCv.notify_all();
  m_dataSocket.close();
  m_clockSocket.close();

#if LOG_TRANSFER
  printf("Stopping JoyBus transfer thread for channel %d\n", m_chan);
#endif
}

void Endpoint::transferWakeup(ThreadLocalEndpoint& endpoint, u8 status) { m_syncCv.notify_all(); }

void Endpoint::stop() {
  m_running = false;
  m_issueCv.notify_one();
  if (m_transferThread.joinable())
    m_transferThread.join();
}

EJoyReturn Endpoint::GBAGetProcessStatus(u8& percentOut) {
  if (!m_running)
    return GBA_NOT_READY;

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_joyBoot) {
    percentOut = m_joyBoot.percentComplete();
    if (!m_joyBoot.isDone())
      return GBA_BUSY;
  }

  if (m_cmdIssued)
    return GBA_BUSY;

  return GBA_READY;
}

EJoyReturn Endpoint::GBAGetStatusAsync(u8* status, FGBACallback&& callback) {
  if (!m_running)
    return GBA_NOT_READY;

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued)
    return GBA_NOT_READY;

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_STATUS;
  m_callback = std::move(callback);

  m_issueCv.notify_one();

  return GBA_READY;
}

EJoyReturn Endpoint::GBAGetStatus(u8* status) {
  if (!m_running)
    return GBA_NOT_READY;

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued)
    return GBA_NOT_READY;

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_STATUS;
  m_callback = bindSync();

  m_issueCv.notify_one();
  m_syncCv.wait(lk);

  return GBA_READY;
}

EJoyReturn Endpoint::GBAResetAsync(u8* status, FGBACallback&& callback) {
  if (!m_running)
    return GBA_NOT_READY;

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued)
    return GBA_NOT_READY;

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_RESET;
  m_callback = std::move(callback);

  m_issueCv.notify_one();

  return GBA_READY;
}

EJoyReturn Endpoint::GBAReset(u8* status) {
  if (!m_running)
    return GBA_NOT_READY;

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued)
    return GBA_NOT_READY;

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_RESET;
  m_callback = bindSync();

  m_issueCv.notify_one();
  m_syncCv.wait(lk);

  return GBA_READY;
}

EJoyReturn Endpoint::GBAReadAsync(ReadWriteBuffer& dst, u8* status, FGBACallback&& callback) {
  if (!m_running) {
    return GBA_NOT_READY;
  }

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued) {
    return GBA_NOT_READY;
  }

  m_cmdIssued = true;
  m_statusPtr = status;
  m_readDstPtr = dst.data();
  m_buffer[0] = CMD_READ;
  m_callback = std::move(callback);

  m_issueCv.notify_one();

  return GBA_READY;
}

EJoyReturn Endpoint::GBARead(ReadWriteBuffer& dst, u8* status) {
  if (!m_running) {
    return GBA_NOT_READY;
  }

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued) {
    return GBA_NOT_READY;
  }

  m_cmdIssued = true;
  m_statusPtr = status;
  m_readDstPtr = dst.data();
  m_buffer[0] = CMD_READ;
  m_callback = bindSync();

  m_issueCv.notify_one();
  m_syncCv.wait(lk);

  return GBA_READY;
}

EJoyReturn Endpoint::GBAWriteAsync(ReadWriteBuffer src, u8* status, FGBACallback&& callback) {
  if (!m_running) {
    return GBA_NOT_READY;
  }

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued) {
    return GBA_NOT_READY;
  }

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_WRITE;
  for (size_t i = 0; i < src.size(); ++i) {
    m_buffer[i + 1] = src[i];
  }
  m_callback = std::move(callback);

  m_issueCv.notify_one();

  return GBA_READY;
}

EJoyReturn Endpoint::GBAWrite(ReadWriteBuffer src, u8* status) {
  if (!m_running) {
    return GBA_NOT_READY;
  }

  std::unique_lock<std::mutex> lk(m_syncLock);
  if (m_cmdIssued) {
    return GBA_NOT_READY;
  }

  m_cmdIssued = true;
  m_statusPtr = status;
  m_buffer[0] = CMD_WRITE;
  for (size_t i = 0; i < src.size(); ++i) {
    m_buffer[i + 1] = src[i];
  }
  m_callback = bindSync();

  m_issueCv.notify_one();
  m_syncCv.wait(lk);

  return GBA_READY;
}

EJoyReturn Endpoint::GBAJoyBootAsync(s32 paletteColor, s32 paletteSpeed, const u8* programp, s32 length, u8* status,
                                     FGBACallback&& callback) {
  if (!m_running)
    return GBA_NOT_READY;

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

  m_joyBoot = KawasedoChallenge(paletteColor, paletteSpeed, programp, length, status, std::move(callback));
  m_joyBoot.start(*this);
  if (!m_joyBoot.started())
    return GBA_NOT_READY;

  return GBA_READY;
}

Endpoint::Endpoint(u8 chan, net::Socket&& data, net::Socket&& clock)
: m_dataSocket(std::move(data)), m_clockSocket(std::move(clock)), m_chan(chan) {
  m_transferThread = std::thread(std::bind(&Endpoint::transferProc, this));
}

Endpoint::~Endpoint() { stop(); }

EJoyReturn ThreadLocalEndpoint::GBAGetStatusAsync(u8* status, FGBACallback&& callback) {
  if (!m_ep.m_running || m_ep.m_cmdIssued)
    return GBA_NOT_READY;

  m_ep.m_cmdIssued = true;
  m_ep.m_statusPtr = status;
  m_ep.m_buffer[0] = Endpoint::CMD_STATUS;
  m_ep.m_callback = std::move(callback);

  return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAResetAsync(u8* status, FGBACallback&& callback) {
  if (!m_ep.m_running || m_ep.m_cmdIssued)
    return GBA_NOT_READY;

  m_ep.m_cmdIssued = true;
  m_ep.m_statusPtr = status;
  m_ep.m_buffer[0] = Endpoint::CMD_RESET;
  m_ep.m_callback = std::move(callback);

  return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAReadAsync(ReadWriteBuffer& dst, u8* status, FGBACallback&& callback) {
  if (!m_ep.m_running || m_ep.m_cmdIssued)
    return GBA_NOT_READY;

  m_ep.m_cmdIssued = true;
  m_ep.m_statusPtr = status;
  m_ep.m_readDstPtr = dst.data();
  m_ep.m_buffer[0] = Endpoint::CMD_READ;
  m_ep.m_callback = std::move(callback);

  return GBA_READY;
}

EJoyReturn ThreadLocalEndpoint::GBAWriteAsync(ReadWriteBuffer src, u8* status, FGBACallback&& callback) {
  if (!m_ep.m_running || m_ep.m_cmdIssued)
    return GBA_NOT_READY;

  m_ep.m_cmdIssued = true;
  m_ep.m_statusPtr = status;
  m_ep.m_buffer[0] = Endpoint::CMD_WRITE;
  for (size_t i = 0; i < src.size(); ++i) {
    m_ep.m_buffer[i + 1] = src[i];
  }
  m_ep.m_callback = std::move(callback);

  return GBA_READY;
}

} // namespace jbus
