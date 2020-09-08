// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceAD16.h"

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace ExpansionInterface
{
CEXIAD16::CEXIAD16() = default;

void CEXIAD16::SetCS(int cs)
{
  if (cs)
    m_position = 0;
}

bool CEXIAD16::IsPresent() const
{
  return true;
}

void CEXIAD16::TransferByte(u8& byte)
{
  if (m_position == 0)
  {
    m_command = byte;
    INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: Command 0x{:02x}", byte);
  }
  else
  {
    switch (m_command)
    {
    case init:
    {
      m_ad16_register.U32 = 0x04120000;
      switch (m_position)
      {
      case 1:
        DEBUG_ASSERT(byte == 0x00);
        break;  // just skip
      case 2:
        byte = m_ad16_register.U8[3];
        break;
      case 3:
        byte = m_ad16_register.U8[2];
        break;
      case 4:
        byte = m_ad16_register.U8[1];
        break;
      case 5:
        byte = m_ad16_register.U8[0];
        break;
      }
      INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: Init byte {:02x}", byte);
      break;
    }

    case write:
    {
      switch (m_position)
      {
      case 1:
        m_ad16_register.U8[0] = byte;
        break;
      case 2:
        m_ad16_register.U8[1] = byte;
        break;
      case 3:
        m_ad16_register.U8[2] = byte;
        break;
      case 4:
        m_ad16_register.U8[3] = byte;
        break;
      }
      INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: Write byte 0x{:02x}", byte);
      if (m_position == 4)
      {
        // Based on http://hitmen.c02.at/files/yagcd/yagcd/chap10.html#sec10.6.2
        switch (m_ad16_register.U32)
        {
        case 1:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Initialized; cached 1",
                       m_ad16_register.U32);
          break;
        case 2:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Cached 2", m_ad16_register.U32);
          break;
        case 3:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Cached 3", m_ad16_register.U32);
          break;
        case 4:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: RAM test passed", m_ad16_register.U32);
          break;
        case 5:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: RAM test failed 1", m_ad16_register.U32);
          break;
        case 6:
          // Not sure what triggers this or 7; simply flipping a bit doesn't seem to be enough
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: RAM test failed 2", m_ad16_register.U32);
          break;
        case 7:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: RAM test failed 3", m_ad16_register.U32);
          break;
        case 8:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: System init", m_ad16_register.U32);
          break;
        case 9:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: DVD init", m_ad16_register.U32);
          break;
        case 0xa:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Card init", m_ad16_register.U32);
          break;
        case 0xb:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Video init", m_ad16_register.U32);
          break;
        case 0xc:
          INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: {:08x}: Final ready", m_ad16_register.U32);
          break;
        default:
          WARN_LOG_FMT(EXPANSIONINTERFACE, "AD16: unknown value {:08x}", m_ad16_register.U32);
          break;
        }
      }
      break;
    }

    case read:
    {
      switch (m_position)
      {
      case 1:
        byte = m_ad16_register.U8[0];
        break;
      case 2:
        byte = m_ad16_register.U8[1];
        break;
      case 3:
        byte = m_ad16_register.U8[2];
        break;
      case 4:
        byte = m_ad16_register.U8[3];
        break;
      }
      INFO_LOG_FMT(EXPANSIONINTERFACE, "AD16: Read byte 0x{:02x}", byte);
      break;
    }

    default:
      WARN_LOG_FMT(EXPANSIONINTERFACE, "AD16: Unknown command 0x{:02x} (byte {:02x})", m_command,
                   byte);
    }
  }

  m_position++;
}

void CEXIAD16::DoState(PointerWrap& p)
{
  p.Do(m_position);
  p.Do(m_command);
  p.Do(m_ad16_register);
}
}  // namespace ExpansionInterface
