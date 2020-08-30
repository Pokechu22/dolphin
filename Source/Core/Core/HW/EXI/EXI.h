// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <initializer_list>

#include "Common/CommonTypes.h"
#include "Core/CoreTiming.h"

class PointerWrap;

namespace CoreTiming
{
enum class FromThread;
}
namespace MMIO
{
class Mapping;
}

namespace ExpansionInterface
{
class CEXIChannel;
class IEXIDevice;
enum class EXIDeviceType : int;

enum
{
  MAX_MEMORYCARD_SLOTS = 2,
  MAX_EXI_CHANNELS = 3
};

enum class Slot : int
{
  A,
  B,
  SP1,
};
static constexpr auto SLOTS = {Slot::A, Slot::B, Slot::SP1};

u8 SlotToEXIChannel(Slot slot);
u8 SlotToEXIDevice(Slot slot);

void Init();
void Shutdown();
void DoState(PointerWrap& p);
void PauseAndLock(bool doLock, bool unpauseOnUnlock);

void RegisterMMIO(MMIO::Mapping* mmio, u32 base);

void UpdateInterrupts();
void ScheduleUpdateInterrupts(CoreTiming::FromThread from, int cycles_late);

void ChangeDevice(const u8 channel, const EXIDeviceType device_type, const u8 device_num,
                  CoreTiming::FromThread from_thread = CoreTiming::FromThread::NON_CPU);

CEXIChannel* GetChannel(u32 index);

IEXIDevice* FindDevice(EXIDeviceType device_type, int customIndex = -1);

}  // end of namespace ExpansionInterface
