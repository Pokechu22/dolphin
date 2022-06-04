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
  INFO_LOG_FMT(DSPHLE, "CARDUCode - initialized");
}

CARDUCode::~CARDUCode()
{
  m_mail_handler.Clear();
}

void CARDUCode::Initialize()
{
  m_mail_handler.PushMail(DSP_INIT);
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
  static bool next_is_addr = false;
  if (next_is_addr)
  {
    next_is_addr = false;
    INFO_LOG_FMT(DSPHLE, "CARDUCode - addr: {:x} => {:x}", mail, mail & 0x0fff'ffff);
    m_mail_handler.PushMail(DSP_DONE);
  }
  else if (m_upload_setup_in_progress)
  {
    PrepareBootUCode(mail);
  }
  else if (mail == 0xFF00'0000)  // unlock card
  {
    INFO_LOG_FMT(DSPHLE, "CARDUCode - Unlock");
    next_is_addr = true;
  }
  else if (mail == 0xCDD10001)
  {
    INFO_LOG_FMT(DSPHLE, "CARDUCode - Setting up new ucode");
    m_upload_setup_in_progress = true;
  }
  else if (mail == 0xCDD10002)
  {
    INFO_LOG_FMT(DSPHLE, "CARDUCode - Switching to ROM ucode");
    m_dsphle->SetUCode(UCODE_ROM);
  }
  else
  {
    WARN_LOG_FMT(DSPHLE, "CARDUCode - unknown command: {:x}", mail);
  }
}
}  // namespace DSP::HLE
