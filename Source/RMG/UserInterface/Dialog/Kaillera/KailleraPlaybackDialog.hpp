/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERAPLAYBACKDIALOG_HPP
#define KAILLERAPLAYBACKDIALOG_HPP

#ifdef _WIN32

#include <QDialog>
#include <QTimer>
#include <QTableWidget>
#include <QPushButton>

class KailleraPlaybackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraPlaybackDialog(QWidget* parent = nullptr);
    ~KailleraPlaybackDialog() override;

private slots:
    void onPlaybackPlay();
    void onPlaybackStop();
    void onPlaybackDelete();
    void onPlaybackRefresh();
    void onPlaybackOpenFolder();
    void onPlaybackDoubleClicked(int row, int column);
    void onPlaybackTimer();

private:
    void setupUI();
    void populatePlaybackList();

    QTableWidget* m_playbackTable = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnPBDelete = nullptr;
    QPushButton* m_btnPBRefresh = nullptr;
    QPushButton* m_btnOpenFolder = nullptr;
    bool m_playbackWasActive = false;

    QTimer* m_playbackTimer = nullptr;
};

#endif // _WIN32
#endif // KAILLERAPLAYBACKDIALOG_HPP
