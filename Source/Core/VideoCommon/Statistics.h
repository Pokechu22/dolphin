// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <vector>

struct BPMemory;
struct XFMemory;

struct Statistics
{
  int num_pixel_shaders_created;
  int num_pixel_shaders_alive;
  int num_vertex_shaders_created;
  int num_vertex_shaders_alive;

  int num_textures_created;
  int num_textures_uploaded;
  int num_textures_alive;

  int num_vertex_loaders;

  std::array<float, 6> proj;
  std::array<float, 16> gproj;
  std::array<float, 16> g2proj;

  struct RectangleInfo
  {
    struct ScissorInfo
    {
      ScissorInfo(const BPMemory& bpmemory);
      bool operator==(const ScissorInfo& other) const;

      int x0;
      int y0;
      int x1;
      int y1;
      int xOff;
      int yOff;
      // Raw/original versions
      int rxOff;
      int ryOff;
    };
    struct ViewportInfo
    {
      ViewportInfo(const XFMemory& xfmemory);
      bool operator==(const ViewportInfo& other) const;

      float vx0;
      float vy0;
      float vx1;
      float vy1;
    };

    RectangleInfo(const BPMemory& bpmemory, const XFMemory& xfmemory);
    bool Matches(const RectangleInfo& other, bool show_scissors, bool show_viewports) const;

    ScissorInfo scissor;
    ViewportInfo viewport;
  };

  std::vector<RectangleInfo> scissor_info;
  size_t current_scissor = 0;  // 0 => all, otherwise index + 1
  int scissor_scale = 10;
  bool allow_duplicate_scissors = false;
  bool show_scissors = true;
  bool show_viewports = false;
  bool show_text = true;

  struct ThisFrame
  {
    int num_bp_loads;
    int num_cp_loads;
    int num_xf_loads;

    int num_bp_loads_in_dl;
    int num_cp_loads_in_dl;
    int num_xf_loads_in_dl;

    int num_prims;
    int num_dl_prims;
    int num_shader_changes;

    int num_primitive_joins;
    int num_draw_calls;

    int num_dlists_called;

    int bytes_vertex_streamed;
    int bytes_index_streamed;
    int bytes_uniform_streamed;

    int num_triangles_clipped;
    int num_triangles_in;
    int num_triangles_rejected;
    int num_triangles_culled;
    int num_drawn_objects;
    int rasterized_pixels;
    int num_triangles_drawn;
    int num_vertices_loaded;
    int tev_pixels_in;
    int tev_pixels_out;

    int num_efb_peeks;
    int num_efb_pokes;
  };
  ThisFrame this_frame;
  void ResetFrame();
  void SwapDL();
  void AddScissorRect(const BPMemory& bpmemory, const XFMemory& xfmemory);
  void Display() const;
  void DisplayProj() const;
  void DisplayScissor();
};

extern Statistics g_stats;

#define STATISTICS

#ifdef STATISTICS
#define INCSTAT(a) (a)++;
#define ADDSTAT(a, b) (a) += (b);
#define SETSTAT(a, x) (a) = (int)(x);
#else
#define INCSTAT(a) ;
#define ADDSTAT(a, b) ;
#define SETSTAT(a, x) ;
#endif
