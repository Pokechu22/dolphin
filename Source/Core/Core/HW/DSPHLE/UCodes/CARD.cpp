// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/DSPHLE/UCodes/CARD.h"

#include <vector>

#include "Common/Assert.h"
#include "Common/ChunkFile.h"
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
// 0000000000000000 -> 24349566
// 0000000000000001 -> aee1a9cc
// 0000000100000000 -> c7697175
// ffffffffffffffff -> c09ac28b
// 0123456789abcdef -> 9b5fe1fb
// fedcba9876543210 -> 6ba14ac4
// 7c77a5c935f29b44 -> 2b3f37c9
// 017ca2808a158490 -> 770bd350
//
// card::InitCallback (80356494) is responsible for sending the data.  A breakpoint there also works
// and allows changing the input parameters.
//
// 0000000000000000 with length 0 -> 9c843834
// 0000000000000001 with length 0 -> 9c843834
// ffffffff with length 0 -> 9c843834
// 00000000 with length 1 -> 05efe0aa
// 00123456 with length 1 -> 05efe0aa
// 00ffffff with length 1 -> 05efe0aa
// 01000000 with length 1 -> 05efe0aa (huh)
// ff000000 with length 1 -> 05efe0aa ... ok.
// 00000000 with length 2 -> bb540b1d
// 0000ffff with length 2 -> bb540b1d
// 00010000 with length 2 -> e4e1f5e3
// 0001ffff with length 2 -> e4e1f5e3
// 0100ffff with length 2 -> e121f5e3 - this is different.
// 00000000 with length 3 -> c2c4e55a
// 000000ff with length 3 -> c2c4e55a
// 0000ff00 with length 3 -> 390013f4
//
// I think a length of 0 or 1 just isn't handled correctly, but any length >= 2, odd or even, works
// (of course, I've only tested 2, 3, or 8).

void CARDUCode::CardUcodeWorkData::WriteAccelerator(u16 value)
{
  WriteARAM(accelerator * 2, value >> 8);
  WriteARAM(accelerator * 2 + 1, value & 0xFF);
  accelerator++;
}

u16 CARDUCode::CardUcodeWorkData::ReadAccelerator()
{
  u8 val = ReadARAM(accelerator / 2);
  if (accelerator & 1)
    val &= 0xf;
  else
    val >>= 4;
  accelerator++;
  return val;
}

static CARDUCode::CardUcodeParameters ReadParameters(u32 address)
{
  // DMA happens in function called from 0034 - 003b; DMA function is at 0094 - 00a1
  CARDUCode::CardUcodeParameters params;
  params.mram_input_addr = HLEMemory_Read_U32(address);
  params.unused = HLEMemory_Read_U16(address + 4);
  params.input_size = HLEMemory_Read_U16(address + 6);
  params.aram_work_addr = HLEMemory_Read_U32(address + 8);
  params.mram_output_addr = HLEMemory_Read_U32(address + 12);

  return params;
}

static void ProcessParameters(CARDUCode::CardUcodeParameters params)
{
  // Large ROM function from 8644 to 86e4
  CARDUCode::CardUcodeWorkData data{};

  // 8649 - 864d - round up size to the next multiple of 4 bytes
  const u16 dma_size = (params.input_size + 3) & ~3;
  // 864e - 8658 - DMA the input data to 0800 in DRAM
  // (We just use our own buffer instead of dealing with DRAM)
  std::vector<u8> buffer;
  buffer.reserve(dma_size);
  const u8* const input_data = static_cast<u8*>(HLEMemory_Get_Pointer(params.mram_input_addr));
  std::copy(input_data, input_data + dma_size, std::back_inserter(buffer));

  ASSERT(params.input_size >= 2);

  // 865a - 8669 - Set up the accelerator
  // Format is 0 (unknown)
  // Start is ARAM 0x0000'0000, and end is ARAM 0x01ff'ffff - we don't need to worry about this
  // as it means there is no wrapping.  The actual address to use comes from params.
  data.accelerator = params.aram_work_addr;

  // Copy from dmem to the accelerator.

  // Set up the accelerator again
  // Format is 0 (unknown)
  // Start is ARAM 0x0000'0000, and end is ARAM 0x07ff'ffff - we don't need to worry about this
  // as it means there is no wrapping.  The actual address to use comes from params again.
  // Note that the end is 0x07ff'ffff this time - 4 times larger.
  // This is still odd as there's only 0x0100'0000 bytes of ARAM, and it gets mirrored every
  // 0x0400'0000 bytes (mask 0x03ff'ffff) - the first mask is half that (so u16), and this mask
  // is twice that (so nybbles?)
}

void CARDUCode::Update()
{
  // check if we have something to send
  if (m_mail_handler.HasPending())
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

    INFO_LOG_FMT(DSPHLE, "CARDUCode - Reading input parameters from address {:08x} ({:08x})",
                 address, mail);
    CardUcodeParameters params = ReadParameters(address);
    INFO_LOG_FMT(DSPHLE, "Input MRAM address: {:08x}", params.mram_input_addr);
    INFO_LOG_FMT(DSPHLE, "Unused: {:04x}", params.unused);
    INFO_LOG_FMT(DSPHLE, "Input size: {:04x}", params.input_size);
    INFO_LOG_FMT(DSPHLE, "ARAM work address: {:08x}", params.aram_work_addr);
    INFO_LOG_FMT(DSPHLE, "Output MRAM address: {:08x}", params.mram_output_addr);

    // Call at 003d into ROM code
    ProcessParameters(params);

    // 003f - 0045: send a response.
    m_mail_handler.PushMail(DSP_DONE);
    m_state = State::WaitingForNextTask;
    break;
  }
  case State::WaitingForNextTask:
  {
    // Loop from 0048 to 0057
    // The CARD uCode checks that the high word is cdd1, so we compare the full mail with
    // MAIL_NEW_UCODE/MAIL_RESET without doing masking
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

void CARDUCode::DoState(PointerWrap& p)
{
  DoStateShared(p);
  p.Do(m_state);
}
}  // namespace DSP::HLE
