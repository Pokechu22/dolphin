// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Settings/GameCubePane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/MsgHandler.h"

#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/GCMemcard/GCMemcard.h"

#include "DolphinQt/Config/Mapping/MappingWindow.h"
#include "DolphinQt/GCMemcardManager.h"
#include "DolphinQt/QtUtils/ModalMessageBox.h"

GameCubePane::GameCubePane()
{
  CreateWidgets();
  LoadSettings();
  ConnectWidgets();
}

void GameCubePane::CreateWidgets()
{
  using ExpansionInterface::EXIDeviceType;
  using ExpansionInterface::Slot;

  QVBoxLayout* layout = new QVBoxLayout(this);

  // IPL Settings
  QGroupBox* ipl_box = new QGroupBox(tr("IPL Settings"), this);
  QGridLayout* ipl_layout = new QGridLayout(ipl_box);
  ipl_box->setLayout(ipl_layout);

  m_skip_main_menu = new QCheckBox(tr("Skip Main Menu"), ipl_box);
  m_language_combo = new QComboBox(ipl_box);
  m_language_combo->setCurrentIndex(-1);

  // Add languages
  for (const auto& entry : {std::make_pair(tr("English"), 0), std::make_pair(tr("German"), 1),
                            std::make_pair(tr("French"), 2), std::make_pair(tr("Spanish"), 3),
                            std::make_pair(tr("Italian"), 4), std::make_pair(tr("Dutch"), 5)})
  {
    m_language_combo->addItem(entry.first, entry.second);
  }

  ipl_layout->addWidget(m_skip_main_menu, 0, 0);
  ipl_layout->addWidget(new QLabel(tr("System Language:")), 1, 0);
  ipl_layout->addWidget(m_language_combo, 1, 1);

  // Device Settings
  QGroupBox* device_box = new QGroupBox(tr("Device Settings"), this);
  QGridLayout* device_layout = new QGridLayout(device_box);
  device_box->setLayout(device_layout);

  for (auto slot : ExpansionInterface::SLOTS)
  {
    m_slot_combos[slot] = new QComboBox(device_box);
    m_slot_buttons[slot] = new QPushButton(tr("..."), device_box);
    m_slot_buttons[slot]->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  // Add slot devices

  for (const auto& entry : {std::make_pair(tr("<Nothing>"), EXIDeviceType::None),
                            std::make_pair(tr("Dummy"), EXIDeviceType::Dummy),
                            std::make_pair(tr("Memory Card"), EXIDeviceType::MemoryCard),
                            std::make_pair(tr("GCI Folder"), EXIDeviceType::MemoryCardFolder),
                            std::make_pair(tr("USB Gecko"), EXIDeviceType::Gecko),
                            std::make_pair(tr("Advance Game Port"), EXIDeviceType::AGP),
                            std::make_pair(tr("Microphone"), EXIDeviceType::Microphone),
                            std::make_pair(tr("SD Adapter"), EXIDeviceType::SD)})
  {
    m_slot_combos[Slot::A]->addItem(entry.first, static_cast<int>(entry.second));
    m_slot_combos[Slot::B]->addItem(entry.first, static_cast<int>(entry.second));
  }

  // Add SP1 devices

  std::vector<std::pair<QString, ExpansionInterface::EXIDeviceType>> sp1Entries{
      std::make_pair(tr("<Nothing>"), ExpansionInterface::EXIDeviceType::None),
      std::make_pair(tr("Dummy"), ExpansionInterface::EXIDeviceType::Dummy),
      std::make_pair(tr("Broadband Adapter (TAP)"), ExpansionInterface::EXIDeviceType::Ethernet),
      std::make_pair(tr("Broadband Adapter (XLink Kai)"),
                     ExpansionInterface::EXIDeviceType::EthernetXLink)};
#if defined(__APPLE__)
  sp1Entries.emplace_back(std::make_pair(tr("Broadband Adapter (tapserver)"),
                                         ExpansionInterface::EXIDeviceType::EthernetTapServer));
#endif
  for (const auto& entry : sp1Entries)
  {
    m_slot_combos[Slot::SP1]->addItem(entry.first, static_cast<int>(entry.second));
  }

  device_layout->addWidget(new QLabel(tr("Slot A:")), 0, 0);
  device_layout->addWidget(m_slot_combos[Slot::A], 0, 1);
  device_layout->addWidget(m_slot_buttons[Slot::A], 0, 2);
  device_layout->addWidget(new QLabel(tr("Slot B:")), 1, 0);
  device_layout->addWidget(m_slot_combos[Slot::B], 1, 1);
  device_layout->addWidget(m_slot_buttons[Slot::B], 1, 2);
  device_layout->addWidget(new QLabel(tr("SP1:")), 2, 0);
  device_layout->addWidget(m_slot_combos[Slot::SP1], 2, 1);
  device_layout->addWidget(m_slot_buttons[Slot::SP1], 2, 2);

  layout->addWidget(ipl_box);
  layout->addWidget(device_box);

  layout->addStretch();

  setLayout(layout);
}

void GameCubePane::ConnectWidgets()
{
  // IPL Settings
  connect(m_skip_main_menu, &QCheckBox::stateChanged, this, &GameCubePane::SaveSettings);
  connect(m_language_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &GameCubePane::SaveSettings);

  // Device Settings
  for (auto slot : ExpansionInterface::SLOTS)
  {
    connect(m_slot_combos[slot], qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, slot] { UpdateButton(slot); });
    connect(m_slot_combos[slot], qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GameCubePane::SaveSettings);
    connect(m_slot_buttons[slot], &QPushButton::clicked, [this, slot] { OnConfigPressed(slot); });
  }
}

