/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraServerBrowserDialog.hpp"

#ifdef _WIN32

#include "../../KailleraUIBridge.hpp"
#include "KailleraOptionsDialog.hpp"

#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Emulation.hpp>
#include <RMG-Core/Settings.hpp>

#include "n02_client.h"
#include "kailleraclient.h"
#include "kcore/kaillera_core.h"
#include "stats.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QScrollBar>
#include <QTime>
#include <QRegularExpression>
#include <QApplication>
#include <windows.h>

// QTableWidgetItem subclass that sorts numerically instead of alphabetically
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    explicit NumericTableWidgetItem(int value)
        : QTableWidgetItem(QString::number(value))
    {
        setData(Qt::UserRole, value);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
    }
};

// QTableWidgetItem subclass for game status that sorts Waiting < Playing < Idle
class StatusTableWidgetItem : public QTableWidgetItem
{
public:
    explicit StatusTableWidgetItem(const QString& text, int sortKey)
        : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, sortKey);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
    }
};

KailleraServerBrowserDialog::KailleraServerBrowserDialog(const QString& serverName, QWidget* parent)
    : QDialog(parent)
    , m_serverName(serverName)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setupUI();
    connectSignals();

    // Restore saved geometry and splitter state
    std::string geom = CoreSettingsGetStringValue(SettingsID::Kaillera_BrowserGeometry);
    if (!geom.empty())
    {
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(geom)));
    }
    std::string topSplit = CoreSettingsGetStringValue(SettingsID::Kaillera_BrowserTopSplitter);
    if (!topSplit.empty())
    {
        m_topSplitter->restoreState(QByteArray::fromBase64(QByteArray::fromStdString(topSplit)));
    }
    std::string botSplit = CoreSettingsGetStringValue(SettingsID::Kaillera_BrowserBottomSplitter);
    if (!botSplit.empty() && m_roomSplitter)
    {
        m_roomSplitter->restoreState(QByteArray::fromBase64(QByteArray::fromStdString(botSplit)));
    }

    // Restore saved column widths
    restoreColumnWidths();

    // Start stats timer (1-second interval for FPS/delay updates in game room)
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &KailleraServerBrowserDialog::onStatsTimer);
}

KailleraServerBrowserDialog::~KailleraServerBrowserDialog()
{
    if (m_statsTimer)
    {
        m_statsTimer->stop();
    }
}

void KailleraServerBrowserDialog::reject()
{
    // Save geometry and splitter state
    CoreSettingsSetValue(SettingsID::Kaillera_BrowserGeometry,
        saveGeometry().toBase64().toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_BrowserTopSplitter,
        m_topSplitter->saveState().toBase64().toStdString());
    if (m_roomSplitter)
    {
        CoreSettingsSetValue(SettingsID::Kaillera_BrowserBottomSplitter,
            m_roomSplitter->saveState().toBase64().toStdString());
    }
    saveColumnWidths();
    CoreSettingsSave();

    // Save join message before closing
    if (m_inGameRoom)
    {
        if (m_isHost)
        {
            CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageHost, m_joinMsgInput->text().toStdString());
        }
        else
        {
            CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageJoin, m_joinMsgInput->text().toStdString());
        }
        CoreSettingsSave();

        kaillera_leave_game();
    }
    kaillera_disconnect(nullptr);
    kaillera_core_cleanup();

    QDialog::reject();
}

void KailleraServerBrowserDialog::setupUI()
{
    setWindowTitle("Connected to " + m_serverName);
    setMinimumSize(700, 500);
    resize(800, 600);

    // Use a classic blue selection instead of the default purple
    setStyleSheet("QTableWidget::item:selected { background-color: #0078D7; color: white; }");

    auto* mainLayout = new QVBoxLayout(this);

    // === TOP SECTION (always visible) ===

    // Lobby chat (left) + User table (right)
    m_topSplitter = new QSplitter(Qt::Horizontal, this);

    m_lobbyChat = new QTextBrowser();
    m_lobbyChat->setReadOnly(true);
    m_lobbyChat->setOpenExternalLinks(true);
    m_topSplitter->addWidget(m_lobbyChat);

    // User list — styled as a selectable list
    m_userTable = new QTableWidget(0, 4);
    m_userTable->setHorizontalHeaderLabels({"Name", "Ping", "UID", "Status"});
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->verticalHeader()->setVisible(false);
    m_userTable->setShowGrid(false);
    m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userTable->setSortingEnabled(true);
    m_userTable->horizontalHeader()->setMinimumSectionSize(16);
    m_userTable->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_userTable->horizontalHeader(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        for (int i = 0; i < m_userTable->columnCount(); ++i) {
            QAction* act = menu.addAction(m_userTable->horizontalHeaderItem(i)->text());
            act->setCheckable(true);
            act->setChecked(!m_userTable->isColumnHidden(i));
            connect(act, &QAction::toggled, this, [this, i](bool visible) {
                m_userTable->setColumnHidden(i, !visible);
            });
        }
        menu.exec(m_userTable->horizontalHeader()->mapToGlobal(pos));
    });
    m_userTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_userTable, &QWidget::customContextMenuRequested,
            this, &KailleraServerBrowserDialog::onUserListContextMenu);
    m_topSplitter->addWidget(m_userTable);
    m_userTable->sortByColumn(1, Qt::AscendingOrder);

    m_topSplitter->setStretchFactor(0, 3);
    m_topSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(m_topSplitter, 3);

    // Chat input row: [input] [Chat] [Create/Swap]
    auto* chatLayout = new QHBoxLayout();
    m_lobbyChatInput = new QLineEdit();
    m_lobbyChatInput->setPlaceholderText("Type a message...");
    m_btnSendLobby = new QPushButton("Chat");
    m_btnCreateSwap = new QPushButton("Create");
    connect(m_btnSendLobby, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onSendLobbyChat);
    connect(m_lobbyChatInput, &QLineEdit::returnPressed, this, &KailleraServerBrowserDialog::onSendLobbyChat);
    connect(m_btnCreateSwap, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onCreateOrSwap);
    chatLayout->addWidget(m_lobbyChatInput);
    chatLayout->addWidget(m_btnSendLobby);
    chatLayout->addWidget(m_btnCreateSwap);
    mainLayout->addLayout(chatLayout);

    // === BOTTOM SECTION (swappable between game list and game room) ===

    m_bottomStack = new QStackedWidget(this);
    m_bottomStack->addWidget(createGameListWidget());   // index 0 = game table
    m_bottomStack->addWidget(createGameRoomWidget());   // index 1 = game room
    m_bottomStack->setCurrentIndex(0);
    mainLayout->addWidget(m_bottomStack, 2);

    // Build game list context menu for Create Game
    m_gameListMenu = new QMenu(this);
    buildGameListMenu();
}

