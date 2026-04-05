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
#include <QLabel>
#include <QProcess>
#include <QProgressDialog>
#include <QElapsedTimer>

class KailleraPlaybackDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraPlaybackDialog(QWidget* parent = nullptr);
    ~KailleraPlaybackDialog() override;

private slots:
    void onPlaybackPlay();
    void onPlaybackStop();
    void onPlaybackPause();
    void onPlaybackStepForward();
    void onPlaybackDelete();
    void onPlaybackRefresh();
    void onPlaybackOpenFolder();
    void onPlaybackExport();
    void onPlaybackDoubleClicked(int row, int column);
    void onPlaybackTimer();
    void onExportProcessOutput();
    void onExportProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void setupUI();
    void populatePlaybackList();
    QString getSelectedRecordingPath() const;
    QString getSelectedRecordingGameName(QString* recordingPath = nullptr, int* totalFrames = nullptr) const;
    void resetExportUi();
    void startExportProcess(const QString& recordingPath, const QString& romPath, const QString& outputPath, int totalFrames);
    void processExportOutputText(const QString& text, bool finalizePartialLine = false);
    void processExportOutputLine(const QString& line);
    void updateExportProgressDialog();
    QString buildExportProgressSummary() const;

    QTableWidget* m_playbackTable = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnPause = nullptr;
    QPushButton* m_btnStepForward = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnPBDelete = nullptr;
    QPushButton* m_btnPBRefresh = nullptr;
    QPushButton* m_btnExport = nullptr;
    QPushButton* m_btnOpenFolder = nullptr;
    QLabel* m_frameLabel = nullptr;
    bool m_playbackWasActive = false;
    bool m_isPaused = false;
    bool m_exportCanceled = false;
    QString m_exportOutputPath;
    QString m_exportLog;
    QString m_exportPendingOutput;
    QString m_exportStatusLine;
    QString m_exportVideoEncoder;
    QString m_exportTargetSpeed;
    int m_exportCapturedFrames = 0;
    int m_exportTotalFrames = 0;
    QElapsedTimer m_exportElapsedTimer;

    QTimer* m_playbackTimer = nullptr;
    QProcess* m_exportProcess = nullptr;
    QProgressDialog* m_exportProgressDialog = nullptr;
};

#endif // _WIN32
#endif // KAILLERAPLAYBACKDIALOG_HPP
