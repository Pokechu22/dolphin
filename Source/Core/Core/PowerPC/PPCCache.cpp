// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/PPCCache.h"

#include <array>

#include "Common/ChunkFile.h"
#include "Common/Swap.h"
#include "Core/Config/MainSettings.h"
#include "Core/DolphinAnalytics.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/EXI/EXI_Channel.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_DeviceIPL.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"

namespace PowerPC
{
namespace
{
constexpr std::array<u32, 8> s_plru_mask{
    11, 11, 19, 19, 37, 37, 69, 69,
};
constexpr std::array<u32, 8> s_plru_value{
    11, 3, 17, 1, 36, 4, 64, 0,
};

constexpr std::array<u32, 255> s_way_from_valid = [] {
  std::array<u32, 255> data{};
  for (size_t m = 0; m < data.size(); m++)
  {
    u32 w = 0;
    while ((m & (size_t{1} << w)) != 0)
      w++;
    data[m] = w;
  }
  return data;
}();

constexpr std::array<u32, 128> s_way_from_plru = [] {
  std::array<u32, 128> data{};

  for (size_t m = 0; m < data.size(); m++)
  {
    std::array<u32, 7> b{};
    for (size_t i = 0; i < b.size(); i++)
      b[i] = u32(m & (size_t{1} << i));

    u32 w = 0;
    if (b[0] != 0)
    {
      if (b[2] != 0)
      {
        if (b[6] != 0)
          w = 7;
        else
          w = 6;
      }
      else if (b[5] != 0)
      {
        w = 5;
      }
      else
      {
        w = 4;
      }
    }
    else if (b[1] != 0)
    {
      if (b[4] != 0)
        w = 3;
      else
        w = 2;
    }
    else if (b[3] != 0)
    {
      w = 1;
    }
    else
    {
      w = 0;
    }

    data[m] = w;
  }

  return data;
}();

// During the GameCube boot process, code execution starts at 0xfff00100, which is mapped to an
// automated EXI transfer from the IPL (with decryption).  Note in particular that the decryption
// does not care about the address, so everything must be read in forward order exactly once; thus,
// BS1 jumps forward through code to load it into ICache before it then jumps backwards to run it.
//
// I've assumed that the mapped region is the size of the copyright message and BS1 (0x800 bytes),
// and that it actually starts mapping at offset 0 for 0xfff00000, and that this mapping always
// exists (though it would return encrypted data after decryption is disabled).  None of this is
// hardware tested.
//
// This cannot be done with regular MMIOs, as Memory::Read_U32 doesn't use them.
void ReadCacheBlock(u32 address, std::array<u32, ICACHE_BLOCK_SIZE>& block)
{
  address = (address & ~0x1f);  // Keep aligned with the block
  if ((address & 0xfffff800) == 0xfff00000)
  {
    u32 offset = address & 0x7ff;
    ExpansionInterface::IEXIDevice* ipl = ExpansionInterface::GetChannel(0)->GetDevice(1 << 1);
    DEBUG_ASSERT(ipl != nullptr);
    DEBUG_ASSERT(ipl->m_device_type == ExpansionInterface::EXIDeviceType::MaskROM);
    // Note that there's some funkyness here that isn't emulated; per
    // http://hitmen.c02.at/files/yagcd/yagcd/chap2.html#sec2.8.3 the CPU actually reads 64 bits
    // at a time and 32 of those bits are sent back decrpyted over the EXI bus, since there's no
    // way to not write data. Since this is only observable via bus snooping, there isn't a reason
    // to emulate it, and we just read the whole cache block instead.
    ipl->SetCS(1);
    ipl->ImmWrite(offset << 6, 4);
    for (u32 i = 0; i < ICACHE_BLOCK_SIZE; i++)
    {
      block[i] = Common::swap32(ipl->ImmRead(4));
    }
    ipl->SetCS(0);
  }
  else
  {
    Memory::CopyFromEmu(reinterpret_cast<u8*>(block.data()), address, ICACHE_BLOCK_SIZE * 4);
  }
}

// This function is only called as a fallback when ICache is disabled.
// Since it might be called multiple times for the same address (in fact, it must be for the ICache
// stale data message), we can't depend on EXI bus decryption here (and can't access EXI at all,
// since this can happen in the middle of actual emulated EXI transfers), so hack into already
// decrypted data.
u32 ReadInstruction0(u32 address)
{
  if ((address & 0xfffff800) == 0xfff00000)
  {
    u32 offset = address & 0x7ff;
    ExpansionInterface::IEXIDevice* ipl = ExpansionInterface::GetChannel(0)->GetDevice(1 << 1);
    DEBUG_ASSERT(ipl != nullptr);
    DEBUG_ASSERT(ipl->m_device_type == ExpansionInterface::EXIDeviceType::MaskROM);
    return static_cast<ExpansionInterface::CEXIIPL*>(ipl)->ReadDecryptedIPL(offset);
  }
  else
  {
    return Memory::Read_U32(address);
  }
}
}  // Anonymous namespace

InstructionCache::~InstructionCache()
{
  if (m_config_callback_id)
    Config::RemoveConfigChangedCallback(*m_config_callback_id);
}

void InstructionCache::Reset()
{
  valid.fill(0);
  plru.fill(0);
  lookup_table.fill(0xFF);
  lookup_table_ex.fill(0xFF);
  lookup_table_vmem.fill(0xFF);
  JitInterface::ClearSafe();
}

void InstructionCache::Init()
{
  if (!m_config_callback_id)
    m_config_callback_id = Config::AddConfigChangedCallback([this] { RefreshConfig(); });
  RefreshConfig();

  data.fill({});
  tags.fill({});
  Reset();
}

void InstructionCache::Invalidate(u32 addr)
{
  if (!HID0.ICE || m_disable_icache)
    return;

  // Invalidates the whole set
  const u32 set = (addr >> 5) & 0x7f;
  for (size_t i = 0; i < 8; i++)
  {
    if (valid[set] & (1U << i))
    {
      if (tags[set][i] & (ICACHE_VMEM_BIT >> 12))
        lookup_table_vmem[((tags[set][i] << 7) | set) & 0xfffff] = 0xff;
      else if (tags[set][i] & (ICACHE_EXRAM_BIT >> 12))
        lookup_table_ex[((tags[set][i] << 7) | set) & 0x1fffff] = 0xff;
      else
        lookup_table[((tags[set][i] << 7) | set) & 0xfffff] = 0xff;
    }
  }
  valid[set] = 0;
  JitInterface::InvalidateICacheLine(addr);
}

u32 InstructionCache::ReadInstruction(u32 addr)
{
  if (!HID0.ICE || m_disable_icache)  // instruction cache is disabled
    return ReadInstruction0(addr);

  u32 set = (addr >> 5) & 0x7f;
  u32 tag = addr >> 12;

  u32 t;
  if (addr & ICACHE_VMEM_BIT)
  {
    t = lookup_table_vmem[(addr >> 5) & 0xfffff];
  }
  else if (addr & ICACHE_EXRAM_BIT)
  {
    t = lookup_table_ex[(addr >> 5) & 0x1fffff];
  }
  else
  {
    t = lookup_table[(addr >> 5) & 0xfffff];
  }

  if (t == 0xff)  // load to the cache
  {
    if (HID0.ILOCK)  // instruction cache is locked
      return ReadInstruction0(addr);
    // select a way
    if (valid[set] != 0xff)
      t = s_way_from_valid[valid[set]];
    else
      t = s_way_from_plru[plru[set]];
    // load
    ReadCacheBlock(addr, data[set][t]);
    if (valid[set] & (1 << t))
    {
      if (tags[set][t] & (ICACHE_VMEM_BIT >> 12))
        lookup_table_vmem[((tags[set][t] << 7) | set) & 0xfffff] = 0xff;
      else if (tags[set][t] & (ICACHE_EXRAM_BIT >> 12))
        lookup_table_ex[((tags[set][t] << 7) | set) & 0x1fffff] = 0xff;
      else
        lookup_table[((tags[set][t] << 7) | set) & 0xfffff] = 0xff;
    }

    if (addr & ICACHE_VMEM_BIT)
      lookup_table_vmem[(addr >> 5) & 0xfffff] = t;
    else if (addr & ICACHE_EXRAM_BIT)
      lookup_table_ex[(addr >> 5) & 0x1fffff] = t;
    else
      lookup_table[(addr >> 5) & 0xfffff] = t;
    tags[set][t] = tag;
    valid[set] |= (1 << t);
  }
  // update plru
  plru[set] = (plru[set] & ~s_plru_mask[t]) | s_plru_value[t];
  const u32 res = Common::swap32(data[set][t][(addr >> 2) & 7]);
  const u32 inmem = ReadInstruction0(addr);
  if (res != inmem)
  {
    INFO_LOG_FMT(POWERPC,
                 "ICache read at {:08x} returned stale data: CACHED: {:08x} vs. RAM: {:08x}", addr,
                 res, inmem);
    DolphinAnalytics::Instance().ReportGameQuirk(GameQuirk::ICACHE_MATTERS);
  }
  return res;
}

void InstructionCache::DoState(PointerWrap& p)
{
  if (p.IsReadMode())
  {
    // Clear valid parts of the lookup tables (this is done instead of using fill(0xff) to avoid
    // loading the entire 4MB of tables into cache)
    for (u32 set = 0; set < ICACHE_SETS; set++)
    {
      for (u32 way = 0; way < ICACHE_WAYS; way++)
      {
        if ((valid[set] & (1 << way)) != 0)
        {
          const u32 addr = (tags[set][way] << 12) | (set << 5);
          if (addr & ICACHE_VMEM_BIT)
            lookup_table_vmem[(addr >> 5) & 0xfffff] = 0xff;
          else if (addr & ICACHE_EXRAM_BIT)
            lookup_table_ex[(addr >> 5) & 0x1fffff] = 0xff;
          else
            lookup_table[(addr >> 5) & 0xfffff] = 0xff;
        }
      }
    }
  }

  p.DoArray(data);
  p.DoArray(tags);
  p.DoArray(plru);
  p.DoArray(valid);

  if (p.IsReadMode())
  {
    // Recompute lookup tables
    for (u32 set = 0; set < ICACHE_SETS; set++)
    {
      for (u32 way = 0; way < ICACHE_WAYS; way++)
      {
        if ((valid[set] & (1 << way)) != 0)
        {
          const u32 addr = (tags[set][way] << 12) | (set << 5);
          if (addr & ICACHE_VMEM_BIT)
            lookup_table_vmem[(addr >> 5) & 0xfffff] = way;
          else if (addr & ICACHE_EXRAM_BIT)
            lookup_table_ex[(addr >> 5) & 0x1fffff] = way;
          else
            lookup_table[(addr >> 5) & 0xfffff] = way;
        }
      }
    }
  }
}

void InstructionCache::RefreshConfig()
{
  m_disable_icache = Config::Get(Config::MAIN_DISABLE_ICACHE);
}
}  // namespace PowerPC
