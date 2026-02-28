/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraOptionsDialog.hpp"

#ifdef _WIN32

#include <RMG-Core/Settings.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>

KailleraOptionsDialog::KailleraOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setWindowTitle("Kaillera Options");
    setFixedSize(280, 200);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout();

    m_maxPlayers = new QSpinBox(this);
    m_maxPlayers->setRange(1, 16);
    form->addRow("Max players:", m_maxPlayers);

    m_maxPing = new QSpinBox(this);
    m_maxPing->setRange(1, 9999);
    form->addRow("Max ping:", m_maxPing);

    m_flashOnJoin = new QCheckBox("Flash taskbar on player join", this);
    m_beepOnJoin = new QCheckBox("Beep on player join", this);

    layout->addLayout(form);
    layout->addWidget(m_flashOnJoin);
    layout->addWidget(m_beepOnJoin);
    layout->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    loadSettings();
}

void KailleraOptionsDialog::loadSettings()
{
    m_maxPlayers->setValue(CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPlayers));
    m_maxPing->setValue(CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPing));
    m_flashOnJoin->setChecked(CoreSettingsGetBoolValue(SettingsID::Kaillera_FlashOnJoin));
    m_beepOnJoin->setChecked(CoreSettingsGetBoolValue(SettingsID::Kaillera_BeepOnJoin));
}

void KailleraOptionsDialog::saveSettings()
{
    CoreSettingsSetValue(SettingsID::Kaillera_MaxPlayers, m_maxPlayers->value());
    CoreSettingsSetValue(SettingsID::Kaillera_MaxPing, m_maxPing->value());
    CoreSettingsSetValue(SettingsID::Kaillera_FlashOnJoin, m_flashOnJoin->isChecked());
    CoreSettingsSetValue(SettingsID::Kaillera_BeepOnJoin, m_beepOnJoin->isChecked());
    CoreSettingsSave();
}

#endif // _WIN32
