// Copyright 2020 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI_Device.h"

class PointerWrap;

namespace ExpansionInterface
{
// EXI-SD adapter (DOL-019)
class CEXISD final : public IEXIDevice
{
public:
  explicit CEXISD();

  void ImmWrite(u32 data, u32 size) override;
  u32 ImmRead(u32 size) override;
  void ImmReadWrite(u32& data, u32 size) override;
  void SetCS(int cs) override;

  bool IsPresent() const override;
  void DoState(PointerWrap& p) override;

private:
  void TransferByte(u8& byte) override;

  enum class R1
  {
    InIdleState = 1 << 0,
    EraseRequest = 1 << 1,
    IllegalCommand = 1 << 2,
    CommunicationCRCError = 1 << 3,
    EraseSequenceError = 1 << 4,
    AddressError = 1 << 5,
    ParameterError = 1 << 6,
    // Top bit 0
  };
  enum class R2
  {
    CardIsLocked = 1 << 0,
    WriteProtectEraseSkip = 1 << 1,  // or lock/unlock command failed
    Error = 1 << 2,
    CardControllerError = 1 << 3,
    CardEccFailed = 1 << 4,
    WriteProtectViolation = 1 << 5,
    EraseParam = 1 << 6,
    // OUT_OF_RANGE_OR_CSD_OVERWRITE, not documented in text?
  };

  // STATE_TO_SAVE
  bool inited = false;
  bool get_id = false;
  u32 m_uPosition = 0;
  std::array<u8, 6> cmd = {};
  R1 result = static_cast<R1>(0);
};
}  // namespace ExpansionInterface
