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

#ifdef NETPLAY

#include <RMG-Core/Settings.hpp>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QIcon>

KailleraOptionsDialog::KailleraOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName("LobbyOptionsDialog");
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setWindowTitle("Lobby Options");
    setMinimumWidth(420);
    setStyleSheet(
        "QDialog#LobbyOptionsDialog {"
        "  background-color: palette(base);"
        "}"
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  min-height: 24px;"
        "  padding: 2px 8px;"
        "  background-color: palette(base);"
        "}"
        "QDialogButtonBox QPushButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  min-height: 26px;"
        "  padding: 4px 12px;"
        "  background-color: palette(window);"
        "}"
        "QDialogButtonBox QPushButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QDialogButtonBox QPushButton:pressed {"
        "  border-color: palette(shadow);"
        "  background-color: palette(mid);"
        "  padding-top: 5px;"
        "  padding-bottom: 3px;"
        "}"
    );

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    m_maxPlayers = new QSpinBox(this);
    m_maxPlayers->setObjectName("LobbyOptionsSpinPlayers");
    m_maxPlayers->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_maxPlayers->setRange(1, 16);
    form->addRow("Max players:", m_maxPlayers);

    m_maxPing = new QSpinBox(this);
    m_maxPing->setObjectName("LobbyOptionsSpinPing");
    m_maxPing->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    m_maxPing->setRange(1, 9999);
    form->addRow("Max ping:", m_maxPing);

    m_joinMsgHost = new QLineEdit(this);
    m_joinMsgHost->setPlaceholderText("Auto-chat when players join your game");
    form->addRow("Host join message:", m_joinMsgHost);

    m_joinMsgJoin = new QLineEdit(this);
    m_joinMsgJoin->setPlaceholderText("Auto-chat when you join someone else's game");
    form->addRow("Joiner message:", m_joinMsgJoin);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    loadSettings();
    adjustSize();
    setFixedHeight(sizeHint().height());
}

void KailleraOptionsDialog::loadSettings()
{
    int maxPlayers = CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPlayers);
    if (maxPlayers < 1 || maxPlayers > 16)
    {
        maxPlayers = 4;
    }

    m_maxPlayers->setValue(maxPlayers);
    m_maxPing->setValue(CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPing));
    m_joinMsgHost->setText(QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_JoinMessageHost)));
    m_joinMsgJoin->setText(QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_JoinMessageJoin)));
}

void KailleraOptionsDialog::saveSettings()
{
    CoreSettingsSetValue(SettingsID::Kaillera_MaxPlayers, m_maxPlayers->value());
    CoreSettingsSetValue(SettingsID::Kaillera_MaxPing, m_maxPing->value());
    CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageHost, m_joinMsgHost->text().toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageJoin, m_joinMsgJoin->text().toStdString());
    CoreSettingsSave();
}

#endif // NETPLAY