QWidget* KailleraServerBrowserDialog::createGameListWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    m_gameTable = new QTableWidget(0, 6, widget);
    m_gameTable->setHorizontalHeaderLabels({"Game", "GameID", "Emulator", "User", "Status", "Users"});
    m_gameTable->horizontalHeader()->setStretchLastSection(true);
    m_gameTable->verticalHeader()->setVisible(false);
    m_gameTable->setShowGrid(false);
    m_gameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_gameTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_gameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_gameTable->setSortingEnabled(true);
    m_gameTable->horizontalHeader()->setMinimumSectionSize(16);
    m_gameTable->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_gameTable->horizontalHeader(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        for (int i = 0; i < m_gameTable->columnCount(); ++i) {
            QAction* act = menu.addAction(m_gameTable->horizontalHeaderItem(i)->text());
            act->setCheckable(true);
            act->setChecked(!m_gameTable->isColumnHidden(i));
            connect(act, &QAction::toggled, this, [this, i](bool visible) {
                m_gameTable->setColumnHidden(i, !visible);
            });
        }
        menu.exec(m_gameTable->horizontalHeader()->mapToGlobal(pos));
    });
    m_gameTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_gameTable, &QWidget::customContextMenuRequested,
            this, &KailleraServerBrowserDialog::onGameListContextMenu);
    // Double-click a game to join it
    connect(m_gameTable, &QTableWidget::cellDoubleClicked, this, [this]() { onJoinGame(); });
    layout->addWidget(m_gameTable);
    m_gameTable->sortByColumn(4, Qt::AscendingOrder);

    return widget;
}

QWidget* KailleraServerBrowserDialog::createGameRoomWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);

    // Main game room area: Game chat (left) + Player table (center) + Buttons (right)
    m_roomSplitter = new QSplitter(Qt::Horizontal, widget);
    auto* roomSplitter = m_roomSplitter;

    // Game chat (left)
    auto* chatWidget = new QWidget();
    auto* chatVBox = new QVBoxLayout(chatWidget);
    chatVBox->setContentsMargins(0, 0, 0, 0);

    m_gameChat = new QTextBrowser(chatWidget);
    m_gameChat->setReadOnly(true);
    m_gameChat->setOpenExternalLinks(true);
    chatVBox->addWidget(m_gameChat);

    // Game chat input + send
    auto* gameChatRow = new QHBoxLayout();
    m_gameChatInput = new QLineEdit(chatWidget);
    m_gameChatInput->setPlaceholderText("Type a message...");
    m_btnSendGame = new QPushButton("Chat", chatWidget);
    connect(m_btnSendGame, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onSendGameChat);
    connect(m_gameChatInput, &QLineEdit::returnPressed, this, &KailleraServerBrowserDialog::onSendGameChat);
    gameChatRow->addWidget(m_gameChatInput);
    gameChatRow->addWidget(m_btnSendGame);
    chatVBox->addLayout(gameChatRow);

    roomSplitter->addWidget(chatWidget);

    // Player table (center)
    m_playerTable = new QTableWidget(0, 3, widget);
    m_playerTable->setHorizontalHeaderLabels({"Nick", "Ping", "Delay"});
    m_playerTable->horizontalHeader()->setStretchLastSection(true);
    m_playerTable->verticalHeader()->setVisible(false);
    m_playerTable->setShowGrid(false);
    m_playerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_playerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_playerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_playerTable->setSortingEnabled(true);
    m_playerTable->horizontalHeader()->setMinimumSectionSize(16);
    m_playerTable->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playerTable->horizontalHeader(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QMenu menu(this);
        for (int i = 0; i < m_playerTable->columnCount(); ++i) {
            QAction* act = menu.addAction(m_playerTable->horizontalHeaderItem(i)->text());
            act->setCheckable(true);
            act->setChecked(!m_playerTable->isColumnHidden(i));
            connect(act, &QAction::toggled, this, [this, i](bool visible) {
                m_playerTable->setColumnHidden(i, !visible);
            });
        }
        menu.exec(m_playerTable->horizontalHeader()->mapToGlobal(pos));
    });
    roomSplitter->addWidget(m_playerTable);

    // Buttons + stats (right)
    auto* rightWidget = new QWidget();
    auto* rightVBox = new QVBoxLayout(rightWidget);
    rightVBox->setContentsMargins(0, 0, 0, 0);

    m_btnStart = new QPushButton("Start", rightWidget);
    rightVBox->addWidget(m_btnStart);

    auto* dropLeaveRow = new QHBoxLayout();
    m_btnDrop = new QPushButton("Drop", rightWidget);
    m_btnLeave = new QPushButton("Leave", rightWidget);
    dropLeaveRow->addWidget(m_btnDrop);
    dropLeaveRow->addWidget(m_btnLeave);
    rightVBox->addLayout(dropLeaveRow);

    auto* kickLagRow = new QHBoxLayout();
    m_btnKick = new QPushButton("Kick", rightWidget);
    m_btnLagStat = new QPushButton("Lagstat", rightWidget);
    kickLagRow->addWidget(m_btnKick);
    kickLagRow->addWidget(m_btnLagStat);
    rightVBox->addLayout(kickLagRow);

    auto* optAdvRow = new QHBoxLayout();
    m_btnOptions = new QPushButton("Options", rightWidget);
    m_btnAdvertise = new QPushButton("Advertise", rightWidget);
    optAdvRow->addWidget(m_btnOptions);
    optAdvRow->addWidget(m_btnAdvertise);
    rightVBox->addLayout(optAdvRow);

    m_recordCheck = new QCheckBox("Record game", rightWidget);
    rightVBox->addWidget(m_recordCheck);

    m_fpsLabel = new QLabel("speed: 0fps", rightWidget);
    m_delayLabel = new QLabel("0 frames", rightWidget);
    rightVBox->addWidget(m_fpsLabel);
    rightVBox->addWidget(m_delayLabel);

    m_joinMsgLabel = new QLabel("Join msg:", rightWidget);
    m_joinMsgInput = new QLineEdit(rightWidget);
    m_joinMsgInput->setPlaceholderText("Auto-send on join...");
    rightVBox->addWidget(m_joinMsgLabel);
    rightVBox->addWidget(m_joinMsgInput);

    rightVBox->addStretch();

    connect(m_btnStart, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onStartGame);
    connect(m_btnDrop, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onDropGame);
    connect(m_btnLeave, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onLeaveGame);
    connect(m_btnKick, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onKickPlayer);
    connect(m_btnLagStat, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onLagStat);
    connect(m_btnOptions, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onOptions);
    connect(m_btnAdvertise, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onAdvertise);

    roomSplitter->addWidget(rightWidget);

    roomSplitter->setStretchFactor(0, 3);  // game chat
    roomSplitter->setStretchFactor(1, 2);  // player table
    roomSplitter->setStretchFactor(2, 0);  // buttons (fixed width)

    layout->addWidget(roomSplitter);

    return widget;
}

