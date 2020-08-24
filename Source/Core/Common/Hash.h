// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>

#include "Common/CommonTypes.h"

namespace Common
{
u32 HashFletcher(const u8* data_u8, size_t length);  // FAST. Length & 1 == 0.
u32 HashAdler32(const u8* data, size_t len);         // Fairly accurate, slightly slower
u32 HashEctor(const u8* ptr, size_t length);         // JUNK. DO NOT USE FOR NEW THINGS
u8 HashCrc7(const u8* ptr, size_t length);           // For SD
u16 HashCrc16(const u8* ptr, size_t length);         // For SD
u64 GetHash64(const u8* src, u32 len, u32 samples);
void SetHash64Function();

template <size_t N>
u8 HashCrc7(const std::array<u8, N>& data)
{
  return HashCrc7(data.data(), N);
}
template <size_t N>
u16 HashCrc16(const std::array<u8, N>& data)
{
  return HashCrc16(data.data(), N);
}
}  // namespace Common
