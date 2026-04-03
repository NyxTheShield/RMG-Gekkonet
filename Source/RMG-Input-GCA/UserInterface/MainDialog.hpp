/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef MAINDIALOG_HPP
#define MAINDIALOG_HPP

#include <QDialog>
#include <QAbstractButton>
#include <QPushButton>
#include <QTimer>
#include <array>

#include <RMG-Core/Settings.hpp>
#include "../GCInput.hpp"
#include "../Adapter.hpp"

#include "ui_MainDialog.h"

namespace UserInterface
{
class MainDialog : public QDialog, private Ui::MainDialog
{
Q_OBJECT
public:
    MainDialog(QWidget *parent);
    ~MainDialog(void);

private slots:
    void on_buttonBox_clicked(QAbstractButton *);

    void on_deadZoneSlider_valueChanged(int value);
    void on_sensitivitySlider_valueChanged(int value);
    void on_triggerTresholdSlider_valueChanged(int value);
    void on_cButtonTresholdSlider_valueChanged(int value);

    void onMappingButtonClicked(int index);
    void onClearButtonClicked(int index);
    void onPollTimerTimeout(void);
    void onAxisReadoutTimer(void);

private:
    void loadMappings(void);
    void saveMappings(void);
    void setDefaultMappings(void);
    void updateMappingButtons(void);
    void clearDuplicateMapping(int assignedIndex, GCInput input);
    void updateAxisReadout(void);

    std::array<QPushButton*, N64_BUTTON_COUNT> m_MappingButtons;
    std::array<QPushButton*, N64_BUTTON_COUNT> m_ClearButtons;
    std::array<GCInput, N64_BUTTON_COUNT> m_Mappings;
    std::array<SettingsID, N64_BUTTON_COUNT> m_MappingSettingsIDs;

    QTimer* m_PollTimer = nullptr;
    QTimer* m_AxisReadoutTimer = nullptr;
    int m_ListeningIndex = -1;
    GameCubeAdapterControllerState m_PrevState = {};
    int m_ListenTickCount = 0;
};
}

#endif // MAINDIALOG_HPP
