// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/DSPHLE/UCodes/CARD.h"

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/HW/DSP.h"
#include "Core/HW/DSPHLE/DSPHLE.h"
#include "Core/HW/DSPHLE/UCodes/UCodes.h"

namespace DSP::HLE
{
CARDUCode::CARDUCode(DSPHLE* dsphle, u32 crc) : UCodeInterface(dsphle, crc)
{
  std::string_view type = "unknown";
  switch (m_crc)
  {
  case 0x65d6cc6f:
    type = "GameCube";
    break;
  case 0x65da0c63:
    type = "Wii";
    break;
  }
  INFO_LOG_FMT(DSPHLE, "CARDUCode - initialized (type: {})", type);
}

CARDUCode::~CARDUCode()
{
  m_mail_handler.Clear();
}

void CARDUCode::Initialize()
{
  // 0010 - 0025, with the mail being 001f - 0025 and the stuff before being register initialization
  m_mail_handler.PushMail(DSP_INIT);
  m_state = State::WaitingForRequest;
}

// In Super Mario Sunshine, the relevant functions are card::__CARDUnlock (8035593c) and
// card::DoneCallback (80356504).  The input parameters are at 80747300 (I think this is dynamically
// allocated, but it seems to be consistent); the input address is 80747320, the unused value is 0,
// the input size is 8, the ARAM address is 00000000, and the output address is 80747340.
// The input data is populated by __CARDUnlock (at 803563e0) and the output is read by DoneCallback
// (at 80356564).  Setting a breakpoint at 803563f0 allows us to change the input data, which
// otherwise seems to be completely random.  Here are a few inputs and outputs from DSP LLE:
//
// 0000000000000000 -> 6348b619
// 0000000000000001 -> 9f31c537
// 0000000100000000 -> 8fb395d5
// ffffffffffffffff -> 56bd4b37
// 0123456789abcdef -> c7d0a17d
// fedcba9876543210 -> a115cacc
// 7c77a5c935f29b44 -> 6ac69ca5
// 017ca2808a158490 -> df3fbcda

void CARDUCode::Update()
{
  // check if we have to sent something
  if (!m_mail_handler.IsEmpty())
  {
    DSP::GenerateDSPInterruptFromDSPEmu(DSP::INT_DSP);
  }
}

void CARDUCode::HandleMail(u32 mail)
{
  if (m_upload_setup_in_progress)
  {
    // Function at 005a - 0085
    // The CARD ucode ignores the first 3 mails (mram_dest_addr, mram_size, mram_dram_addr)
    // but we currently don't handle that (they're read when they shoudln't be, but DSP HLE doesn't
    // implement them so it's fine).
    PrepareBootUCode(mail);
    return;
  }

  switch (m_state)
  {
  case State::WaitingForRequest:
  {
    // Loop from 0027 - 002c
    if (mail == 0xFF00'0000)
    {
      INFO_LOG_FMT(DSPHLE, "CARDUCode - Recieved unlock command");
      m_state = State::WaitingForAddress;
    }
    else
    {
      WARN_LOG_FMT(DSPHLE, "CARDUCode - Expected unlock command but got {:08x}", mail);
    }
    break;
  }
  case State::WaitingForAddress:
  {
    // Waiting, reading the address, and masking happens at 002e - 0032
    const u32 address = mail & 0x0fff'ffff;

    // DMA happens in function called from 0034 - 003b; function is at 0094 - 00a1
    CardUcodeParameters params;
    params.mram_input_addr = HLEMemory_Read_U32(address);
    params.unused = HLEMemory_Read_U16(address + 4);
    params.input_size = HLEMemory_Read_U16(address + 6);
    params.aram_work_addr = HLEMemory_Read_U32(address + 8);
    params.mram_output_addr = HLEMemory_Read_U32(address + 12);

    INFO_LOG_FMT(DSPHLE, "CARDUCode - Reading input parameters from address {:08x} ({:08x})",
                 address, mail);
    INFO_LOG_FMT(DSPHLE, "Input MRAM address: {:08x}", params.mram_input_addr);
    INFO_LOG_FMT(DSPHLE, "Unused: {:04x}", params.unused);
    INFO_LOG_FMT(DSPHLE, "Input size: {:04x}", params.input_size);
    INFO_LOG_FMT(DSPHLE, "ARAM work address: {:08x}", params.aram_work_addr);
    INFO_LOG_FMT(DSPHLE, "Output MRAM address: {:08x}", params.mram_output_addr);

    // Call at 003d into ROM code
    // TODO

    // 003f - 0045: send a response.
    m_mail_handler.PushMail(DSP_DONE);
    m_state = State::WaitingForNextTask;
    break;
  }
  case State::WaitingForNextTask:
  {
    // Loop from 0048 to 0057
    switch (mail)
    {
    case MAIL_NEW_UCODE:
      INFO_LOG_FMT(DSPHLE, "CARDUCode - Setting up new ucode");
      // Jumps to 005a
      m_upload_setup_in_progress = true;
      break;
    case MAIL_RESET:
      INFO_LOG_FMT(DSPHLE, "CARDUCode - Switching to ROM ucode");
      m_dsphle->SetUCode(UCODE_ROM);
      break;
    default:
      WARN_LOG_FMT(DSPHLE, "CARDUCode - Expected MAIL_NEW_UCODE or MAIL_RESET but got {:08x}",
                   mail);
      break;
    }
  }
  }
}
}  // namespace DSP::HLE
