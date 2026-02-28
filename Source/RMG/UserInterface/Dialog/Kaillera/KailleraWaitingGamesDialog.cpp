/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraWaitingGamesDialog.hpp"

#ifdef _WIN32

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>

static const char* kPlistUrl = "http://kaillerareborn.2manygames.fr:27887/plist.txt";

// Extract traversal code from emulator string.
// Input:  "FakeEmulator 1.0 {CC:ABC123}"
// Output: emulator = "FakeEmulator 1.0", code = "ABC123"
static bool extractAppCode(QString& emulator, QString& code)
{
    static const QString prefix = " {CC:";
    static const QString suffix = "}";

    int prefixIdx = emulator.indexOf(prefix);
    if (prefixIdx < 0) return false;

    int codeStart = prefixIdx + prefix.length();
    int suffixIdx = emulator.indexOf(suffix, codeStart);
    if (suffixIdx < 0) return false;

    code = emulator.mid(codeStart, suffixIdx - codeStart);
    emulator.truncate(prefixIdx);
    return true;
}

KailleraWaitingGamesDialog::KailleraWaitingGamesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setWindowTitle("waiting games...");
    setMinimumSize(640, 400);
    resize(700, 450);

    m_netManager = new QNetworkAccessManager(this);
    connect(m_netManager, &QNetworkAccessManager::finished,
            this, &KailleraWaitingGamesDialog::onFetchFinished);

    auto* mainLayout = new QVBoxLayout(this);

    // Table: Game, Emulator, User, Ping
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Game", "Emulator", "User", "Ping"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->resizeSection(0, 250);
    m_table->horizontalHeader()->resizeSection(1, 170);
    m_table->horizontalHeader()->resizeSection(2, 100);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &KailleraWaitingGamesDialog::onRowDoubleClicked);
    mainLayout->addWidget(m_table, 1);

    // Status label
    m_statusLabel = new QLabel("Downloading...", this);
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    m_btnConnect = new QPushButton("Connect", this);
    connect(m_btnConnect, &QPushButton::clicked, this, &KailleraWaitingGamesDialog::onConnect);
    btnLayout->addWidget(m_btnConnect);

    m_btnRefresh = new QPushButton("Refresh", this);
    connect(m_btnRefresh, &QPushButton::clicked, this, &KailleraWaitingGamesDialog::onRefresh);
    btnLayout->addWidget(m_btnRefresh);

    btnLayout->addStretch();

    m_btnClose = new QPushButton("Close", this);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_btnClose);
    mainLayout->addLayout(btnLayout);

    fetchList();
}

void KailleraWaitingGamesDialog::fetchList()
{
    m_table->setRowCount(0);
    m_statusLabel->setText("Downloading...");
    m_btnRefresh->setEnabled(false);

    QNetworkRequest request{QUrl(kPlistUrl)};
    m_netManager->get(request);
}

void KailleraWaitingGamesDialog::onFetchFinished(QNetworkReply* reply)
{
    m_btnRefresh->setEnabled(true);

    if (reply->error() != QNetworkReply::NoError)
    {
        m_statusLabel->setText("Error: " + reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.size() < 7)
    {
        m_statusLabel->setText("No games found.");
        return;
    }

    parseResponse(data);
}

void KailleraWaitingGamesDialog::parseResponse(const QByteArray& data)
{
    // QNetworkAccessManager strips HTTP headers, but handle raw responses too
    QByteArray body = data;
    int headerEnd = body.indexOf("\r\n\r\n");
    if (headerEnd >= 0)
        body = body.mid(headerEnd + 4);

    if (body.trimmed().isEmpty())
    {
        m_statusLabel->setText("No games found.");
        return;
    }

    // Disable sorting while populating
    m_table->setSortingEnabled(false);

    QList<QByteArray> lines = body.split('\n');
    int total = 0;

    for (const QByteArray& line : lines)
    {
        if (line.trimmed().isEmpty()) continue;

        // Format: Game|Emulator {CC:Code}|User|IP:Port|...
        QList<QByteArray> fields = line.split('|');
        if (fields.size() < 4) continue;

        QString gameName = QString::fromUtf8(fields[0].trimmed());
        QString emulator = QString::fromUtf8(fields[1].trimmed());
        QString userName = QString::fromUtf8(fields[2].trimmed());
        QString hostPort = QString::fromUtf8(fields[3].trimmed());

        // Extract traversal code from emulator string
        QString code;
        extractAppCode(emulator, code);

        // Only show games with a traversal code (P2P games)
        if (code.isEmpty()) continue;

        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* gameItem = new QTableWidgetItem(gameName);
        auto* emuItem = new QTableWidgetItem(emulator);
        auto* userItem = new QTableWidgetItem(userName);
        auto* pingItem = new QTableWidgetItem("-");

        // Store code and host in the game item's data roles
        gameItem->setData(Qt::UserRole, code);
        gameItem->setData(Qt::UserRole + 1, hostPort);

        m_table->setItem(row, 0, gameItem);
        m_table->setItem(row, 1, emuItem);
        m_table->setItem(row, 2, userItem);
        m_table->setItem(row, 3, pingItem);
        total++;
    }

    m_table->setSortingEnabled(true);
    m_statusLabel->setText(QString("%1 waiting games found").arg(total));
}

void KailleraWaitingGamesDialog::onConnect()
{
    int row = m_table->currentRow();
    if (row < 0) return;

    QTableWidgetItem* gameItem = m_table->item(row, 0);
    if (!gameItem) return;

    m_selectedCode = gameItem->data(Qt::UserRole).toString();
    m_selectedHost = gameItem->data(Qt::UserRole + 1).toString();
    accept();
}

void KailleraWaitingGamesDialog::onRefresh()
{
    fetchList();
}

void KailleraWaitingGamesDialog::onRowDoubleClicked(int row, int column)
{
    (void)column;
    m_table->setCurrentCell(row, 0);
    onConnect();
}

#endif // _WIN32
