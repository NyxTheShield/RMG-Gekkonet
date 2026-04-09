/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraPlaybackDialog.hpp"
#include "KailleraTableStyle.hpp"

#ifdef _WIN32

#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Emulation.hpp>

#include "n02_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>

#include <cstring>
#include <ctime>
#include <fstream>

static QString getKailleraRecordsDirectory()
{
    return QString::fromStdString(CoreGetKailleraRecordsDirectory());
}

KailleraPlaybackDialog::KailleraPlaybackDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setupUI();

    // Timer to detect playback ending naturally
    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &KailleraPlaybackDialog::onPlaybackTimer);
    m_playbackTimer->start(50);
}

KailleraPlaybackDialog::~KailleraPlaybackDialog()
{
    if (m_playbackTimer)
    {
        m_playbackTimer->stop();
    }
}

void KailleraPlaybackDialog::setupUI()
{
    setWindowTitle("Playback");
    setMinimumSize(520, 400);
    resize(580, 450);

    setStyleSheet("QTableWidget::item:selected { background-color: #0078D7; color: white; }");

    auto* mainLayout = new QVBoxLayout(this);

    // Recordings table
    m_playbackTable = new QTableWidget(0, 6, this);
    m_playbackTable->setHorizontalHeaderLabels({"Date", "Players", "Game", "Duration", "Size", "Filename"});
    m_playbackTable->horizontalHeader()->setStretchLastSection(true);
    m_playbackTable->verticalHeader()->setVisible(false);
    m_playbackTable->setShowGrid(false);
    m_playbackTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_playbackTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playbackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_playbackTable->setSortingEnabled(true);
    m_playbackTable->horizontalHeader()->setMinimumSectionSize(16);
    m_playbackTable->setColumnWidth(0, 100);
    m_playbackTable->setColumnWidth(1, 160);
    m_playbackTable->setColumnWidth(2, 140);
    m_playbackTable->setColumnWidth(3, 60);
    m_playbackTable->setColumnWidth(4, 60);
    applyNoAccentStyle(m_playbackTable);
    installHeaderDoubleClickSortToggle(m_playbackTable);
    connect(m_playbackTable, &QTableWidget::cellDoubleClicked, this, &KailleraPlaybackDialog::onPlaybackDoubleClicked);
    mainLayout->addWidget(m_playbackTable);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    m_btnPlay = new QPushButton("Play", this);
    m_btnPause = new QPushButton("Pause", this);
    m_btnStepForward = new QPushButton(">>", this);
    m_btnStop = new QPushButton("Stop", this);
    m_btnPBDelete = new QPushButton("Delete", this);
    m_btnPBRefresh = new QPushButton("Refresh", this);
    m_btnOpenFolder = new QPushButton("Open Folder", this);

    connect(m_btnPlay, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackPlay);
    connect(m_btnPause, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackPause);
    connect(m_btnStepForward, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackStepForward);
    connect(m_btnStop, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackStop);
    connect(m_btnPBDelete, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackDelete);
    connect(m_btnPBRefresh, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackRefresh);
    connect(m_btnOpenFolder, &QPushButton::clicked, this, &KailleraPlaybackDialog::onPlaybackOpenFolder);

    btnLayout->addWidget(m_btnPlay);
    btnLayout->addWidget(m_btnPause);
    btnLayout->addWidget(m_btnStepForward);
    btnLayout->addWidget(m_btnStop);
    btnLayout->addWidget(m_btnPBDelete);
    btnLayout->addWidget(m_btnPBRefresh);
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOpenFolder);
    mainLayout->addLayout(btnLayout);

    // Frame counter
    m_frameLabel = new QLabel("", this);
    mainLayout->addWidget(m_frameLabel);

    // Bottom close button
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    auto* btnClose = new QPushButton("Close", this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    bottomLayout->addWidget(btnClose);
    mainLayout->addLayout(bottomLayout);

    // Populate on creation
    populatePlaybackList();
}

