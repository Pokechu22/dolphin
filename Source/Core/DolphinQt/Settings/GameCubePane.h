// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <QWidget>
#include <initializer_list>
#include <unordered_map>

class QCheckBox;
class QComboBox;
class QPushButton;

class GameCubePane : public QWidget
{
  Q_OBJECT
public:
  explicit GameCubePane();

private:
  enum class SlotIndex
  {
    A,
    B,
    SP1,
  };
  static constexpr auto SLOTS = {SlotIndex::A, SlotIndex::B, SlotIndex::SP1};

  u8 SlotToEXIChannel(SlotIndex slot);
  u8 SlotToEXIDevice(SlotIndex slot);

  void CreateWidgets();
  void ConnectWidgets();

  void LoadSettings();
  void SaveSettings();

  void UpdateButton(SlotIndex slot);
  void OnConfigPressed(SlotIndex slot);

  QCheckBox* m_skip_main_menu;
  QComboBox* m_language_combo;

  std::unordered_map<SlotIndex, QPushButton*> m_slot_buttons{};
  std::unordered_map<SlotIndex, QComboBox*> m_slot_combos{};
};