void KailleraServerBrowserDialog::connectSignals()
{
    auto& bridge = KailleraUIBridge::instance();

    // Lobby signals
    connect(&bridge, &KailleraUIBridge::kailleraUserAdded,
            this, &KailleraServerBrowserDialog::onUserAdded);
    connect(&bridge, &KailleraUIBridge::kailleraUserJoined,
            this, &KailleraServerBrowserDialog::onUserJoined);
    connect(&bridge, &KailleraUIBridge::kailleraUserLeft,
            this, &KailleraServerBrowserDialog::onUserLeft);
    connect(&bridge, &KailleraUIBridge::kailleraGameAdded,
            this, &KailleraServerBrowserDialog::onGameAdded);
    connect(&bridge, &KailleraUIBridge::kailleraGameCreated,
            this, &KailleraServerBrowserDialog::onGameCreated);
    connect(&bridge, &KailleraUIBridge::kailleraGameClosed,
            this, &KailleraServerBrowserDialog::onGameClosed);
    connect(&bridge, &KailleraUIBridge::kailleraGameStatusChanged,
            this, &KailleraServerBrowserDialog::onGameStatusChanged);
    connect(&bridge, &KailleraUIBridge::kailleraChatReceived,
            this, &KailleraServerBrowserDialog::onChatReceived);
    connect(&bridge, &KailleraUIBridge::kailleraMotdReceived,
            this, &KailleraServerBrowserDialog::onMotdReceived);
    connect(&bridge, &KailleraUIBridge::kailleraLoginStatus,
            this, &KailleraServerBrowserDialog::onLoginStatus);
    connect(&bridge, &KailleraUIBridge::kailleraErrorMessage,
            this, &KailleraServerBrowserDialog::onError);

    // Debug messages shown in lobby chat
    connect(&bridge, &KailleraUIBridge::kailleraDebugMessage,
            this, [this](QString message) {
        m_lobbyChat->append(timestamp() + message.toHtmlEscaped());
    });

    // Game room signals
    connect(&bridge, &KailleraUIBridge::kailleraUserGameCreated,
            this, &KailleraServerBrowserDialog::onUserGameCreated);
    connect(&bridge, &KailleraUIBridge::kailleraUserGameJoined,
            this, &KailleraServerBrowserDialog::onUserGameJoined);
    connect(&bridge, &KailleraUIBridge::kailleraUserGameClosed,
            this, &KailleraServerBrowserDialog::onUserGameClosed);
    connect(&bridge, &KailleraUIBridge::kailleraPlayerAdded,
            this, &KailleraServerBrowserDialog::onPlayerAdded);
    connect(&bridge, &KailleraUIBridge::kailleraPlayerJoined,
            this, &KailleraServerBrowserDialog::onPlayerJoined);
    connect(&bridge, &KailleraUIBridge::kailleraPlayerLeft,
            this, &KailleraServerBrowserDialog::onPlayerLeft);
    connect(&bridge, &KailleraUIBridge::kailleraGameChatReceived,
            this, &KailleraServerBrowserDialog::onGameChatReceived);
    connect(&bridge, &KailleraUIBridge::kailleraUserKicked,
            this, &KailleraServerBrowserDialog::onUserKicked);
    connect(&bridge, &KailleraUIBridge::kailleraGameStarted,
            this, &KailleraServerBrowserDialog::onGameStarted);
    connect(&bridge, &KailleraUIBridge::kailleraPlayerDropped,
            this, &KailleraServerBrowserDialog::onPlayerDropped);
    connect(&bridge, &KailleraUIBridge::kailleraGameEnded,
            this, &KailleraServerBrowserDialog::onGameEnded);
    connect(&bridge, &KailleraUIBridge::kailleraNetsyncWait,
            this, &KailleraServerBrowserDialog::onNetsyncWait);

    // Wire Record checkbox to recording flag
    n02_kaillera_recording_enabled = CoreSettingsGetBoolValue(SettingsID::Kaillera_RecordingEnabled);
    m_recordCheck->setChecked(n02_kaillera_recording_enabled);
    connect(m_recordCheck, &QCheckBox::toggled, this, [](bool checked) {
        n02_kaillera_recording_enabled = checked;
        CoreSettingsSetValue(SettingsID::Kaillera_RecordingEnabled, checked);
    });
}

void KailleraServerBrowserDialog::switchToLobby()
{
    // Save join message for current context before leaving
    if (m_isHost)
    {
        CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageHost, m_joinMsgInput->text().toStdString());
    }
    else
    {
        CoreSettingsSetValue(SettingsID::Kaillera_JoinMessageJoin, m_joinMsgInput->text().toStdString());
    }
    CoreSettingsSave();

    m_inGameRoom = false;
    m_isHost = false;
    m_currentGameName.clear();
    m_statsTimer->stop();

    // Clear player table
    m_playerTable->setRowCount(0);
    m_gameChat->clear();

    // Switch bottom to game list and relabel button
    showBottomGameList();
    m_btnCreateSwap->setText("Create");

    updateTitle();
}

void KailleraServerBrowserDialog::switchToGameRoom()
{
    m_inGameRoom = true;

    // Clear game room state
    m_playerTable->setRowCount(0);
    m_gameChat->clear();
    m_lastFrameCount = kaillera_get_frames_count();
    m_lastPPS = SOCK_SEND_PACKETS;

    // Start stats updates
    m_statsTimer->start(1000);

    // Detect non-game lobbies — Start should be disabled
    bool isNonGameLobby = (m_currentGameName == "*Away (leave messages)" ||
                           m_currentGameName == "*Chat (not game)");

    // Update button visibility based on host status
    m_btnStart->setEnabled(m_isHost && !isNonGameLobby);
    m_btnKick->setEnabled(m_isHost);

    // Load the appropriate join message for this context
    if (m_isHost)
    {
        m_joinMsgInput->setText(QString::fromStdString(
            CoreSettingsGetStringValue(SettingsID::Kaillera_JoinMessageHost)));
    }
    else
    {
        m_joinMsgInput->setText(QString::fromStdString(
            CoreSettingsGetStringValue(SettingsID::Kaillera_JoinMessageJoin)));
    }

    // Switch bottom to game room and relabel button
    showBottomGameRoom();
    m_btnCreateSwap->setText("Swap");

    setWindowTitle("Game Room - " + m_serverName);
}

