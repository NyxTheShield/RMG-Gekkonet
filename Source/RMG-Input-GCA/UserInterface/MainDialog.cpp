/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "MainDialog.hpp"

#include <RMG-Core/Settings.hpp>

using namespace UserInterface;

// Poll timer interval in ms
#define POLL_INTERVAL_MS 50
// Timeout for listen mode (5 seconds / 50ms = 100 ticks)
#define LISTEN_TIMEOUT_TICKS 100

MainDialog::MainDialog(QWidget* parent) : QDialog(parent)
{
    this->setupUi(this);

    // Initialize settings IDs array (order matches N64 button indices 0-13)
    m_MappingSettingsIDs = {
        SettingsID::GCAInput_Map_A,
        SettingsID::GCAInput_Map_B,
        SettingsID::GCAInput_Map_Start,
        SettingsID::GCAInput_Map_Z,
        SettingsID::GCAInput_Map_Z2,
        SettingsID::GCAInput_Map_L,
        SettingsID::GCAInput_Map_R,
        SettingsID::GCAInput_Map_DpadUp,
        SettingsID::GCAInput_Map_DpadDown,
        SettingsID::GCAInput_Map_DpadLeft,
        SettingsID::GCAInput_Map_DpadRight,
        SettingsID::GCAInput_Map_CUp,
        SettingsID::GCAInput_Map_CDown,
        SettingsID::GCAInput_Map_CLeft,
        SettingsID::GCAInput_Map_CRight
    };

    // Initialize mapping buttons array (same order)
    m_MappingButtons = {
        this->mapButtonA,
        this->mapButtonB,
        this->mapButtonStart,
        this->mapButtonZ,
        this->mapButtonZ2,
        this->mapButtonL,
        this->mapButtonR,
        this->mapButtonDpadUp,
        this->mapButtonDpadDown,
        this->mapButtonDpadLeft,
        this->mapButtonDpadRight,
        this->mapButtonCUp,
        this->mapButtonCDown,
        this->mapButtonCLeft,
        this->mapButtonCRight
    };

    // Initialize clear buttons array (same order)
    m_ClearButtons = {
        this->clearButtonA,
        this->clearButtonB,
        this->clearButtonStart,
        this->clearButtonZ,
        this->clearButtonZ2,
        this->clearButtonL,
        this->clearButtonR,
        this->clearButtonDpadUp,
        this->clearButtonDpadDown,
        this->clearButtonDpadLeft,
        this->clearButtonDpadRight,
        this->clearButtonCUp,
        this->clearButtonCDown,
        this->clearButtonCLeft,
        this->clearButtonCRight
    };

    // Connect mapping and clear buttons
    QIcon clearIcon = QIcon::fromTheme("delete-back-line");
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        connect(m_MappingButtons[i], &QPushButton::clicked, this, [this, i]() {
            onMappingButtonClicked(i);
        });
        connect(m_ClearButtons[i], &QPushButton::clicked, this, [this, i]() {
            onClearButtonClicked(i);
        });
        int mapHeight = m_MappingButtons[i]->sizeHint().height();
        m_ClearButtons[i]->setText("");
        m_ClearButtons[i]->setIcon(clearIcon);
        m_ClearButtons[i]->setIconSize(QSize(20, 16));
        m_ClearButtons[i]->setFixedSize(QSize(mapHeight, mapHeight));
        m_ClearButtons[i]->setStyleSheet(QStringLiteral("padding: 0px;"));
    }

    // Setup poll timer
    m_PollTimer = new QTimer(this);
    connect(m_PollTimer, &QTimer::timeout, this, &MainDialog::onPollTimerTimeout);

    // Load slider values
    this->deadZoneSlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_Deadzone));
    this->sensitivitySlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_Sensitivity));
    this->triggerTresholdSlider->setValue(CoreSettingsGetIntValue(SettingsID::GCAInput_TriggerTreshold));
    this->port1CheckBox->setChecked(CoreSettingsGetBoolValue(SettingsID::GCAInput_Port1Enabled));
    this->port2CheckBox->setChecked(CoreSettingsGetBoolValue(SettingsID::GCAInput_Port2Enabled));
    this->port3CheckBox->setChecked(CoreSettingsGetBoolValue(SettingsID::GCAInput_Port3Enabled));
    this->port4CheckBox->setChecked(CoreSettingsGetBoolValue(SettingsID::GCAInput_Port4Enabled));

    // Load button mappings
    loadMappings();
    updateMappingButtons();

    // Start adapter polling for config UI
    GCA_StartConfigPolling();
}

MainDialog::~MainDialog()
{
    if (m_PollTimer->isActive())
    {
        m_PollTimer->stop();
    }
    GCA_StopConfigPolling();
}

void MainDialog::loadMappings(void)
{
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        m_Mappings[i] = static_cast<GCInput>(CoreSettingsGetIntValue(m_MappingSettingsIDs[i]));
    }
}

void MainDialog::saveMappings(void)
{
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        CoreSettingsSetValue(m_MappingSettingsIDs[i], static_cast<int>(m_Mappings[i]));
    }
}

void MainDialog::setDefaultMappings(void)
{
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        m_Mappings[i] = static_cast<GCInput>(CoreSettingsGetDefaultIntValue(m_MappingSettingsIDs[i]));
    }
}

void MainDialog::updateMappingButtons(void)
{
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        m_MappingButtons[i]->setText(GCInputToString(m_Mappings[i]));
    }
}

