// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Statistics.h"

#include <cstring>
#include <utility>

#include <imgui.h>

#include "VideoCommon/BPMemory.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

Statistics g_stats;

void Statistics::ResetFrame()
{
  this_frame = {};
  scissor_info.clear();
}

void Statistics::SwapDL()
{
  std::swap(this_frame.num_dl_prims, this_frame.num_prims);
  std::swap(this_frame.num_xf_loads_in_dl, this_frame.num_xf_loads);
  std::swap(this_frame.num_cp_loads_in_dl, this_frame.num_cp_loads);
  std::swap(this_frame.num_bp_loads_in_dl, this_frame.num_bp_loads);
}

void Statistics::Display() const
{
  const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
  ImGui::SetNextWindowPos(ImVec2(10.0f * scale, 10.0f * scale), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(275.0f * scale, 400.0f * scale),
                                      ImGui::GetIO().DisplaySize);
  if (!ImGui::Begin("Statistics", nullptr, ImGuiWindowFlags_NoNavInputs))
  {
    ImGui::End();
    return;
  }

  ImGui::Columns(2, "Statistics", true);

  const auto draw_statistic = [](const char* name, const char* format, auto&&... args) {
    ImGui::TextUnformatted(name);
    ImGui::NextColumn();
    ImGui::Text(format, std::forward<decltype(args)>(args)...);
    ImGui::NextColumn();
  };

  if (g_ActiveConfig.backend_info.api_type == APIType::Nothing)
  {
    draw_statistic("Objects", "%d", this_frame.num_drawn_objects);
    draw_statistic("Vertices Loaded", "%d", this_frame.num_vertices_loaded);
    draw_statistic("Triangles Input", "%d", this_frame.num_triangles_in);
    draw_statistic("Triangles Rejected", "%d", this_frame.num_triangles_rejected);
    draw_statistic("Triangles Culled", "%d", this_frame.num_triangles_culled);
    draw_statistic("Triangles Clipped", "%d", this_frame.num_triangles_clipped);
    draw_statistic("Triangles Drawn", "%d", this_frame.num_triangles_drawn);
    draw_statistic("Rasterized Pix", "%d", this_frame.rasterized_pixels);
    draw_statistic("TEV Pix In", "%d", this_frame.tev_pixels_in);
    draw_statistic("TEV Pix Out", "%d", this_frame.tev_pixels_out);
  }

  draw_statistic("Textures created", "%d", num_textures_created);
  draw_statistic("Textures uploaded", "%d", num_textures_uploaded);
  draw_statistic("Textures alive", "%d", num_textures_alive);
  draw_statistic("pshaders created", "%d", num_pixel_shaders_created);
  draw_statistic("pshaders alive", "%d", num_pixel_shaders_alive);
  draw_statistic("vshaders created", "%d", num_vertex_shaders_created);
  draw_statistic("vshaders alive", "%d", num_vertex_shaders_alive);
  draw_statistic("shaders changes", "%d", this_frame.num_shader_changes);
  draw_statistic("dlists called", "%d", this_frame.num_dlists_called);
  draw_statistic("Primitive joins", "%d", this_frame.num_primitive_joins);
  draw_statistic("Draw calls", "%d", this_frame.num_draw_calls);
  draw_statistic("Primitives", "%d", this_frame.num_prims);
  draw_statistic("Primitives (DL)", "%d", this_frame.num_dl_prims);
  draw_statistic("XF loads", "%d", this_frame.num_xf_loads);
  draw_statistic("XF loads (DL)", "%d", this_frame.num_xf_loads_in_dl);
  draw_statistic("CP loads", "%d", this_frame.num_cp_loads);
  draw_statistic("CP loads (DL)", "%d", this_frame.num_cp_loads_in_dl);
  draw_statistic("BP loads", "%d", this_frame.num_bp_loads);
  draw_statistic("BP loads (DL)", "%d", this_frame.num_bp_loads_in_dl);
  draw_statistic("Vertex streamed", "%i kB", this_frame.bytes_vertex_streamed / 1024);
  draw_statistic("Index streamed", "%i kB", this_frame.bytes_index_streamed / 1024);
  draw_statistic("Uniform streamed", "%i kB", this_frame.bytes_uniform_streamed / 1024);
  draw_statistic("Vertex Loaders", "%d", num_vertex_loaders);
  draw_statistic("EFB peeks:", "%d", this_frame.num_efb_peeks);
  draw_statistic("EFB pokes:", "%d", this_frame.num_efb_pokes);

  ImGui::Columns(1);

  ImGui::End();
}