void KailleraServerBrowserDialog::showBottomGameList()
{
    m_bottomStack->setCurrentIndex(0);
}

void KailleraServerBrowserDialog::showBottomGameRoom()
{
    m_bottomStack->setCurrentIndex(1);
}

void KailleraServerBrowserDialog::executeOptions()
{
    if (!m_isHost)
        return;

    int maxPlayers = CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPlayers);
    int maxPing = CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPing);

    if (maxPlayers > 0)
    {
        QByteArray cmd = QString("/maxusers %1").arg(maxPlayers).toUtf8();
        kaillera_game_chat_send(cmd.data());
    }
    if (maxPing > 0)
    {
        QByteArray cmd = QString("/maxping %1").arg(maxPing).toUtf8();
        kaillera_game_chat_send(cmd.data());
    }
}

void KailleraServerBrowserDialog::buildGameListMenu()
{
    m_gameListMenu->clear();

    // Add special entries (match original n02 names)
    m_gameListMenu->addAction("*Chat (not game)", this, [this]() {
        m_currentGameName = "*Chat (not game)";
        QByteArray name("*Chat (not game)");
        kaillera_create_game(name.data());
    });
    m_gameListMenu->addAction("*Away (leave messages)", this, [this]() {
        m_currentGameName = "*Away (leave messages)";
        QByteArray name("*Away (leave messages)");
        kaillera_create_game(name.data());
    });
    m_gameListMenu->addSeparator();

    populateGameSubmenus(m_gameListMenu);
}

void KailleraServerBrowserDialog::populateGameSubmenus(QMenu* parentMenu)
{
    if (!infos.gameList)
        return;

    // Collect all game names
    std::vector<QString> gameNames;
    const char* p = infos.gameList;
    while (*p)
    {
        gameNames.push_back(QString::fromUtf8(p));
        p += strlen(p) + 1;
    }

    // Group by first character: # for digits/symbols, A-Z for letters
    QMap<QChar, QStringList> groups;
    for (const auto& name : gameNames)
    {
        if (name.isEmpty())
            continue;
        QChar first = name.at(0).toUpper();
        QChar key = first.isLetter() ? first : QChar('#');
        groups[key].append(name);
    }

    // Add # submenu first if it exists
    if (groups.contains(QChar('#')))
    {
        QMenu* sub = parentMenu->addMenu("#");
        for (const auto& gameName : groups[QChar('#')])
        {
            sub->addAction(gameName, this, [this, gameName]() {
                m_currentGameName = gameName;
                QByteArray nameBytes = gameName.toUtf8();
                kaillera_create_game(nameBytes.data());
            });
        }
    }

    // Add A-Z submenus
    for (char c = 'A'; c <= 'Z'; ++c)
    {
        QChar key(c);
        if (!groups.contains(key))
            continue;
        QMenu* sub = parentMenu->addMenu(QString(key));
        for (const auto& gameName : groups[key])
        {
            sub->addAction(gameName, this, [this, gameName]() {
                m_currentGameName = gameName;
                QByteArray nameBytes = gameName.toUtf8();
                kaillera_create_game(nameBytes.data());
            });
        }
    }
}

void KailleraServerBrowserDialog::updateTitle()
{
    int users = m_userTable->rowCount();
    int games = m_gameTable->rowCount();
    setWindowTitle(QString("Connected to %1 (%2 users & %3 games)")
        .arg(m_serverName)
        .arg(users)
        .arg(games));
}

QString KailleraServerBrowserDialog::timestamp()
{
    return "[" + QTime::currentTime().toString("h:mm AP") + "] ";
}

QString KailleraServerBrowserDialog::linkify(const QString& text)
{
    QString html = text.toHtmlEscaped();
    // Turn URLs into clickable links
    static QRegularExpression urlRe("(https?://\\S+)");
    html.replace(urlRe, "<a href='\\1'>\\1</a>");
    return html;
}

QString KailleraServerBrowserDialog::connString(char conn)
{
    switch (conn)
    {
        case 1: return "LAN";
        case 2: return "Excellent";
        case 3: return "Good";
        case 4: return "Average";
        case 5: return "Low";
        case 6: return "Bad";
        default: return "Unknown";
    }
}

QString KailleraServerBrowserDialog::userStatusString(char status)
{
    switch (status)
    {
        case 0: return "Playing";
        case 1: return "Idle";
        case 2: return "Playing";
        default: return "Unknown";
    }
}

QString KailleraServerBrowserDialog::gameStatusString(char status)
{
    switch (status)
    {
        case 0: return "Waiting";
        case 1: return "Playing";
        case 2: return "Playing";
        default: return "Unknown";
    }
}

int KailleraServerBrowserDialog::findRowByValue(QTableWidget* table, int column, unsigned int value)
{
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QTableWidgetItem* item = table->item(row, column);
        if (item && item->data(Qt::UserRole).toUInt() == value)
        {
            return row;
        }
    }
    return -1;
}

int KailleraServerBrowserDialog::findRowByText(QTableWidget* table, int column, const QString& text)
{
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QTableWidgetItem* item = table->item(row, column);
        if (item && item->text() == text)
        {
            return row;
        }
    }
    return -1;
}

void KailleraServerBrowserDialog::updateUserStatus(const QString& username, const QString& status, int sortKey)
{
    int row = findRowByText(m_userTable, 0, username);
    if (row >= 0)
    {
        m_userTable->setItem(row, 3, new StatusTableWidgetItem(status, sortKey));
    }
}

void KailleraServerBrowserDialog::saveColumnWidths()
{
    // Save user table column widths as comma-separated string
    QStringList userWidths;
    for (int i = 0; i < m_userTable->columnCount(); ++i)
        userWidths << QString::number(m_userTable->columnWidth(i));
    CoreSettingsSetValue(SettingsID::Kaillera_UserColumnWidths, userWidths.join(",").toStdString());

    // Save game table column widths
    QStringList gameWidths;
    for (int i = 0; i < m_gameTable->columnCount(); ++i)
        gameWidths << QString::number(m_gameTable->columnWidth(i));
    CoreSettingsSetValue(SettingsID::Kaillera_GameColumnWidths, gameWidths.join(",").toStdString());

    // Save player table column widths
    QStringList playerWidths;
    for (int i = 0; i < m_playerTable->columnCount(); ++i)
        playerWidths << QString::number(m_playerTable->columnWidth(i));
    CoreSettingsSetValue(SettingsID::Kaillera_PlayerColumnWidths, playerWidths.join(",").toStdString());
}

