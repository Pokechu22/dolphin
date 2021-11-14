// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ------------------------------------------
// Video backend must define these functions
// ------------------------------------------

#pragma once

#include <vector>

#include "Common/MathUtil.h"

struct BPCmd;

namespace BPFunctions
{
struct ScissorRect
{
  struct ScissorRange
  {
    constexpr ScissorRange(u32 offset_, u32 start_, u32 end_)
        : offset(offset_), start(start_), end(end_)
    {
    }
    const int offset;
    const int start;
    const int end;
  };

  constexpr ScissorRect(ScissorRange x_range, ScissorRange y_range)
      :  // Rectangle ctor takes x0, y0, x1, y1.
        rect(x_range.start, y_range.start, x_range.end, y_range.end), x_off(x_range.offset),
        y_off(y_range.offset)
  {
  }

  const MathUtil::Rectangle<int> rect;
  const int x_off;
  const int y_off;

  constexpr bool operator<(const ScissorRect& other) const
  {
    return rect.GetWidth() * rect.GetHeight() < other.rect.GetWidth() * other.rect.GetHeight();
  }
};

std::vector<ScissorRect> ComputeScissorRects();
ScissorRect ComputeScissorRect();

void FlushPipeline();
void SetGenerationMode();
void SetScissorAndViewport();
void SetDepthMode();
void SetBlendMode();
void ClearScreen(const MathUtil::Rectangle<int>& rc);
void OnPixelFormatChange();
void SetInterlacingMode(const BPCmd& bp);
}  // namespace BPFunctions
