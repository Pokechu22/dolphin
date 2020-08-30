// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <QWidget>
#include <unordered_map>

class QCheckBox;
class QComboBox;
class QPushButton;

namespace ExpansionInterface
{
enum class Slot : int;
}

class GameCubePane : public QWidget
{
  Q_OBJECT
public:
  explicit GameCubePane();

private:
  void CreateWidgets();
  void ConnectWidgets();

  void LoadSettings();
  void SaveSettings();

  void UpdateButton(ExpansionInterface::Slot slot);
  void OnConfigPressed(ExpansionInterface::Slot slot);

  QCheckBox* m_skip_main_menu;
  QComboBox* m_language_combo;

  std::unordered_map<ExpansionInterface::Slot, QPushButton*> m_slot_buttons{};
  std::unordered_map<ExpansionInterface::Slot, QComboBox*> m_slot_combos{};
};