void KailleraServerBrowserDialog::restoreColumnWidths()
{
    auto restoreTable = [](QTableWidget* table, const std::string& saved) {
        if (saved.empty()) return;
        QStringList widths = QString::fromStdString(saved).split(",");
        for (int i = 0; i < widths.size() && i < table->columnCount(); ++i)
        {
            int w = widths[i].toInt();
            if (w > 0)
                table->setColumnWidth(i, w);
        }
    };

    restoreTable(m_userTable, CoreSettingsGetStringValue(SettingsID::Kaillera_UserColumnWidths));
    restoreTable(m_gameTable, CoreSettingsGetStringValue(SettingsID::Kaillera_GameColumnWidths));
    restoreTable(m_playerTable, CoreSettingsGetStringValue(SettingsID::Kaillera_PlayerColumnWidths));
}

// ---- Lobby mode handlers ----

void KailleraServerBrowserDialog::onUserAdded(QString name, int ping, int status, unsigned short id, char conn)
{
    (void)conn;
    m_userTable->setSortingEnabled(false);
    int row = m_userTable->rowCount();
    m_userTable->insertRow(row);
    m_userTable->setItem(row, 0, new QTableWidgetItem(name));
    m_userTable->setItem(row, 1, new NumericTableWidgetItem(ping));
    m_userTable->setItem(row, 2, new NumericTableWidgetItem(id));
    m_userTable->setItem(row, 3, new StatusTableWidgetItem(userStatusString(static_cast<char>(status)), status));
    m_userTable->setSortingEnabled(true);
    updateTitle();
}

void KailleraServerBrowserDialog::onUserJoined(QString name, int ping, unsigned short id, char conn)
{
    (void)conn;
    m_userTable->setSortingEnabled(false);
    int row = m_userTable->rowCount();
    m_userTable->insertRow(row);
    m_userTable->setItem(row, 0, new QTableWidgetItem(name));
    m_userTable->setItem(row, 1, new NumericTableWidgetItem(ping));
    m_userTable->setItem(row, 2, new NumericTableWidgetItem(id));
    m_userTable->setItem(row, 3, new StatusTableWidgetItem("Idle", 2));
    m_userTable->setSortingEnabled(true);

    m_lobbyChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "* Joins: " + name.toHtmlEscaped()
        + " (Ping: " + QString::number(ping) + ")</span>");
    updateTitle();
}

void KailleraServerBrowserDialog::onUserLeft(QString name, QString quitMsg, unsigned short id)
{
    int row = findRowByValue(m_userTable, 2, id);
    if (row >= 0)
    {
        m_userTable->removeRow(row);
    }

    QString msg = "* Parts: " + name.toHtmlEscaped();
    if (!quitMsg.isEmpty())
    {
        msg += " (" + quitMsg.toHtmlEscaped() + ")";
    }
    m_lobbyChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + msg + "</span>");
    updateTitle();
}

void KailleraServerBrowserDialog::onGameAdded(QString gameName, unsigned int id, QString emulator, QString owner, QString users, char status)
{
    m_gameTable->setSortingEnabled(false);
    int row = m_gameTable->rowCount();
    m_gameTable->insertRow(row);
    m_gameTable->setItem(row, 0, new QTableWidgetItem(gameName));
    m_gameTable->setItem(row, 1, new NumericTableWidgetItem(static_cast<int>(id)));
    m_gameTable->setItem(row, 2, new QTableWidgetItem(emulator));
    m_gameTable->setItem(row, 3, new QTableWidgetItem(owner));
    m_gameTable->setItem(row, 4, new StatusTableWidgetItem(gameStatusString(status), status));
    m_gameTable->setItem(row, 5, new QTableWidgetItem(users));
    m_gameTable->setSortingEnabled(true);
    updateTitle();
}

void KailleraServerBrowserDialog::onGameCreated(QString gameName, unsigned int id, QString emulator, QString owner)
{
    // Deduplicate — Kaillera UDP retransmissions can deliver GAMEMAKE twice
    if (findRowByValue(m_gameTable, 1, id) >= 0)
        return;

    m_gameTable->setSortingEnabled(false);
    int row = m_gameTable->rowCount();
    m_gameTable->insertRow(row);
    m_gameTable->setItem(row, 0, new QTableWidgetItem(gameName));
    m_gameTable->setItem(row, 1, new NumericTableWidgetItem(static_cast<int>(id)));
    m_gameTable->setItem(row, 2, new QTableWidgetItem(emulator));
    m_gameTable->setItem(row, 3, new QTableWidgetItem(owner));
    m_gameTable->setItem(row, 4, new StatusTableWidgetItem("Waiting", 0));
    m_gameTable->setItem(row, 5, new QTableWidgetItem("1/?"));
    m_gameTable->setSortingEnabled(true);

    // Update owner's user status to Waiting (game hasn't started yet)
    updateUserStatus(owner, "Waiting", 0);
    updateTitle();
}

void KailleraServerBrowserDialog::onGameClosed(unsigned int id)
{
    int row = findRowByValue(m_gameTable, 1, id);
    if (row >= 0)
    {
        // Set the game owner's user status back to Idle before removing the row
        QTableWidgetItem* ownerItem = m_gameTable->item(row, 3);
        if (ownerItem)
            updateUserStatus(ownerItem->text(), "Idle", 1);

        m_gameTable->removeRow(row);
    }

    // Set all game room players back to Idle
    for (int i = 0; i < m_playerTable->rowCount(); ++i)
    {
        QTableWidgetItem* nameItem = m_playerTable->item(i, 0);
        if (nameItem)
            updateUserStatus(nameItem->text(), "Idle", 1);
    }

    updateTitle();
}

void KailleraServerBrowserDialog::onGameStatusChanged(unsigned int id, char status, int players, int maxPlayers)
{
    int row = findRowByValue(m_gameTable, 1, id);
    if (row >= 0)
    {
        m_gameTable->setItem(row, 4, new StatusTableWidgetItem(gameStatusString(status), status));
        m_gameTable->setItem(row, 5, new QTableWidgetItem(
            QString::number(players) + "/" + QString::number(maxPlayers)));

        QString gameStatus = gameStatusString(status);

        // Update the game owner's user status to match
        QTableWidgetItem* ownerItem = m_gameTable->item(row, 3);
        if (ownerItem)
            updateUserStatus(ownerItem->text(), gameStatus, status);

        // Update all players in the game room (if we're in this game)
        for (int i = 0; i < m_playerTable->rowCount(); ++i)
        {
            QTableWidgetItem* nameItem = m_playerTable->item(i, 0);
            if (nameItem)
                updateUserStatus(nameItem->text(), gameStatus, status);
        }
    }
}

