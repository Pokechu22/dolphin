// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>

#include "Common/BitUtils.h"
#include "Core/HW/EXI/EXI_Device.h"

class PointerWrap;

namespace ExpansionInterface
{
class CEXIIPL : public IEXIDevice
{
public:
  CEXIIPL();
  ~CEXIIPL() override;

  void SetCS(int cs) override;
  bool IsPresent() const override;
  void DoState(PointerWrap& p) override;

  static constexpr u32 UNIX_EPOCH = 0;         // 1970-01-01 00:00:00
  static constexpr u32 GC_EPOCH = 0x386D4380;  // 2000-01-01 00:00:00

  static u32 GetEmulatedTime(u32 epoch);
  static u64 NetPlay_GetEmulatedTime();

  static bool HasIPLDump();

  u32 ReadDecryptedIPL(u32 addr);

private:
  static constexpr u32 ROM_BASE = 0;
  static constexpr u32 ROM_SIZE = 0x200000;
  static constexpr u32 ROM_NAME_START = 0;
  static constexpr u32 ROM_NAME_LENGTH = 0x100;
  static constexpr u32 ROM_SCRAMBLE_START = 0x100;
  static constexpr u32 ROM_SCRAMBLE_LENGTH = 0x1afe00;
  static constexpr u32 ROM_SHIFT_JIS_FONT_START = 0x1aff00;
  static constexpr u32 ROM_SHIFT_JIS_FONT_LENGTH = 0x4a24d;
  static constexpr u32 ROM_WINDOWS_1252_FONT_START = 0x1fcf00;
  static constexpr u32 ROM_WINDOWS_1252_FONT_LENGTH = 0x2575;

  // TODO these ranges are highly suspect
  static constexpr u32 SRAM_BASE = 0x800000;
  static constexpr u32 SRAM_SIZE = 0x44;
  static constexpr u32 UART_BASE = 0x800400;
  static constexpr u32 UART_SIZE = 0x50;
  static constexpr u32 WII_RTC_BASE = 0x840000;
  static constexpr u32 WII_RTC_SIZE = 0x40;
  static constexpr u32 EUART_BASE = 0xc00000;
  static constexpr u32 EUART_SIZE = 8;

  std::array<u8, ROM_SIZE> m_rom;

  struct
  {
    bool is_write() const { return (value >> 31) & 1; }
    // TODO this is definitely a guess
    // Also, the low 6 bits are completely ignored
    u32 address() const { return (value >> 6) & 0x1ffffff; }
    u32 low_bits() const { return value & 0x3f; }
    u32 value;
  } m_command{};
  u32 m_command_bytes_received{};
  // Technically each device has it's own state, but we assume the selected
  // device will not change without toggling cs, and that each device has at
  // most 1 interesting position to keep track of.
  u32 m_cursor{};

  std::string m_buffer;
  bool m_fonts_loaded{};

  void UpdateRTC();

  void TransferByte(u8& data) override;

  bool LoadFileToIPL(const std::string& filename, u32 offset, u32 file_offset, u64 size);
  bool LoadWholeFileToIPL(const std::string& filename, u32 offset, u64 size);
  void LoadFontFile(const std::string& filename, bool jis);

  static std::string FindIPLDump(const std::string& path_prefix);
};

// Used to indicate disc changes on the Wii, as insane as that sounds.
// However, the name is definitely RTCFlag, as the code that gets it is __OSGetRTCFlags and
// __OSClearRTCFlags in OSRtc.o (based on symbols from Kirby's Dream Collection)
// This may simply be a single byte that gets repeated 4 times by some EXI quirk,
// as reading it gives the value repeated 4 times but code only checks the first bit.
enum class RTCFlag : u32
{
  EjectButton = 0x01010101,
  DiscChanged = 0x02020202,
};

extern Common::Flags<RTCFlag> g_rtc_flags;
}  // namespace ExpansionInterface