void GameCubePane::UpdateButton(ExpansionInterface::Slot slot)
{
  const auto value =
      static_cast<ExpansionInterface::EXIDeviceType>(m_slot_combos[slot]->currentData().toInt());
  bool has_config = false;

  switch (slot)
  {
  case ExpansionInterface::Slot::A:
  case ExpansionInterface::Slot::B:
    has_config = (value == ExpansionInterface::EXIDeviceType::MemoryCard ||
                  value == ExpansionInterface::EXIDeviceType::AGP ||
                  value == ExpansionInterface::EXIDeviceType::Microphone);
    break;
  case ExpansionInterface::Slot::SP1:
    has_config = (value == ExpansionInterface::EXIDeviceType::Ethernet ||
                  value == ExpansionInterface::EXIDeviceType::EthernetXLink);
    break;
  }

  m_slot_buttons[slot]->setEnabled(has_config);
}

void GameCubePane::OnConfigPressed(ExpansionInterface::Slot slot)
{
  using ExpansionInterface::EXIDeviceType;
  using ExpansionInterface::Slot;

  QString filter;
  bool memcard = false;

  switch (static_cast<EXIDeviceType>(m_slot_combos[slot]->currentData().toInt()))
  {
  case EXIDeviceType::MemoryCard:
    filter = tr("GameCube Memory Cards (*.raw *.gcp)");
    memcard = true;
    break;
  case EXIDeviceType::AGP:
    filter = tr("Game Boy Advance Carts (*.gba)");
    break;
  case EXIDeviceType::Microphone:
    // TODO: convert MappingWindow to use SlotIndex?
    MappingWindow(this, MappingWindow::Type::MAPPING_GC_MICROPHONE, static_cast<int>(slot)).exec();
    return;
  case EXIDeviceType::Ethernet:
  {
    bool ok;
    const auto new_mac = QInputDialog::getText(
        // i18n: MAC stands for Media Access Control. A MAC address uniquely identifies a network
        // interface (physical) like a serial number. "MAC" should be kept in translations.
        this, tr("Broadband Adapter MAC address"), tr("Enter new Broadband Adapter MAC address:"),
        QLineEdit::Normal, QString::fromStdString(SConfig::GetInstance().m_bba_mac), &ok);
    if (ok)
      SConfig::GetInstance().m_bba_mac = new_mac.toStdString();
    return;
  }
  case EXIDeviceType::EthernetXLink:
  {
    bool ok;
    const auto new_dest = QInputDialog::getText(
        this, tr("Broadband Adapter (XLink Kai) Destination Address"),
        tr("Enter IP address of device running the XLink Kai Client.\nFor more information see"
           " https://www.teamxlink.co.uk/wiki/Dolphin"),
        QLineEdit::Normal, QString::fromStdString(SConfig::GetInstance().m_bba_xlink_ip), &ok);
    if (ok)
      SConfig::GetInstance().m_bba_xlink_ip = new_dest.toStdString();
    return;
  }
  default:
    qFatal("unknown settings pressed");
  }

  QString filename = QFileDialog::getSaveFileName(
      this, tr("Choose a file to open"), QString::fromStdString(File::GetUserPath(D_GCUSER_IDX)),
      filter, 0, QFileDialog::DontConfirmOverwrite);

  if (filename.isEmpty())
    return;

  QString path_abs = QFileInfo(filename).absoluteFilePath();

  // Memcard validity checks
  if (memcard)
  {
    if (File::Exists(filename.toStdString()))
    {
      auto [error_code, mc] = Memcard::GCMemcard::Open(filename.toStdString());

      if (error_code.HasCriticalErrors() || !mc || !mc->IsValid())
      {
        ModalMessageBox::critical(
            this, tr("Error"),
            tr("The file\n%1\nis either corrupted or not a GameCube memory card file.\n%2")
                .arg(filename)
                .arg(GCMemcardManager::GetErrorMessagesForErrorCode(error_code)));
        return;
      }
    }

    bool other_slot_memcard =
        m_slot_combos[slot == Slot::A ? Slot::B : Slot::A]->currentData().toInt() ==
        static_cast<int>(EXIDeviceType::MemoryCard);
    if (other_slot_memcard)
    {
      QString path_other =
          QFileInfo(QString::fromStdString(slot == Slot::A ?
                                               Config::Get(Config::MAIN_MEMCARD_B_PATH) :
                                               Config::Get(Config::MAIN_MEMCARD_A_PATH)))
              .absoluteFilePath();

      if (path_abs == path_other)
      {
        ModalMessageBox::critical(this, tr("Error"),
                                  tr("The same file can't be used in both slots."));
        return;
      }
    }
  }

  QString path_old;
  if (memcard)
  {
    path_old = QFileInfo(QString::fromStdString(slot == Slot::A ?
                                                    Config::Get(Config::MAIN_MEMCARD_A_PATH) :
                                                    Config::Get(Config::MAIN_MEMCARD_B_PATH)))
                   .absoluteFilePath();
  }
  else
  {
    path_old =
        QFileInfo(QString::fromStdString(slot == Slot::A ? SConfig::GetInstance().m_strGbaCartA :
                                                           SConfig::GetInstance().m_strGbaCartB))
            .absoluteFilePath();
  }

  if (memcard)
  {
    if (slot == Slot::A)
    {
      Config::SetBase(Config::MAIN_MEMCARD_A_PATH, path_abs.toStdString());
    }
    else
    {
      Config::SetBase(Config::MAIN_MEMCARD_B_PATH, path_abs.toStdString());
    }
  }
  else
  {
    if (slot == Slot::A)
    {
      SConfig::GetInstance().m_strGbaCartA = path_abs.toStdString();
    }
    else
    {
      SConfig::GetInstance().m_strGbaCartB = path_abs.toStdString();
    }
  }

  if (Core::IsRunning() && path_abs != path_old)
  {
    ExpansionInterface::ChangeDevice(SlotToEXIChannel(slot),
                                     memcard ? EXIDeviceType::MemoryCard : EXIDeviceType::AGP,
                                     SlotToEXIDevice(slot));
  }
}