void KailleraServerBrowserDialog::onChatReceived(QString name, QString message)
{
    // Detect private messages: "TO: <user>(id): msg" or "<user>(id): msg"
    bool isPM = message.startsWith("TO: <") ||
                (message.startsWith("<") && message.contains("): "));
    if (isPM)
    {
        m_lobbyChat->append("<span style='color:lime;'>" + timestamp()
            + "&lt;" + name.toHtmlEscaped() + "&gt; " + linkify(message) + "</span>");
    }
    else
    {
        m_lobbyChat->append(timestamp() + "&lt;" + name.toHtmlEscaped() + "&gt; " + linkify(message));
    }
}

void KailleraServerBrowserDialog::onMotdReceived(QString name, QString message)
{
    (void)name;
    m_lobbyChat->append("<span style='color:green;'>" + timestamp() + "- " + linkify(message) + "</span>");
}

void KailleraServerBrowserDialog::onLoginStatus(QString message)
{
    m_lobbyChat->append("<span style='color:red;'>" + timestamp() + message.toHtmlEscaped() + "</span>");
}

void KailleraServerBrowserDialog::onError(QString message)
{
    m_lobbyChat->append("<span style='color:red;'>" + timestamp() + "Error: " + message.toHtmlEscaped() + "</span>");
}

// ---- Game room handlers ----

void KailleraServerBrowserDialog::onUserGameCreated()
{
    m_isHost = true;
    switchToGameRoom();
    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "You created a game. Waiting for players...</span>");
    executeOptions();
}

void KailleraServerBrowserDialog::onUserGameJoined()
{
    m_isHost = false;
    switchToGameRoom();
    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "You joined the game. Waiting for host to start...</span>");
}

void KailleraServerBrowserDialog::onUserGameClosed()
{
    m_gameChat->append("<span style='color:red;'>" + timestamp() + "Game has been closed.</span>");
    switchToLobby();
}

void KailleraServerBrowserDialog::onPlayerAdded(QString name, int ping, unsigned short id, char conn)
{
    m_playerTable->setSortingEnabled(false);
    int row = m_playerTable->rowCount();
    m_playerTable->insertRow(row);
    auto* nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, static_cast<unsigned int>(id));
    m_playerTable->setItem(row, 0, nameItem);
    m_playerTable->setItem(row, 1, new NumericTableWidgetItem(ping));
    // Delay calculation: (ping * 60 / 1000 / conn) + 2, then thrp * conn - 1 frames
    int c = (conn > 0) ? conn : 1;
    int thrp = (ping * 60 / 1000 / c) + 2;
    int delay = thrp * c - 1;
    m_playerTable->setItem(row, 2, new NumericTableWidgetItem(delay));
    m_playerTable->setSortingEnabled(true);
}

void KailleraServerBrowserDialog::onPlayerJoined(QString name, int ping, unsigned short uid, char conn)
{
    m_playerTable->setSortingEnabled(false);
    int row = m_playerTable->rowCount();
    m_playerTable->insertRow(row);
    auto* nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, static_cast<unsigned int>(uid));
    m_playerTable->setItem(row, 0, nameItem);
    m_playerTable->setItem(row, 1, new NumericTableWidgetItem(ping));
    int c = (conn > 0) ? conn : 1;
    int thrp = (ping * 60 / 1000 / c) + 2;
    int delay = thrp * c - 1;
    m_playerTable->setItem(row, 2, new NumericTableWidgetItem(delay));
    m_playerTable->setSortingEnabled(true);

    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "* " + name.toHtmlEscaped() + " joined the game</span>");

    // Update this user's status in the lobby user table
    updateUserStatus(name, "Waiting", 0);

    // Auto-kick players who join while game is already running (host only)
    if (m_isHost && kaillera_is_game_running())
    {
        kaillera_kick_user(uid);
        return;
    }

    // Auto-send join message when hosting
    if (m_isHost)
    {
        QString joinMsg = m_joinMsgInput->text().trimmed();
        if (!joinMsg.isEmpty())
        {
            QByteArray msgBytes = joinMsg.toUtf8();
            kaillera_game_chat_send(msgBytes.data());
        }
    }

    // Beep on player join
    if (CoreSettingsGetBoolValue(SettingsID::Kaillera_BeepOnJoin))
    {
        MessageBeep(MB_OK);
    }

    // Flash taskbar if dialog not focused
    if (CoreSettingsGetBoolValue(SettingsID::Kaillera_FlashOnJoin) && !isActiveWindow())
    {
        FLASHWINFO fwi = {};
        fwi.cbSize = sizeof(fwi);
        fwi.hwnd = reinterpret_cast<HWND>(winId());
        fwi.dwFlags = FLASHW_TIMERNOFG | FLASHW_TRAY;
        fwi.uCount = 0;
        fwi.dwTimeout = 0;
        FlashWindowEx(&fwi);
    }
}

void KailleraServerBrowserDialog::onPlayerLeft(QString name, unsigned short id)
{
    int row = findRowByValue(m_playerTable, 0, id);
    if (row >= 0)
    {
        m_playerTable->removeRow(row);
    }

    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "* " + name.toHtmlEscaped() + " left the game</span>");

    // Update this user's status in the lobby user table
    updateUserStatus(name, "Idle", 1);
}

void KailleraServerBrowserDialog::onGameChatReceived(QString name, QString message)
{
    m_gameChat->append(timestamp() + "&lt;" + name.toHtmlEscaped() + "&gt; " + linkify(message));
}

void KailleraServerBrowserDialog::onUserKicked()
{
    QMessageBox::warning(this, "Kicked", "You have been kicked from the game.");
    switchToLobby();
}

void KailleraServerBrowserDialog::onPlayerDropped(QString name, int player)
{
    m_gameChat->append("<span style='color:orange;'>" + timestamp()
        + "* " + name.toHtmlEscaped() + " (player " + QString::number(player) + ") has dropped</span>");

    // Host (player 1) dropping forces everyone else to drop too.
    if (player == 1 && player != playerno)
    {
        kaillera_game_drop();
    }
}

void KailleraServerBrowserDialog::onGameStarted(QString game, int player, int numPlayers)
{
    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp()
        + "* Starting: " + game.toHtmlEscaped()
        + " (" + QString::number(player) + "/" + QString::number(numPlayers) + ")</span>");
    m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp()
        + "- press \"Drop\" if emulator fails</span>");

    if (n02_kaillera_recording_enabled)
    {
        m_gameChat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp()
            + "- your game will be recorded</span>");
    }

    // Populate recording_player_names from current player table
    memset(recording_player_names, 0, sizeof(recording_player_names));
    for (int i = 0; i < m_playerTable->rowCount() && i < 4; i++)
    {
        QTableWidgetItem* item = m_playerTable->item(i, 0);
        if (item)
        {
            QByteArray nameBytes = item->text().toUtf8();
            strncpy(recording_player_names[i], nameBytes.constData(), 31);
            recording_player_names[i][31] = '\0';
        }
    }
}