// Is this really needed?
void Statistics::DisplayProj() const
{
  if (!ImGui::Begin("Projection Statistics", nullptr, ImGuiWindowFlags_NoNavInputs))
  {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Projection #: X for Raw 6=0 (X for Raw 6!=0)");
  ImGui::NewLine();
  ImGui::Text("Projection 0: %f (%f) Raw 0: %f", gproj[0], g2proj[0], proj[0]);
  ImGui::Text("Projection 1: %f (%f)", gproj[1], g2proj[1]);
  ImGui::Text("Projection 2: %f (%f) Raw 1: %f", gproj[2], g2proj[2], proj[1]);
  ImGui::Text("Projection 3: %f (%f)", gproj[3], g2proj[3]);
  ImGui::Text("Projection 4: %f (%f)", gproj[4], g2proj[4]);
  ImGui::Text("Projection 5: %f (%f) Raw 2: %f", gproj[5], g2proj[5], proj[2]);
  ImGui::Text("Projection 6: %f (%f) Raw 3: %f", gproj[6], g2proj[6], proj[3]);
  ImGui::Text("Projection 7: %f (%f)", gproj[7], g2proj[7]);
  ImGui::Text("Projection 8: %f (%f)", gproj[8], g2proj[8]);
  ImGui::Text("Projection 9: %f (%f)", gproj[9], g2proj[9]);
  ImGui::Text("Projection 10: %f (%f) Raw 4: %f", gproj[10], g2proj[10], proj[4]);
  ImGui::Text("Projection 11: %f (%f) Raw 5: %f", gproj[11], g2proj[11], proj[5]);
  ImGui::Text("Projection 12: %f (%f)", gproj[12], g2proj[12]);
  ImGui::Text("Projection 13: %f (%f)", gproj[13], g2proj[13]);
  ImGui::Text("Projection 14: %f (%f)", gproj[14], g2proj[14]);
  ImGui::Text("Projection 15: %f (%f)", gproj[15], g2proj[15]);

  ImGui::End();
}

void Statistics::AddScissorRect(const BPMemory& bpmemory, const XFMemory& xfmemory)
{
  RectangleInfo info{bpmemory, xfmemory};
  bool add;
  if (scissor_info.empty())
  {
    add = true;
  }
  else
  {
    if (allow_duplicate_scissors)
    {
      // Only check the last entry
      add = !scissor_info.back().Matches(info, show_scissors, show_viewports);
    }
    else
    {
      add = std::find_if(scissor_info.begin(), scissor_info.end(), [&](auto& i) {
              return i.Matches(info, show_scissors, show_viewports);
            }) == scissor_info.end();
    }
  }
  if (add)
    scissor_info.push_back(std::move(info));
}

Statistics::RectangleInfo::ScissorInfo::ScissorInfo(const BPMemory& bpmemory)
{
  x0 = bpmemory.scissorTL.x - 342;
  y0 = bpmemory.scissorTL.y - 342;
  x1 = bpmemory.scissorBR.x - 342 + 1;
  y1 = bpmemory.scissorBR.y - 342 + 1;
  xOff = ((bpmemory.scissorOffset.x << 1) - 342);
  yOff = ((bpmemory.scissorOffset.y << 1) - 342);
}

bool Statistics::RectangleInfo::ScissorInfo::operator==(const ScissorInfo& other) const
{
  return memcmp(this, &other, sizeof(ScissorInfo)) == 0;
}

Statistics::RectangleInfo::ViewportInfo::ViewportInfo(const XFMemory& xfmemory)
{
  float vxCenter = xfmemory.viewport.xOrig - 342;
  float vyCenter = xfmemory.viewport.yOrig - 342;
  // Subtract for x and add for y since y height is usually negative
  vx0 = vxCenter - xfmemory.viewport.wd;
  vy0 = vyCenter + xfmemory.viewport.ht;
  vx1 = vxCenter + xfmemory.viewport.wd;
  vy1 = vyCenter - xfmemory.viewport.ht;
}

bool Statistics::RectangleInfo::ViewportInfo::operator==(const ViewportInfo& other) const
{
  return memcmp(this, &other, sizeof(ViewportInfo)) == 0;
}

Statistics::RectangleInfo::RectangleInfo(const BPMemory& bpmemory, const XFMemory& xfmemory)
    : scissor{bpmemory}, viewport{xfmemory}
{
}

bool Statistics::RectangleInfo::Matches(const RectangleInfo& other, bool show_scissors,
                                        bool show_viewports) const
{
  if (show_scissors && (scissor != other.scissor))
    return false;
  if (show_viewports && (viewport != other.viewport))
    return false;
  return true;
}

void Statistics::DisplayScissor()
{
  // TODO: This is the same position as the regular statistics text
  const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
  ImGui::SetNextWindowPos(ImVec2(10.0f * scale, 10.0f * scale), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Scissor Rectangles", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::End();
    return;
  }

  if (ImGui::TreeNode("Options"))
  {
    ImGui::Checkbox("Allow Duplicates", &allow_duplicate_scissors);
    ImGui::Checkbox("Show Scissors", &show_scissors);
    ImGui::Checkbox("Show Viewports", &show_viewports);
    ImGui::Checkbox("Show Text", &show_text);
    ImGui::DragInt("Scale", &scissor_scale, .2f, 1, 16);
    ImGui::TreePop();
  }

  ImGui::BeginDisabled(current_scissor == 0);
  if (ImGui::ArrowButton("##left", ImGuiDir_Left))
  {
    current_scissor--;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(current_scissor >= scissor_info.size());
  if (ImGui::ArrowButton("##right", ImGuiDir_Right))
  {
    current_scissor++;
    if (current_scissor > scissor_info.size())
    {
      current_scissor = scissor_info.size();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (current_scissor == 0)
    ImGui::Text("Displaying all %zu rectangles", scissor_info.size());
  else if (current_scissor <= scissor_info.size())
    ImGui::Text("Displaying rectangle %zu / %zu", current_scissor, scissor_info.size());
  else
    ImGui::Text("Displaying rectangle %zu / %zu (OoB)", current_scissor, scissor_info.size());

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(1024 * 3 / scissor_scale, 1024 * 3 / scissor_scale));

  constexpr int DRAW_START = -1024;
  // constexpr int DRAW_START = -2048;
  constexpr int DRAW_END = DRAW_START + 3 * 1024;

  const auto vec = [&](int x, int y, int xoff = 0, int yoff = 0) {
    return ImVec2(p.x + (float(x - DRAW_START) / scissor_scale) + xoff,
                  p.y + (float(y - DRAW_START) / scissor_scale) + yoff);
  };

  // First draw half-rectangles for copied EFB regions, along with the coordinates
  for (int x = DRAW_START; x < DRAW_END; x += 1024)
  {
    for (int y = DRAW_START; y < DRAW_END; y += 1024)
    {
      if (x != 0 || y != 0)
      {
        draw_list->AddLine(vec(x, y + EFB_HEIGHT), vec(x + EFB_WIDTH, y + EFB_HEIGHT),
                           IM_COL32(64, 64, 64, 255));
        draw_list->AddLine(vec(x + EFB_WIDTH, y), vec(x + EFB_WIDTH, y + EFB_HEIGHT),
                           IM_COL32(64, 64, 64, 255));
      }
      auto coord = fmt::format("{:+}\n{:+}", x, y);
      draw_list->AddText(vec(x, y, +3, +2), IM_COL32(64, 64, 64, 255), coord.data());
    }
  }

  // Now draw gridlines (over those rectangles)
  for (int x = DRAW_START; x <= DRAW_END; x += 1024)
    draw_list->AddLine(vec(x, DRAW_START), vec(x, DRAW_END), IM_COL32(128, 128, 128, 255));
  for (int y = DRAW_START; y <= DRAW_END; y += 1024)
    draw_list->AddLine(vec(DRAW_START, y), vec(DRAW_END, y), IM_COL32(128, 128, 128, 255));

  // Now draw a white rectangle for the real EFB region
  draw_list->AddRect(vec(0, 0), vec(EFB_WIDTH, EFB_HEIGHT), IM_COL32(255, 255, 255, 255));

  const auto draw_x = [&](int x, int y, int size, ImU32 col) {
    draw_list->AddLine(vec(x, y, -size, -size), vec(x, y, +size, +size), col);
    draw_list->AddLine(vec(x, y, -size, +size), vec(x, y, +size, -size), col);
  };
  static constexpr std::array<ImU32, 6> COLORS = {
      IM_COL32(255, 0, 0, 255),   IM_COL32(255, 255, 0, 255), IM_COL32(0, 255, 0, 255),
      IM_COL32(0, 255, 255, 255), IM_COL32(0, 0, 255, 255),   IM_COL32(255, 0, 255, 255),
  };
  const auto draw_scissor = [&](size_t index) {
    const RectangleInfo& rect_info = scissor_info[index];
    const ImU32 col = COLORS[index % COLORS.size()];
    if (show_scissors)
    {
      const auto& info = rect_info.scissor;
      draw_x(-info.xOff, -info.yOff, 4, col);
      draw_list->AddRect(vec(info.x0 - info.xOff, info.y0 - info.yOff),
                         vec(info.x1 - info.xOff, info.y1 - info.yOff), col);
      if (show_text)
      {
        ImGui::Text("Scissor %zu: x0 %d y0 %d x1 %d y1 %d xOff %d yOff %d", index + 1, info.x0,
                    info.y0, info.x1, info.y1, info.xOff, info.yOff);
      }
    }
    if (show_viewports)
    {
      const auto& info = rect_info.viewport;
      draw_list->AddRect(vec(info.vx0, info.vy0), vec(info.vx1, info.vy1), col);
      if (show_text)
      {
        ImGui::Text("Viewport %zu: vx0 %.1f vy0 %.1f vx1 %.1f vy1 %.1f", index + 1, info.vx0, info.vy0,
                    info.vx1, info.vy1);
      }
    }
  };

  if (current_scissor == 0)
  {
    for (size_t i = 0; i < scissor_info.size(); i++)
      draw_scissor(i);
  }
  else if (current_scissor <= scissor_info.size())
  {
    // This bounds check is needed since we only clamp when changing the value; different frames may
    // have different numbers
    draw_scissor(current_scissor - 1);
  }
  else if (show_text)
  {
    if (show_scissors)
      ImGui::Text("Scissor %zu: Does not exist", current_scissor);
    if (show_viewports)
      ImGui::Text("Viewport %zu: Does not exist", current_scissor);
  }

  ImGui::End();
}