void GameCubePane::LoadSettings()
{
  const SConfig& params = SConfig::GetInstance();

  // IPL Settings
  m_skip_main_menu->setChecked(params.bHLE_BS2);
  m_language_combo->setCurrentIndex(m_language_combo->findData(params.SelectedLanguage));

  bool have_menu = false;

  for (const std::string& dir : {USA_DIR, JAP_DIR, EUR_DIR})
  {
    const auto path = DIR_SEP + dir + DIR_SEP GC_IPL;
    if (File::Exists(File::GetUserPath(D_GCUSER_IDX) + path) ||
        File::Exists(File::GetSysDirectory() + GC_SYS_DIR + path))
    {
      have_menu = true;
      break;
    }
  }

  m_skip_main_menu->setEnabled(have_menu);
  m_skip_main_menu->setToolTip(have_menu ? QString{} :
                                           tr("Put Main Menu roms in User/GC/{region}."));

  // Device Settings

  for (auto slot : ExpansionInterface::SLOTS)
  {
    QSignalBlocker blocker(m_slot_combos[slot]);
    m_slot_combos[slot]->setCurrentIndex(
        m_slot_combos[slot]->findData(static_cast<int>(SConfig::GetInstance().m_EXIDevice[slot])));
    UpdateButton(slot);
  }
}

void GameCubePane::SaveSettings()
{
  Config::ConfigChangeCallbackGuard config_guard;

  SConfig& params = SConfig::GetInstance();

  // IPL Settings
  params.bHLE_BS2 = m_skip_main_menu->isChecked();
  Config::SetBaseOrCurrent(Config::MAIN_SKIP_IPL, m_skip_main_menu->isChecked());
  params.SelectedLanguage = m_language_combo->currentData().toInt();
  Config::SetBaseOrCurrent(Config::MAIN_GC_LANGUAGE, m_language_combo->currentData().toInt());

  for (auto slot : ExpansionInterface::SLOTS)
  {
    const auto dev =
        static_cast<ExpansionInterface::EXIDeviceType>(m_slot_combos[slot]->currentData().toInt());

    if (Core::IsRunning() && SConfig::GetInstance().m_EXIDevice[slot] != dev)
    {
      ExpansionInterface::ChangeDevice(SlotToEXIChannel(slot), dev, SlotToEXIDevice(slot));
    }

    SConfig::GetInstance().m_EXIDevice[slot] = dev;
    switch (slot)
    {
    case ExpansionInterface::Slot::A:
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, dev);
      break;
    case ExpansionInterface::Slot::B:
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, dev);
      break;
    case ExpansionInterface::Slot::SP1:
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1, dev);
      break;
    }
  }
  LoadSettings();
}