void KailleraServerBrowserDialog::onGameEnded()
{
    // Guard: kaillera_end_game_callback() can fire multiple times
    // (once from kaillera_game_drop(), again from server echo GAMRDROP,
    // and possibly again from GAMESHUT). Only act on the first.
    if (!CoreHasInitKaillera())
        return;

    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
}

void KailleraServerBrowserDialog::onNetsyncWait(int tx)
{
    m_fpsLabel->setText("waiting for others");
    int secs = tx / 1000;
    int tenths = (tx % 1000) / 100;
    m_delayLabel->setText(QString("%1.%2s").arg(secs, 3, 10, QChar('0')).arg(tenths));
}

// ---- Context menus ----

void KailleraServerBrowserDialog::onUserListContextMenu(const QPoint& pos)
{
    int row = m_userTable->rowAt(pos.y());
    if (row < 0) return;

    m_userTable->selectRow(row);

    // Get user ID from UID column and name from row
    unsigned short userId = 0;
    QTableWidgetItem* uidItem = m_userTable->item(row, 2);
    if (uidItem)
    {
        userId = static_cast<unsigned short>(uidItem->data(Qt::UserRole).toUInt());
    }
    QString username = m_userTable->item(row, 0)->text();

    QMenu menu(this);
    QAction* sendMsg = menu.addAction("Send Message");
    QAction* findUser = menu.addAction("Find User");
    menu.addSeparator();
    QAction* ignoreUser = menu.addAction("Ignore");
    QAction* unignoreUser = menu.addAction("Unignore");

    QAction* chosen = menu.exec(m_userTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == sendMsg)
    {
        // Fill chat input with /msg <UserID> and focus it
        m_lobbyChatInput->setText(QString("/msg %1 ").arg(userId));
        m_lobbyChatInput->setFocus();
    }
    else if (chosen == findUser)
    {
        QByteArray cmd = QString("/finduser %1").arg(username).toUtf8();
        kaillera_chat_send(cmd.data());
    }
    else if (chosen == ignoreUser)
    {
        QByteArray cmd = QString("/ignore %1").arg(userId).toUtf8();
        kaillera_chat_send(cmd.data());
    }
    else if (chosen == unignoreUser)
    {
        QByteArray cmd = QString("/unignore %1").arg(userId).toUtf8();
        kaillera_chat_send(cmd.data());
    }
}

void KailleraServerBrowserDialog::onGameListContextMenu(const QPoint& pos)
{
    int row = m_gameTable->rowAt(pos.y());
    bool hasSelection = (row >= 0);

    if (hasSelection)
    {
        m_gameTable->selectRow(row);
    }

    QMenu menu(this);

    // "Create" submenu with game list
    QMenu* createSub = menu.addMenu("Create");

    createSub->addAction("*Chat (not game)", this, [this]() {
        m_currentGameName = "*Chat (not game)";
        QByteArray name("*Chat (not game)");
        kaillera_create_game(name.data());
    });
    createSub->addAction("*Away (leave messages)", this, [this]() {
        m_currentGameName = "*Away (leave messages)";
        QByteArray name("*Away (leave messages)");
        kaillera_create_game(name.data());
    });
    createSub->addSeparator();

    populateGameSubmenus(createSub);

    // "Join" option only if a game row is selected
    if (hasSelection)
    {
        QAction* joinAction = menu.addAction("Join");
        connect(joinAction, &QAction::triggered, this, &KailleraServerBrowserDialog::onJoinGame);
    }

    menu.exec(m_gameTable->viewport()->mapToGlobal(pos));
}

// ---- Button actions ----

void KailleraServerBrowserDialog::onCreateOrSwap()
{
    if (!m_inGameRoom)
    {
        // Normal mode: show game list menu to create a game
        if (m_gameListMenu->actions().isEmpty())
        {
            buildGameListMenu();
        }
        m_gameListMenu->popup(m_btnCreateSwap->mapToGlobal(QPoint(0, m_btnCreateSwap->height())));
    }
    else
    {
        // Game room mode: toggle bottom between game list and game room
        if (m_bottomStack->currentIndex() == 1)
        {
            showBottomGameList();
        }
        else
        {
            showBottomGameRoom();
        }
    }
}

void KailleraServerBrowserDialog::onJoinGame()
{
    int row = m_gameTable->currentRow();
    if (row < 0)
    {
        QMessageBox::information(this, "Join Game", "Select a game from the list to join.");
        return;
    }

    // Check game status — block joining running games
    QTableWidgetItem* statusItem = m_gameTable->item(row, 4);
    if (statusItem && statusItem->text() != "Waiting")
    {
        QMessageBox::warning(this, "Join Game", "Joining a running game is not allowed.");
        return;
    }

    // Check if the ROM is in our local game list
    QString gameName = m_gameTable->item(row, 0) ? m_gameTable->item(row, 0)->text() : "";
    if (!gameName.startsWith("*") && infos.gameList)
    {
        bool found = false;
        const char* p = infos.gameList;
        while (*p)
        {
            if (gameName == QString::fromUtf8(p))
            {
                found = true;
                break;
            }
            p += strlen(p) + 1;
        }
        if (!found)
        {
            QMessageBox::warning(this, "Join Game",
                QString("The ROM '%1' is not in your list.").arg(gameName));
            return;
        }
    }

    // Check emulator version mismatch
    QTableWidgetItem* emuItem = m_gameTable->item(row, 2);
    if (emuItem)
    {
        QString remoteEmu = emuItem->text();
        QString localEmu = QString::fromUtf8(APP);
        if (remoteEmu != localEmu)
        {
            int ret = QMessageBox::warning(this, "Version Mismatch",
                "Emulator/version mismatch and the game may desync.\nDo you want to continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
            {
                return;
            }
        }
    }

    // Get game ID from GameID column
    unsigned int gameId = 0;
    QTableWidgetItem* gameIdItem = m_gameTable->item(row, 1);
    if (gameIdItem)
    {
        gameId = gameIdItem->data(Qt::UserRole).toUInt();
    }

    if (gameId > 0)
    {
        // Track the game name for advertise
        m_currentGameName = gameName;
        QByteArray gameBytes = gameName.toUtf8();
        kaillera_join_game(gameId, gameBytes.constData());
    }
}

void KailleraServerBrowserDialog::onStartGame()
{
    if (m_isHost)
    {
        kaillera_start_game();
    }
}

void KailleraServerBrowserDialog::onDropGame()
{
    // Matches n02-rmg: just call kaillera_game_drop().
    // The callback chain handles the rest:
    //   kaillera_game_drop() -> kaillera_end_game_callback() ->
    //   UIBridge kailleraGameEnded -> onGameEnded() -> CoreStopEmulation()
    kaillera_game_drop();
}

void KailleraServerBrowserDialog::onLeaveGame()
{
    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
    kaillera_leave_game();
    switchToLobby();
}

void KailleraServerBrowserDialog::onKickPlayer()
{
    if (!m_isHost) return;

    int row = m_playerTable->currentRow();
    if (row < 0)
    {
        QMessageBox::information(this, "Kick Player", "Select a player to kick.");
        return;
    }

    // Prevent kicking self (row 0 is always the host)
    if (row == 0)
    {
        return;
    }

    // Get player ID from the Name column's UserRole data
    unsigned short playerId = 0;
    QTableWidgetItem* nameItem = m_playerTable->item(row, 0);
    if (nameItem)
    {
        playerId = static_cast<unsigned short>(nameItem->data(Qt::UserRole).toUInt());
    }

    if (playerId > 0)
    {
        kaillera_kick_user(playerId);
    }
}

void KailleraServerBrowserDialog::onLagStat()
{
    QByteArray cmd("/lagstat");
    kaillera_game_chat_send(cmd.data());
}

void KailleraServerBrowserDialog::onOptions()
{
    KailleraOptionsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        executeOptions();
    }
}

