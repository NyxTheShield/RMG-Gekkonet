/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERAOPTIONSDIALOG_HPP
#define KAILLERAOPTIONSDIALOG_HPP

#ifdef _WIN32

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>

class KailleraOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraOptionsDialog(QWidget* parent = nullptr);

private:
    void loadSettings();
    void saveSettings();

    QSpinBox* m_maxPlayers = nullptr;
    QSpinBox* m_maxPing = nullptr;
    QCheckBox* m_flashOnJoin = nullptr;
    QCheckBox* m_beepOnJoin = nullptr;
};

#endif // _WIN32
#endif // KAILLERAOPTIONSDIALOG_HPP
