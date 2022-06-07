// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/DSPHLE/UCodes/CARD.h"

#include <vector>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Core/DSP/DSPAccelerator.h"
#include "Core/HW/DSP.h"
#include "Core/HW/DSPHLE/DSPHLE.h"
#include "Core/HW/DSPHLE/UCodes/UCodes.h"

namespace DSP::HLE
{
constexpr u32 CRC_GAMECUBE = 0x65d6cc6f;
constexpr u32 CRC_WII = 0x65da0c63;

CARDUCode::CARDUCode(DSPHLE* dsphle, u32 crc) : UCodeInterface(dsphle, crc)
{
  std::string_view type = "unknown";
  switch (m_crc)
  {
  case CRC_GAMECUBE:
    type = "GameCube";
    break;
  case CRC_WII:
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
//
// This HLE implementation matches Dolphin's DSP LLE for all inputs listed above apart from the
// zero-length ones.  Testing has not been done on real hardware yet.
//
// The above testing was done with LLE recompiler.  LLE interpreter seems to behave different for
// length 0...
//
// On LLE interpreter (and now this HLE version), 0 bytes are read into ARAM when length is 0,
// but it processes 0x40000 nybbles (0x10000 words).  This means the initial ARAM contents matters.
// If they are zero, the hash is 0ecc54f7 (both here and LLE int).  For Super Mario Sunshine, the
// default is 029f0010 029f0033 029f0034 029f0035 029f0036 029f0037 029f0038 029f0039 followed by
// all zeros, which gives a hash of 691cbad0.
//
// I haven't determined the cause of the bug for LLE Rec yet.
//
// Update: The LLE Rec bug has been fixed (https://github.com/dolphin-emu/dolphin/pull/10723).
// Now both give 691cbad0 for the default memory contents.

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

namespace
{
class HLEAccelerator final : public Accelerator
{
protected:
  void OnEndException() override
  {
    PanicAlertFmt("CARD uCode shouldn't have the accelerator end!");
  }

  u8 ReadMemory(u32 address) override { return ReadARAM(address); }
  void WriteMemory(u32 address, u8 value) override { WriteARAM(value, address); }
};

// TODO: Doing this is jank (and AXVoice.h does a similar thing).
static std::unique_ptr<Accelerator> s_accelerator = std::make_unique<HLEAccelerator>();
}  // namespace

static void DoCardHashStep(CARDUCode::CardUcodeWorkData* data, u16 prev1, u16 prev2, u16 new1,
                           u16 new2)
{
  // ROM function from 86e5 to 8725

  // 86e8 - 86eb
  u16 tmp1 = (new2 << 4) | prev2;
  // This happens due to sign extension from the arithmetic right shift at 86eb
  if ((tmp1 & 0x80) != 0)
    tmp1 |= 0xff00;
  // 86ec - 86f0
  tmp1 ^= (prev1 << 8);
  tmp1 ^= (new1 << 12);

  // Assuming the accelerator reads nybbles, this just comes out to:
  // u16 tmp = (new1 << 12) | (prev1 << 8) | (new2 << 4) | prev2;
  // if (new2 & 0x80) tmp ^= 0xff00;

  // 86f1 - 86f7
  data->work_0408 += tmp1;  // unsigned addition

  // 86e9 - 86ea and 86f6 - 86fb
  const u32 tmp2 = (data->work_040c ^ data->work_040e) + data->work_0408;

  // 86fb - 86fd
  data->work_0411++;

  // 86fe - 870b
  u32 rotate = (data->work_0410 + data->work_0411) & 0x1f;
  u32 tmp3 = (tmp2 >> rotate);
  if (rotate != 0)
    tmp3 += (tmp2 << (0x20 - rotate));

  // 86fe and 870b - 870e
  data->work_040a += tmp3;

  // 870f - 871c
  data->work_040c = (~data->work_0408 & data->work_040a) | (data->work_0408 & data->work_040e);

  // 871d - 8724
  data->work_040e = data->work_0408 ^ data->work_040a ^ data->work_040c;
}

static void DoCardHash(CARDUCode::CardUcodeParameters params)
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

  // 865a - 8669 - Set up the accelerator
  s_accelerator->SetSampleFormat(0);
  s_accelerator->SetStartAddress(0);
  // Since there are 0x0100'0000 bytes of ARAM, and it gets mirrored every 0x0400'0000 bytes
  // (mask 0x03ff'ffff) according to DSP.cpp, this indicates that format 0 writes u16, probably.
  s_accelerator->SetEndAddress(0x01ff'ffff);
  s_accelerator->SetCurrentAddress(params.aram_work_addr);

  // 866a - 8684 - Copy from dmem to the accelerator and also sum the bytes

  u32 sum = 0;
  for (u32 i = 0; i < (params.input_size / sizeof(u16)); i++)
  {
    // DRAM and most things the DSP interacts with use 16-bit words
    const u8 first = buffer[2 * i];
    const u8 second = buffer[(2 * i) + 1];
    const u16 value = (u16(first) << 8) | u16(second);
    s_accelerator->WriteD3(value);
    sum += first;
    sum += second;
  }
  if (params.input_size & 1)
  {
    // Handle the last byte
    // Note that this won't go out of bounds on the buffer, as the buffer is read in groups of 4
    // bytes (possibly a restriction on DMA sizes?).
    // The second value is written to the accelerator just in case it makes a difference with
    // the behavior of the mode, but this may be unnecessary. (We're writing 2-byte words to ARAM.)
    const u8 first = buffer[params.input_size - 1];
    const u8 second = buffer[params.input_size];

    const u16 value = (u16(first) << 8) | u16(second);
    s_accelerator->WriteD3(value);
    sum += buffer[params.input_size - 1];
  }

  // 8685 - 86a3 - Initialize a bunch of state
  data.work_0408 = sum + 0x170a7489;
  data.work_040a = 0x05efe0aa;
  data.work_040c = 0xdaf4b157;
  data.work_040e = 0x6bbec3b6;
  data.work_0410 = (sum + 8) & 0xffff;
  data.work_0411 = 0;

  // 86a4 - 86b1 - Set up the accelerator again
  s_accelerator->SetSampleFormat(0);
  s_accelerator->SetStartAddress(0);
  // Since there are 0x0100'0000 bytes of ARAM, and it gets mirrored every 0x0400'0000 bytes
  // (mask 0x03ff'ffff) according to DSP.cpp, this indicates that format 0 reads nybbles, probably.
  s_accelerator->SetEndAddress(0x07ff'ffff);
  s_accelerator->SetCurrentAddress(params.aram_work_addr);

  // 86b2 - 86d2 - Actually do the hashing
  u16 prev1 = s_accelerator->ReadD3();
  u16 prev2 = s_accelerator->ReadD3();

  u16 new_counter;
  if (params.input_size != 0)
  {
    new_counter = (params.input_size - 1) / 2;
  }
  else
  {
    // This happens due to underflow, which also affects the high byte, so even with a logical
    // right shift sign extension is observed.
    // Also, this situation almost certainly never occurs in practice.
    new_counter = 0xffff;
  }

  for (u32 i = 0; i < new_counter; i++)
  {
    // Note: in the actual ROM, the accelerator is read in DoCardHashStep, but the copy from new to
    // prev happens outside.
    u16 new1 = s_accelerator->ReadD3();
    u16 new2 = s_accelerator->ReadD3();
    DoCardHashStep(&data, prev1, prev2, new1, new2);
    prev1 = new1;
    prev2 = new2;
    new1 = s_accelerator->ReadD3();
    new2 = s_accelerator->ReadD3();
    DoCardHashStep(&data, prev1, prev2, new1, new2);
    prev1 = new1;
    prev2 = new2;
  }
  if ((params.input_size & 1) == 0)
  {
    // Handle the last byte -- note that this happens for *even* counts, unlike before.
    // I'm not sure if this is an implementation detail that actually matters.
    u16 new1 = s_accelerator->ReadD3();
    u16 new2 = s_accelerator->ReadD3();
    DoCardHashStep(&data, prev1, prev2, new1, new2);
  }

  // 86d6 - 86e4 - DMA back the hash
  HLEMemory_Write_U32(params.mram_output_addr, data.work_040a);
}

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

    // Note that the difference in masking also happens in PrepareBootUCode, but we don't directly
    // handle that (HLEMemory_Get_Pointer does behave differently in Wii vs GameCube mode,
    // but based on the console's mode and not the uCode itself)
    // There are only 3 bytes that differ between the GC and Wii card uCode, and they are all
    // for masking (here, on iram_mram_addr, and on dram_mram_addr)
    const u32 mask = (m_crc == CRC_WII) ? 0x3fff'ffff : 0x0fff'ffff;
    const u32 address = mail & mask;

    INFO_LOG_FMT(DSPHLE, "CARDUCode - Reading input parameters from address {:08x} ({:08x})",
                 address, mail);
    CardUcodeParameters params = ReadParameters(address);
    INFO_LOG_FMT(DSPHLE, "Input MRAM address: {:08x}", params.mram_input_addr);
    INFO_LOG_FMT(DSPHLE, "Unused: {:04x}", params.unused);
    INFO_LOG_FMT(DSPHLE, "Input size: {:04x}", params.input_size);
    INFO_LOG_FMT(DSPHLE, "ARAM work address: {:08x}", params.aram_work_addr);
    INFO_LOG_FMT(DSPHLE, "Output MRAM address: {:08x}", params.mram_output_addr);

    // 003d - Call into ROM code
    DoCardHash(params);

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
