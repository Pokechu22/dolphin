// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// ------------------------------------------
// Video backend must define these functions
// ------------------------------------------

#pragma once

#include <set>
#include <utility>

#include "Common/MathUtil.h"
#include "VideoCommon/BPMemory.h"
struct XFMemory;

namespace BPFunctions
{
struct ScissorRange
{
  constexpr ScissorRange(u32 offset, u32 start, u32 end) : offset(offset), start(start), end(end) {}
  const int offset;
  const int start;
  const int end;
};

struct ScissorRect
{
  constexpr ScissorRect(ScissorRange x_range, ScissorRange y_range)
      :  // Rectangle ctor takes x0, y0, x1, y1.
        rect(x_range.start, y_range.start, x_range.end, y_range.end), x_off(x_range.offset),
        y_off(y_range.offset)
  {
  }

  MathUtil::Rectangle<int> rect;
  int x_off;
  int y_off;

  int GetArea() const;
};

struct ScissorResult
{
  ScissorResult(const BPMemory& bpmem, const XFMemory& xfmem);
  ~ScissorResult() = default;
  ScissorResult(const ScissorResult& other)
      : scissor_tl{.hex = other.scissor_tl.hex}, scissor_br{.hex = other.scissor_br.hex},
        scissor_off{.hex = other.scissor_off.hex}, viewport_left{other.viewport_left},
        viewport_right{other.viewport_right}, viewport_top{other.viewport_top},
        viewport_bottom{other.viewport_bottom}, m_result{other.m_result}
  {
  }
  ScissorResult& operator=(const ScissorResult& other)
  {
    scissor_tl.hex = other.scissor_tl.hex;
    scissor_br.hex = other.scissor_br.hex;
    scissor_off.hex = other.scissor_off.hex;
    viewport_left = other.viewport_left;
    viewport_right = other.viewport_right;
    viewport_top = other.viewport_top;
    viewport_bottom = other.viewport_bottom;
    m_result = other.m_result;
    return *this;
  }
  ScissorResult(ScissorResult&& other)
      : scissor_tl{.hex = other.scissor_tl.hex}, scissor_br{.hex = other.scissor_br.hex},
        scissor_off{.hex = other.scissor_off.hex}, viewport_left{other.viewport_left},
        viewport_right{other.viewport_right}, viewport_top{other.viewport_top},
        viewport_bottom{other.viewport_bottom}, m_result{std::move(other.m_result)}
  {
  }
  ScissorResult& operator=(ScissorResult&& other)
  {
    scissor_tl.hex = other.scissor_tl.hex;
    scissor_br.hex = other.scissor_br.hex;
    scissor_off.hex = other.scissor_off.hex;
    viewport_left = other.viewport_left;
    viewport_right = other.viewport_right;
    viewport_top = other.viewport_top;
    viewport_bottom = other.viewport_bottom;
    m_result = std::move(other.m_result);
    return *this;
  }

  // Input values, for use in statistics
  ScissorPos scissor_tl;
  ScissorPos scissor_br;
  ScissorOffset scissor_off;
  float viewport_left;
  float viewport_right;
  float viewport_top;
  float viewport_bottom;

  // Actual result
  std::vector<ScissorRect> m_result;

  ScissorRect Best() const;

  constexpr bool ScissorMatches(const ScissorResult& other) const
  {
    return scissor_tl.hex == other.scissor_tl.hex && scissor_br.hex == other.scissor_br.hex &&
           scissor_off.hex == other.scissor_off.hex;
  }
  constexpr bool ViewportMatches(const ScissorResult& other) const
  {
    return viewport_left == other.viewport_left && viewport_right == other.viewport_right &&
           viewport_top == other.viewport_top && viewport_bottom == other.viewport_bottom;
  }
  constexpr bool Matches(const ScissorResult& other, bool compare_scissor,
                         bool compare_viewport) const
  {
    if (compare_scissor && !ScissorMatches(other))
      return false;
    if (compare_viewport && !ViewportMatches(other))
      return false;
    return true;
  }

private:
  ScissorResult(const BPMemory& bpmem, std::pair<float, float> viewport_x,
                std::pair<float, float> viewport_y);

  int GetViewportArea(const ScissorRect& rect) const;
  bool IsWorse(const ScissorRect& lhs, const ScissorRect& rhs) const;
};

ScissorResult ComputeScissorRects();

void FlushPipeline();
void SetGenerationMode();
void SetScissorAndViewport();
void SetDepthMode();
void SetBlendMode();
void ClearScreen(const MathUtil::Rectangle<int>& rc);
void OnPixelFormatChange();
void SetInterlacingMode(const BPCmd& bp);
}  // namespace BPFunctions