void MainDialog::onClearButtonClicked(int index)
{
    // Cancel listen mode if active
    if (m_ListeningIndex >= 0)
    {
        m_MappingButtons[m_ListeningIndex]->setText(GCInputToString(m_Mappings[m_ListeningIndex]));
        m_ListeningIndex = -1;
        m_PollTimer->stop();
    }

    m_Mappings[index] = GCInput::None;
    m_MappingButtons[index]->setText(GCInputToString(GCInput::None));
}

void MainDialog::clearDuplicateMapping(int assignedIndex, GCInput input)
{
    for (int i = 0; i < N64_BUTTON_COUNT; i++)
    {
        if (i != assignedIndex && m_Mappings[i] == input)
        {
            m_Mappings[i] = GCInput::None;
            m_MappingButtons[i]->setText(GCInputToString(GCInput::None));
        }
    }
}

void MainDialog::onMappingButtonClicked(int index)
{
    // If already listening on another button, cancel it
    if (m_ListeningIndex >= 0 && m_ListeningIndex != index)
    {
        m_MappingButtons[m_ListeningIndex]->setText(GCInputToString(m_Mappings[m_ListeningIndex]));
    }

    m_ListeningIndex = index;
    m_ListenTickCount = 0;
    m_MappingButtons[index]->setText("...");

    // Capture current state as baseline
    m_PrevState = GCA_GetControllerState(0);

    // Start polling
    if (!m_PollTimer->isActive())
    {
        m_PollTimer->start(POLL_INTERVAL_MS);
    }
}

void MainDialog::onPollTimerTimeout(void)
{
    if (m_ListeningIndex < 0)
    {
        m_PollTimer->stop();
        return;
    }

    m_ListenTickCount++;

    // Timeout after 5 seconds
    if (m_ListenTickCount >= LISTEN_TIMEOUT_TICKS)
    {
        m_MappingButtons[m_ListeningIndex]->setText(GCInputToString(m_Mappings[m_ListeningIndex]));
        m_ListeningIndex = -1;
        m_PollTimer->stop();
        return;
    }

    GameCubeAdapterControllerState curr = GCA_GetControllerState(0);

    double triggerThreshold = static_cast<double>(this->triggerTresholdSlider->value()) / 100.0;
    double cStickThreshold = static_cast<double>(CoreSettingsGetIntValue(SettingsID::GCAInput_CButtonTreshold)) / 100.0;

    GCInput detected = DetectGCInput(m_PrevState, curr, triggerThreshold, cStickThreshold);

    if (detected != GCInput::None)
    {
        int idx = m_ListeningIndex;
        m_ListeningIndex = -1;
        m_PollTimer->stop();

        m_Mappings[idx] = detected;
        clearDuplicateMapping(idx, detected);
        m_MappingButtons[idx]->setText(GCInputToString(detected));
        return;
    }

    // Update previous state for next tick
    m_PrevState = curr;
}

void MainDialog::on_buttonBox_clicked(QAbstractButton* button)
{
    QPushButton *pushButton = (QPushButton *)button;
    QPushButton *okButton = this->buttonBox->button(QDialogButtonBox::Ok);
    QPushButton *defaultButton = this->buttonBox->button(QDialogButtonBox::RestoreDefaults);

    if (pushButton == okButton)
    {
        CoreSettingsSetValue(SettingsID::GCAInput_Deadzone, this->deadZoneSlider->value());
        CoreSettingsSetValue(SettingsID::GCAInput_Sensitivity, this->sensitivitySlider->value());
        CoreSettingsSetValue(SettingsID::GCAInput_TriggerTreshold, this->triggerTresholdSlider->value());
        CoreSettingsSetValue(SettingsID::GCAInput_Port1Enabled, this->port1CheckBox->isChecked());
        CoreSettingsSetValue(SettingsID::GCAInput_Port2Enabled, this->port2CheckBox->isChecked());
        CoreSettingsSetValue(SettingsID::GCAInput_Port3Enabled, this->port3CheckBox->isChecked());
        CoreSettingsSetValue(SettingsID::GCAInput_Port4Enabled, this->port4CheckBox->isChecked());
        saveMappings();
        CoreSettingsSave();
    }
    else if (pushButton == defaultButton)
    {
        this->deadZoneSlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_Deadzone));
        this->sensitivitySlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_Sensitivity));
        this->triggerTresholdSlider->setValue(CoreSettingsGetDefaultIntValue(SettingsID::GCAInput_TriggerTreshold));
        this->port1CheckBox->setChecked(CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_Port1Enabled));
        this->port2CheckBox->setChecked(CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_Port2Enabled));
        this->port3CheckBox->setChecked(CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_Port3Enabled));
        this->port4CheckBox->setChecked(CoreSettingsGetDefaultBoolValue(SettingsID::GCAInput_Port4Enabled));
        setDefaultMappings();
        updateMappingButtons();
    }
}

void MainDialog::on_deadZoneSlider_valueChanged(int value)
{
    this->deadZoneLabel->setText("Stick Deadzone: " + QString::number(value) + "%");
}

void MainDialog::on_sensitivitySlider_valueChanged(int value)
{
    this->sensitivityLabel->setText("Stick Sensitivity: " + QString::number(value) + "%");
}

void MainDialog::on_triggerTresholdSlider_valueChanged(int value)
{
    this->triggerTresholdLabel->setText("Trigger threshold: " + QString::number(value) + "%");
}