void KailleraPlaybackDialog::onPlaybackTimer()
{
    // Drive the KSSDFA state machine so that KSSDFA_START_GAME (set by
    // player_play) transitions to state 1->2 and fires gameCallback,
    // which ultimately starts emulation.
    n02::processStateMachineStep();

    // Update frame counter display
    if (n02::isPlaybackActive() && m_frameLabel)
    {
        int cur = n02::playbackGetCurrentFrame();
        int total = n02::playbackGetTotalFrames();
        m_frameLabel->setText(QString("Frame: %1 / %2").arg(cur).arg(total));
    }
    else if (m_frameLabel)
    {
        m_frameLabel->setText("");
    }

    // Detect playback ending naturally (recording ran out).
    if (m_playbackWasActive && !n02::isPlaybackActive())
    {
        m_playbackWasActive = false;
        m_isPaused = false;
        if (m_btnPause) m_btnPause->setText("Pause");
        CoreMarkKailleraGameInactive();
        CoreStopEmulation();
    }
}

void KailleraPlaybackDialog::populatePlaybackList()
{
    if (!m_playbackTable) return;

    m_playbackTable->setSortingEnabled(false);
    m_playbackTable->setRowCount(0);

    const QString recordsPath = getKailleraRecordsDirectory();
    QDir recordsDir(recordsPath);
    if (!recordsDir.exists())
    {
        QDir().mkpath(recordsPath);
        m_playbackTable->setSortingEnabled(true);
        return;
    }

    QStringList filters;
    filters << "*.krec";
    QFileInfoList files = recordsDir.entryInfoList(filters, QDir::Files, QDir::Name | QDir::Reversed);

    for (const QFileInfo& fi : files)
    {
        std::string fullPath = fi.absoluteFilePath().toStdString();
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) continue;

        std::streamsize len = file.tellg();
        if (len < 272)
        {
            file.close();
            continue;
        }

        file.seekg(0, std::ios::beg);
        char* filebuf = (char*)malloc((size_t)len + 1);
        if (!filebuf) { file.close(); continue; }
        file.read(filebuf, len);
        file.close();

        char VER[5];
        memcpy(VER, filebuf, 4);
        VER[4] = 0;
        bool isKRC1 = (strcmp(VER, "KRC1") == 0);
        if (strcmp(VER, "KRC0") != 0 && !isKRC1)
        {
            free(filebuf);
            continue;
        }

        size_t headerSize = isKRC1 ? 400 : 272;
        if ((size_t)len < headerSize)
        {
            free(filebuf);
            continue;
        }

        // Parse game name (offset 132, 128 bytes)
        char gameName[129];
        memcpy(gameName, filebuf + 132, 128);
        gameName[128] = 0;

        // Parse timestamp, player number, numplayers (offset 260)
        int32_t timestamp = 0;
        memcpy(&timestamp, filebuf + 260, 4);
        int32_t recPlayerNo = 0;
        memcpy(&recPlayerNo, filebuf + 264, 4);
        int32_t recNumPlayers = 0;
        memcpy(&recNumPlayers, filebuf + 268, 4);

        // Column 0: Date
        QString dateStr;
        {
            bool parsed = false;
            QByteArray fn = fi.fileName().toUtf8();
            if (!isKRC1 && fn.size() > 13)
            {
                bool allDigits = true;
                for (int d = 0; d < 12; d++)
                {
                    if (!isdigit((unsigned char)fn[d])) { allDigits = false; break; }
                }
                if (allDigits)
                {
                    // YYMMDDHHMMSS format
                    dateStr = QString("%1/%2/%3 %4:%5")
                        .arg(QString::fromUtf8(fn.mid(0, 2)))
                        .arg(QString::fromUtf8(fn.mid(2, 2)))
                        .arg(QString::fromUtf8(fn.mid(4, 2)))
                        .arg(QString::fromUtf8(fn.mid(6, 2)))
                        .arg(QString::fromUtf8(fn.mid(8, 2)));
                    parsed = true;
                }
            }
            if (!parsed)
            {
                time_t t = (time_t)timestamp;
                tm* lt = localtime(&t);
                if (lt)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%02d/%02d/%02d %02d:%02d",
                             lt->tm_year % 100, lt->tm_mon + 1, lt->tm_mday,
                             lt->tm_hour, lt->tm_min);
                    dateStr = QString::fromUtf8(buf);
                }
                else
                {
                    dateStr = "?";
                }
            }
        }

        // Column 1: Players
        QString playersStr;
        {
            if (isKRC1)
            {
                QStringList names;
                for (int p = 0; p < recNumPlayers && p < 4; p++)
                {
                    char name[33];
                    memcpy(name, filebuf + 272 + p * 32, 32);
                    name[32] = 0;
                    if (name[0] != 0)
                        names.append(QString::fromUtf8(name));
                }
                playersStr = names.isEmpty() ? "?" : names.join(", ");
            }
            else
            {
                // KRC0: try to parse player names from filename
                QByteArray fn = fi.fileName().toUtf8();
                int nameStart = 0;
                if (fn.size() > 13)
                {
                    bool allDigits = true;
                    for (int d = 0; d < 12; d++)
                    {
                        if (!isdigit((unsigned char)fn[d])) { allDigits = false; break; }
                    }
                    if (allDigits && fn[12] == '-') nameStart = 13;
                }
                if (nameStart > 0)
                {
                    int extIdx = fn.indexOf(".krec");
                    if (extIdx < 0) extIdx = fn.size();
                    // Find last dash before extension — that separates players from game
                    int lastDash = -1;
                    for (int s = nameStart; s < extIdx; s++)
                    {
                        if (fn[s] == '-') lastDash = s;
                    }
                    if (lastDash > nameStart)
                    {
                        QByteArray playerPart = fn.mid(nameStart, lastDash - nameStart);
                        playersStr = QString::fromUtf8(playerPart).replace('-', ", ");
                    }
                    else
                    {
                        playersStr = "?";
                    }
                }
                else
                {
                    playersStr = "?";
                }
            }
        }

        // Column 3: Duration — count 0x12 records
        int frames = 0;
        {
            char* scan = filebuf + headerSize;
            char* scanEnd = filebuf + len;
            while (scan + 1 < scanEnd)
            {
                unsigned char type = (unsigned char)*scan++;
                if (type == 0x12)
                {
                    if (scan + 2 > scanEnd) break;
                    unsigned short rlen = *(unsigned short*)scan;
                    scan += 2;
                    if (rlen > 0)
                    {
                        if (scan + rlen > scanEnd) break;
                        scan += rlen;
                    }
                    frames++;
                }
                else if (type == 0x14)
                {
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                    scan += 4;
                }
                else if (type == 0x08)
                {
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                    while (scan < scanEnd && *scan != 0) scan++;
                    if (scan < scanEnd) scan++;
                }
                else
                {
                    break;
                }
            }
        }
        int totalSec = frames / 60;
        int mins = totalSec / 60;
        int secs = totalSec % 60;
        QString durationStr = QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));

        // Column 4: Size
        QString sizeStr;
        {
            qint64 sz = fi.size();
            if (sz <= 1024)
                sizeStr = QString::number(sz) + " B";
            else if (sz < 1024 * 1000)
                sizeStr = QString::number(sz / 1024) + " kB";
            else
                sizeStr = QString("%1.%2 MB").arg(sz / (1024 * 1000)).arg((sz % (1024 * 1000)) / (1024 * 100));
        }

        // Add row
        int row = m_playbackTable->rowCount();
        m_playbackTable->insertRow(row);
        m_playbackTable->setItem(row, 0, new QTableWidgetItem(dateStr));
        m_playbackTable->setItem(row, 1, new QTableWidgetItem(playersStr));
        m_playbackTable->setItem(row, 2, new QTableWidgetItem(QString::fromUtf8(gameName)));
        m_playbackTable->setItem(row, 3, new QTableWidgetItem(durationStr));
        m_playbackTable->setItem(row, 4, new QTableWidgetItem(sizeStr));
        m_playbackTable->setItem(row, 5, new QTableWidgetItem(fi.fileName()));

        free(filebuf);
    }

    m_playbackTable->setSortingEnabled(true);

    // Default sort: date descending (newest first)
    m_playbackTable->sortByColumn(0, Qt::DescendingOrder);
}