void KailleraServerBrowserDialog::onAdvertise()
{
    if (!m_inGameRoom || m_currentGameName.isEmpty())
        return;

    int playerCount = m_playerTable->rowCount();
    QString ad;

    if (m_isHost)
    {
        ad = QString("%1 - %2 player(s)").arg(m_currentGameName).arg(playerCount);
    }
    else
    {
        // Get host name (first player in list, row 0)
        QString hostName;
        if (m_playerTable->rowCount() > 0 && m_playerTable->item(0, 0))
        {
            hostName = m_playerTable->item(0, 0)->text();
        }
        ad = QString("<%1> | %2 - %3 player(s)").arg(hostName, m_currentGameName).arg(playerCount);
    }

    QByteArray adBytes = ad.toUtf8();
    kaillera_chat_send(adBytes.data());
}

void KailleraServerBrowserDialog::onSendLobbyChat()
{
    QString text = m_lobbyChatInput->text().trimmed();
    if (text.isEmpty()) return;

    // Handle client-side commands
    if (text.startsWith("/"))
    {
        if (text == "/pcs")
        {
            m_lobbyChat->append("<span style='color:gray;'>============= Core status begin ===============</span>");
            kaillera_print_core_status();
            unsigned int sta = KSSDFA.state;
            unsigned int inp = KSSDFA.input;
            m_lobbyChat->append(QString("<span style='color:gray;'>KSSDFA { state: %1, input: %2 }</span>").arg(sta).arg(inp));
            m_lobbyChat->append(QString("<span style='color:gray;'>PACKETLOSSCOUNT=%1</span>").arg(PACKETLOSSCOUNT));
            m_lobbyChat->append(QString("<span style='color:gray;'>PACKETMISOTDERCOUNT=%1</span>").arg(PACKETMISOTDERCOUNT));
            m_lobbyChat->append("<span style='color:gray;'>============ Core status end =================</span>");
            m_lobbyChatInput->clear();
            return;
        }
        else if (text == "/swm")
        {
            // Toggle bottom between game list and game room view
            if (m_inGameRoom)
            {
                if (m_bottomStack->currentIndex() == 1)
                    showBottomGameList();
                else
                    showBottomGameRoom();
            }
            m_lobbyChatInput->clear();
            return;
        }
        else if (text == "/clear")
        {
            m_lobbyChat->clear();
            m_lobbyChatInput->clear();
            return;
        }
        else if (text == "/stats")
        {
            m_lobbyChat->append("<span style='color:gray;'>--- Network Statistics ---</span>");
            m_lobbyChat->append(QString("<span style='color:gray;'>Recv: %1 packets, %2 bytes, %3 retransmits</span>")
                .arg(SOCK_RECV_PACKETS).arg(SOCK_RECV_BYTES).arg(SOCK_RECV_RETR));
            m_lobbyChat->append(QString("<span style='color:gray;'>Send: %1 packets, %2 bytes, %3 retransmits</span>")
                .arg(SOCK_SEND_PACKETS).arg(SOCK_SEND_BYTES).arg(SOCK_SEND_RETR));
            m_lobbyChat->append(QString("<span style='color:gray;'>Packet loss: %1, Misordered: %2</span>")
                .arg(PACKETLOSSCOUNT).arg(PACKETMISOTDERCOUNT));
            m_lobbyChatInput->clear();
            return;
        }
    }

    QByteArray textBytes = text.toUtf8();
    kaillera_chat_send(textBytes.data());
    m_lobbyChatInput->clear();
}

void KailleraServerBrowserDialog::onSendGameChat()
{
    QString text = m_gameChatInput->text().trimmed();
    if (text.isEmpty()) return;

    // Handle /halfdelay locally (toggle 30fps mode)
    if (text == "/halfdelay")
    {
        kaillera_30fps_mode = !kaillera_30fps_mode;
        m_gameChat->append(QString("<span style='color:gray;'>Half delay mode %1 (for 30fps games: Mario Kart, 1080, THPS)</span>")
            .arg(kaillera_30fps_mode ? "ENABLED" : "DISABLED"));
        m_gameChatInput->clear();
        return;
    }

    // Split long messages at 127 chars (Kaillera protocol limit)
    QByteArray textBytes = text.toUtf8();
    while (textBytes.size() > 0)
    {
        int len = qMin(textBytes.size(), 127);
        QByteArray chunk = textBytes.left(len);
        chunk.append('\0');
        kaillera_game_chat_send(chunk.data());
        textBytes = textBytes.mid(len);
    }
    m_gameChatInput->clear();
}

// ---- Stats timer ----

void KailleraServerBrowserDialog::onStatsTimer()
{
    if (!m_inGameRoom) return;

    int currentFrames = kaillera_get_frames_count();
    int fps = currentFrames - m_lastFrameCount;
    m_lastFrameCount = currentFrames;

    int currentPPS = SOCK_SEND_PACKETS;
    int pps = currentPPS - m_lastPPS;
    m_lastPPS = currentPPS;

    m_fpsLabel->setText(QString("%1 fps/%2 pps").arg(fps).arg(pps));

    int delay = kaillera_get_delay();
    m_delayLabel->setText(QString::number(delay) + " frames");
}

#endif // _WIN32
