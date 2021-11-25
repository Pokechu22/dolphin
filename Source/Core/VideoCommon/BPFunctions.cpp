// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/BPFunctions.h"

#include <algorithm>
#include <string_view>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

namespace BPFunctions
{
// ----------------------------------------------
// State translation lookup tables
// Reference: Yet Another GameCube Documentation
// ----------------------------------------------

void FlushPipeline()
{
  g_vertex_manager->Flush();
}

void SetGenerationMode()
{
  g_vertex_manager->SetRasterizationStateChanged();
}

int ScissorRect::GetViewportArea() const
{
  int viewport_x0 = xfmem.viewport.xOrig - xfmem.viewport.wd;
  int viewport_x1 = xfmem.viewport.xOrig + xfmem.viewport.wd;
  int viewport_y0 = xfmem.viewport.xOrig - xfmem.viewport.wd;
  int viewport_y1 = xfmem.viewport.xOrig + xfmem.viewport.wd;

  int x0 = std::clamp(rect.left + x_off, viewport_x0, viewport_x1);
  int x1 = std::clamp(rect.right + x_off, viewport_x0, viewport_x1);

  int y0 = std::clamp(rect.top + y_off, viewport_y0, viewport_y1);
  int y1 = std::clamp(rect.bottom + y_off, viewport_y0, viewport_y1);

  return (x1 - x0) * (y1 - y0);
}

int ScissorRect::GetArea() const
{
  return rect.GetWidth() * rect.GetHeight();
}

bool ScissorRect::operator<(const ScissorRect& other) const
{
  // First, penalize any rect that is not in the viewport
  int our_area = GetViewportArea();
  int their_area = other.GetViewportArea();

  if (our_area != their_area)
    return our_area < their_area;

  // Now compare on areas
  return GetArea() < other.GetArea();
}

static std::vector<ScissorRect::ScissorRange> ComputeScissorRanges(int start, int end, int offset,
                                                                   int efb_dim)
{
  std::vector<ScissorRect::ScissorRange> ranges;

  for (int extra_off = -4096; extra_off <= 4096; extra_off += 1024)
  {
    int new_off = offset + extra_off;
    int new_start = std::clamp(start - new_off, 0, efb_dim);
    int new_end = std::clamp(end - new_off + 1, 0, efb_dim);
    if (new_start < new_end)
    {
      ranges.emplace_back(new_off, new_start, new_end);
    }
  }

  return ranges;
}

std::vector<ScissorRect> ComputeScissorRects()
{
  // Range is [left, right] and [top, bottom] (closed intervals)
  const int left = bpmem.scissorTL.x & 2047;
  const int right = bpmem.scissorBR.x & 2047;
  const int top = bpmem.scissorTL.y & 2047;
  const int bottom = bpmem.scissorBR.y & 2047;
  // When left > right or top > bottom, nothing renders (even with wrapping from the offsets)
  if (left > right || top > bottom)
    return {};
  // Note that both the offsets and the coordinates have 342 added to them internally
  // (for the offsets, this is before they are divided by 2/right shifted).
  // This code could undo both sets of offsets, but it doesn't need to since they
  // cancel out when subtracting.
  const int x_off = (bpmem.scissorOffset.x << 1) & 1023;
  const int y_off = (bpmem.scissorOffset.y << 1) & 1023;

  std::vector<ScissorRect::ScissorRange> x_ranges =
      ComputeScissorRanges(left, right, x_off, EFB_WIDTH);
  std::vector<ScissorRect::ScissorRange> y_ranges =
      ComputeScissorRanges(top, bottom, y_off, EFB_HEIGHT);

  // Now we need to form actual rectangles from the x and y ranges,
  // which is a simple Cartesian product of x_ranges_clamped and y_ranges_clamped.
  // Each rectangle is also a Cartesian product of x_range and y_range, with
  // the rectangles being half-open (of the form [x0, x1) X [y0, y1)).
  std::vector<ScissorRect> rectangles;
  rectangles.reserve(std::min(x_ranges.size() * y_ranges.size(), 1ULL));

  for (const auto& x_range : x_ranges)
  {
    DEBUG_ASSERT(x_range.start < x_range.end);
    DEBUG_ASSERT(x_range.end <= EFB_WIDTH);
    for (const auto& y_range : y_ranges)
    {
      DEBUG_ASSERT(y_range.start < y_range.end);
      DEBUG_ASSERT(y_range.end <= EFB_HEIGHT);
      rectangles.emplace_back(x_range, y_range);
    }
  }

  return rectangles;
}

ScissorRect ComputeScissorRect()
{
  std::vector<ScissorRect> rectangles = ComputeScissorRects();

  // For now, simply choose the largest rectangle.
  // But if we have no rectangles, add a bogus one that's out of bounds (this is temporary)
  // Yes, this could be done more efficiently by looking at x_range and y_range individually,
  // or even only picking one range earlier on, but again, this is temporary.
  if (rectangles.empty())
  {
    rectangles.emplace_back(ScissorRect::ScissorRange{0, 1000, 1001},
                            ScissorRect::ScissorRange{0, 1000, 1001});
  }

  return *std::max_element(rectangles.begin(), rectangles.end());
}

void SetScissorAndViewport()
{
  /* NOTE: the minimum value here for the scissor rect is -342.
   * GX SDK functions internally add an offset of 342 to scissor coords to
   * ensure that the register was always unsigned.
   *
   * The code that was here before tried to "undo" this offset, but
   * since we always take the difference, the +342 added to both
   * sides cancels out. */

  /* NOTE: With a positive scissor offset, the scissor rect is shifted left and/or up;
   * With a negative scissor offset, the scissor rect is shifted right and/or down.
   *
   * GX SDK functions internally add an offset of 342 to scissor offset.
   * The scissor offset is always even, so to save space, the scissor offset register
   * is scaled down by 2. So, if somebody calls GX_SetScissorBoxOffset(20, 20);
   * the registers will be set to ((20 + 342) / 2 = 181, 181).
   *
   * The scissor offset register is 10bit signed [-512, 511].
   * e.g. In Super Mario Galaxy 1 and 2, during the "Boss roar effect",
   * for a scissor offset of (0, -464), the scissor offset register will be set to
   * (171, (-464 + 342) / 2 = -61).
   */

  auto native_rc = ComputeScissorRect();

  auto target_rc = g_renderer->ConvertEFBRectangle(native_rc.rect);
  auto converted_rc =
      g_renderer->ConvertFramebufferRectangle(target_rc, g_renderer->GetCurrentFramebuffer());
  g_renderer->SetScissorRect(converted_rc);

  float x = g_renderer->EFBToScaledXf((xfmem.viewport.xOrig - native_rc.x_off) - xfmem.viewport.wd);
  float y = g_renderer->EFBToScaledYf((xfmem.viewport.yOrig - native_rc.y_off) + xfmem.viewport.ht);

  float width = g_renderer->EFBToScaledXf(2.0f * xfmem.viewport.wd);
  float height = g_renderer->EFBToScaledYf(-2.0f * xfmem.viewport.ht);
  float min_depth = (xfmem.viewport.farZ - xfmem.viewport.zRange) / 16777216.0f;
  float max_depth = xfmem.viewport.farZ / 16777216.0f;
  if (width < 0.f)
  {
    x += width;
    width *= -1;
  }
  if (height < 0.f)
  {
    y += height;
    height *= -1;
  }

  // The maximum depth that is written to the depth buffer should never exceed this value.
  // This is necessary because we use a 2^24 divisor for all our depth values to prevent
  // floating-point round-trip errors. However the console GPU doesn't ever write a value
  // to the depth buffer that exceeds 2^24 - 1.
  constexpr float GX_MAX_DEPTH = 16777215.0f / 16777216.0f;
  if (!g_ActiveConfig.backend_info.bSupportsDepthClamp)
  {
    // There's no way to support oversized depth ranges in this situation. Let's just clamp the
    // range to the maximum value supported by the console GPU and hope for the best.
    min_depth = std::clamp(min_depth, 0.0f, GX_MAX_DEPTH);
    max_depth = std::clamp(max_depth, 0.0f, GX_MAX_DEPTH);
  }

  if (g_renderer->UseVertexDepthRange())
  {
    // We need to ensure depth values are clamped the maximum value supported by the console GPU.
    // Taking into account whether the depth range is inverted or not.
    if (xfmem.viewport.zRange < 0.0f && g_ActiveConfig.backend_info.bSupportsReversedDepthRange)
    {
      min_depth = GX_MAX_DEPTH;
      max_depth = 0.0f;
    }
    else
    {
      min_depth = 0.0f;
      max_depth = GX_MAX_DEPTH;
    }
  }

  float near_depth, far_depth;
  if (g_ActiveConfig.backend_info.bSupportsReversedDepthRange)
  {
    // Set the reversed depth range.
    near_depth = max_depth;
    far_depth = min_depth;
  }
  else
  {
    // We use an inverted depth range here to apply the Reverse Z trick.
    // This trick makes sure we match the precision provided by the 1:0
    // clipping depth range on the hardware.
    near_depth = 1.0f - max_depth;
    far_depth = 1.0f - min_depth;
  }

  // Lower-left flip.
  if (g_ActiveConfig.backend_info.bUsesLowerLeftOrigin)
    y = static_cast<float>(g_renderer->GetCurrentFramebuffer()->GetHeight()) - y - height;

  g_renderer->SetViewport(x, y, width, height, near_depth, far_depth);
}

void SetDepthMode()
{
  g_vertex_manager->SetDepthStateChanged();
}

void SetBlendMode()
{
  g_vertex_manager->SetBlendingStateChanged();
}

/* Explanation of the magic behind ClearScreen:
  There's numerous possible formats for the pixel data in the EFB.
  However, in the HW accelerated backends we're always using RGBA8
  for the EFB format, which causes some problems:
  - We're using an alpha channel although the game doesn't
  - If the actual EFB format is RGBA6_Z24 or R5G6B5_Z16, we are using more bits per channel than the
  native HW

  To properly emulate the above points, we're doing the following:
  (1)
    - disable alpha channel writing of any kind of rendering if the actual EFB format doesn't use an
  alpha channel
    - NOTE: Always make sure that the EFB has been cleared to an alpha value of 0xFF in this case!
    - Same for color channels, these need to be cleared to 0x00 though.
  (2)
    - convert the RGBA8 color to RGBA6/RGB8/RGB565 and convert it to RGBA8 again
    - convert the Z24 depth value to Z16 and back to Z24
*/
void ClearScreen(const MathUtil::Rectangle<int>& rc)
{
  bool colorEnable = (bpmem.blendmode.colorupdate != 0);
  bool alphaEnable = (bpmem.blendmode.alphaupdate != 0);
  bool zEnable = (bpmem.zmode.updateenable != 0);
  auto pixel_format = bpmem.zcontrol.pixel_format;

  // (1): Disable unused color channels
  if (pixel_format == PixelFormat::RGB8_Z24 || pixel_format == PixelFormat::RGB565_Z16 ||
      pixel_format == PixelFormat::Z24)
  {
    alphaEnable = false;
  }

  if (colorEnable || alphaEnable || zEnable)
  {
    u32 color = (bpmem.clearcolorAR << 16) | bpmem.clearcolorGB;
    u32 z = bpmem.clearZValue;

    // (2) drop additional accuracy
    if (pixel_format == PixelFormat::RGBA6_Z24)
    {
      color = RGBA8ToRGBA6ToRGBA8(color);
    }
    else if (pixel_format == PixelFormat::RGB565_Z16)
    {
      color = RGBA8ToRGB565ToRGBA8(color);
      z = Z24ToZ16ToZ24(z);
    }
    g_renderer->ClearScreen(rc, colorEnable, alphaEnable, zEnable, color, z);
  }
}

void OnPixelFormatChange()
{
  // TODO : Check for Z compression format change
  // When using 16bit Z, the game may enable a special compression format which we might need to
  // handle. Only a few games like RS2 and RS3 even use z compression but it looks like they
  // always use ZFAR when using 16bit Z (on top of linear 24bit Z)

  // Besides, we currently don't even emulate 16bit depth and force it to 24bit.

  /*
   * When changing the EFB format, the pixel data won't get converted to the new format but stays
   * the same.
   * Since we are always using an RGBA8 buffer though, this causes issues in some games.
   * Thus, we reinterpret the old EFB data with the new format here.
   */
  if (!g_ActiveConfig.bEFBEmulateFormatChanges)
    return;

  const auto old_format = g_renderer->GetPrevPixelFormat();
  const auto new_format = bpmem.zcontrol.pixel_format;
  g_renderer->StorePixelFormat(new_format);

  DEBUG_LOG_FMT(VIDEO, "pixelfmt: pixel={}, zc={}", new_format, bpmem.zcontrol.zformat);

  // no need to reinterpret pixel data in these cases
  if (new_format == old_format || old_format == PixelFormat::INVALID_FMT)
    return;

  // Check for pixel format changes
  switch (old_format)
  {
  case PixelFormat::RGB8_Z24:
  case PixelFormat::Z24:
  {
    // Z24 and RGB8_Z24 are treated equal, so just return in this case
    if (new_format == PixelFormat::RGB8_Z24 || new_format == PixelFormat::Z24)
      return;

    if (new_format == PixelFormat::RGBA6_Z24)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGB8ToRGBA6);
      return;
    }
    else if (new_format == PixelFormat::RGB565_Z16)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGB8ToRGB565);
      return;
    }
  }
  break;

  case PixelFormat::RGBA6_Z24:
  {
    if (new_format == PixelFormat::RGB8_Z24 || new_format == PixelFormat::Z24)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGBA6ToRGB8);
      return;
    }
    else if (new_format == PixelFormat::RGB565_Z16)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGBA6ToRGB565);
      return;
    }
  }
  break;

  case PixelFormat::RGB565_Z16:
  {
    if (new_format == PixelFormat::RGB8_Z24 || new_format == PixelFormat::Z24)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGB565ToRGB8);
      return;
    }
    else if (new_format == PixelFormat::RGBA6_Z24)
    {
      g_renderer->ReinterpretPixelData(EFBReinterpretType::RGB565ToRGBA6);
      return;
    }
  }
  break;

  default:
    break;
  }

  ERROR_LOG_FMT(VIDEO, "Unhandled EFB format change: {} to {}", old_format, new_format);
}

void SetInterlacingMode(const BPCmd& bp)
{
  // TODO
  switch (bp.address)
  {
  case BPMEM_FIELDMODE:
  {
    // SDK always sets bpmem.lineptwidth.lineaspect via BPMEM_LINEPTWIDTH
    // just before this cmd
    DEBUG_LOG_FMT(VIDEO, "BPMEM_FIELDMODE texLOD:{} lineaspect:{}", bpmem.fieldmode.texLOD,
                  bpmem.lineptwidth.adjust_for_aspect_ratio);
  }
  break;
  case BPMEM_FIELDMASK:
  {
    // Determines if fields will be written to EFB (always computed)
    DEBUG_LOG_FMT(VIDEO, "BPMEM_FIELDMASK even:{} odd:{}", bpmem.fieldmask.even,
                  bpmem.fieldmask.odd);
  }
  break;
  default:
    ERROR_LOG_FMT(VIDEO, "SetInterlacingMode default");
    break;
  }
}
};  // namespace BPFunctions