void KailleraPlaybackDialog::onPlaybackPlay()
{
    if (!m_playbackTable) return;
    if (n02::isPlaybackActive()) return;

    int row = m_playbackTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* fnItem = m_playbackTable->item(row, 5);
    if (!fnItem) return;

    QString filename = QDir(getKailleraRecordsDirectory()).filePath(fnItem->text());
    QByteArray pathBytes = filename.toUtf8();

    // Ensure playback mode is active
    n02::activateMode(2);

    if (n02::playbackLoad(pathBytes.constData()))
    {
        m_playbackWasActive = true;
    }
    else
    {
        QMessageBox::warning(this, "Playback", "Failed to load recording: " + fnItem->text());
    }
}

void KailleraPlaybackDialog::onPlaybackStop()
{
    // Resume first if paused, so emulation can shut down cleanly
    if (m_isPaused)
    {
        CoreResumeEmulation();
        m_isPaused = false;
        if (m_btnPause) m_btnPause->setText("Pause");
    }

    if (n02::isPlaybackActive())
    {
        n02::endGame();
    }
    m_playbackWasActive = false;
    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
}

void KailleraPlaybackDialog::onPlaybackPause()
{
    if (!n02::isPlaybackActive()) return;

    if (!m_isPaused)
    {
        if (CorePauseEmulation())
        {
            m_isPaused = true;
            if (m_btnPause) m_btnPause->setText("Resume");
        }
    }
    else
    {
        if (CoreResumeEmulation())
        {
            m_isPaused = false;
            if (m_btnPause) m_btnPause->setText("Pause");
        }
    }
}

