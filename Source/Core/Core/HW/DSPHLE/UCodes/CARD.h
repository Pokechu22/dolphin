// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"
#include "Core/HW/DSPHLE/UCodes/UCodes.h"

namespace DSP::HLE
{
class DSPHLE;

class CARDUCode final : public UCodeInterface
{
public:
  CARDUCode(DSPHLE* dsphle, u32 crc);

  void Initialize() override;
  void HandleMail(u32 mail) override;
  void Update() override;
  void DoState(PointerWrap& p) override;

private:
  // The addresses listed here are written by the card uCode and read by the DSP ROM
  struct CardUcodeParameters
  {
    u32 mram_input_addr;   // high: 0400, low: 0401
    u16 unused;            // 0402
    u16 input_size;        // 0403
    u32 aram_work_addr;    // high: 0404, low: 0405
    u32 mram_output_addr;  // high: 0406, low: 0407
  };

  // The addresses listed here are read and written by the DSP ROM only
  struct CardUcodeWorkData
  {
    u32 work_0408;  // high: 0408, low: 0409
    u32 work_040a;  // high: 040a, low: 040b - serves as the final hash
    u32 work_040c;  // high: 040c, low: 040d
    u32 work_040e;  // high: 040e, low: 040f
    u16 work_0410;
    u16 work_0411;
  };

  enum class State
  {
    WaitingForRequest,
    WaitingForAddress,
    WaitingForNextTask,
  };

  State m_state = State::WaitingForRequest;
};
}  // namespace DSP::HLE