void KailleraPlaybackDialog::onPlaybackStepForward()
{
    if (!n02::isPlaybackActive()) return;

    // Pause first if running
    if (!m_isPaused)
    {
        CorePauseEmulation();
        m_isPaused = true;
        if (m_btnPause) m_btnPause->setText("Resume");
        return;
    }

    // Use M64CMD_ADVANCE_FRAME for precise single-frame stepping,
    // then nudge with resume+pause to force the display to update.
    CoreAdvanceFrame();
    QTimer::singleShot(50, this, [this]() {
        CoreResumeEmulation();
        QTimer::singleShot(17, this, [this]() {
            CorePauseEmulation();
        });
    });
}

void KailleraPlaybackDialog::onPlaybackDelete()
{
    if (!m_playbackTable) return;

    int row = m_playbackTable->currentRow();
    if (row < 0) return;

    QTableWidgetItem* fnItem = m_playbackTable->item(row, 5);
    if (!fnItem) return;

    QString filename = fnItem->text();
    if (QMessageBox::question(this, "Delete Recording",
            "Delete \"" + filename + "\"?",
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    QString fullPath = QDir(getKailleraRecordsDirectory()).filePath(filename);
    QFile::remove(fullPath);
    populatePlaybackList();
}

void KailleraPlaybackDialog::onPlaybackRefresh()
{
    populatePlaybackList();
}

void KailleraPlaybackDialog::onPlaybackOpenFolder()
{
    const QString recordsPath = getKailleraRecordsDirectory();
    QDir().mkpath(recordsPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(recordsPath).absolutePath()));
}

void KailleraPlaybackDialog::onPlaybackDoubleClicked(int row, int column)
{
    (void)column;
    if (row >= 0)
    {
        onPlaybackPlay();
    }
}

#endif // _WIN32
