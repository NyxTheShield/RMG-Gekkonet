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
#include <QColor>
#include <QIcon>
#include <QProxyStyle>
#include <QStyle>
#include <windows.h>

namespace
{
QString infoMessageColor()
{
    return (QApplication::palette().window().color().value() < 128) ? "cornflowerblue" : "darkblue";
}

QIcon themedLineIcon(const QString& iconName)
{
    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    const QString iconPath = QString(":/icons/%1/svg/%2.svg")
        .arg(darkTheme ? "white" : "black", iconName);
    return QIcon(iconPath);
}

QIcon whiteLineIcon(const QString& iconName)
{
    return QIcon(QString(":/icons/white/svg/%1.svg").arg(iconName));
}

QColor blendColors(const QColor& fg, const QColor& bg, int fgWeight)
{
    const int clampedWeight = (fgWeight < 0) ? 0 : ((fgWeight > 100) ? 100 : fgWeight);
    const int bgWeight = 100 - clampedWeight;
    return QColor(
        (fg.red() * clampedWeight + bg.red() * bgWeight) / 100,
        (fg.green() * clampedWeight + bg.green() * bgWeight) / 100,
        (fg.blue() * clampedWeight + bg.blue() * bgWeight) / 100);
}

class ThinSplitterStyle final : public QProxyStyle
{
public:
    explicit ThinSplitterStyle()
        : QProxyStyle(nullptr)
    {
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        if (metric == QStyle::PM_SplitterWidth)
        {
            return 1;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
}

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
    if (m_isClosing)
    {
        return;
    }
    m_isClosing = true;

    if (m_statsTimer)
    {
        m_statsTimer->stop();
    }

    // Prevent any further lobby/game callbacks from targeting this dialog
    // while the network core is being torn down.
    QObject::disconnect(&KailleraUIBridge::instance(), nullptr, this, nullptr);

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

    if (m_inGameRoom)
    {
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

    // Use a classic blue selection instead of the default purple and style modern chat composers.
    setStyleSheet(
        "QTableWidget::item:selected { background-color: #0078D7; color: white; }"
        "QWidget#KailleraPane {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "}"
        "QWidget#KailleraPaneLeftJoined {"
        "  border: 1px solid palette(mid);"
        "  border-right: none;"
        "  border-top-left-radius: 10px;"
        "  border-bottom-left-radius: 10px;"
        "  border-top-right-radius: 0px;"
        "  border-bottom-right-radius: 0px;"
        "  background-color: palette(base);"
        "}"
        "QWidget#KailleraPaneRightJoined {"
        "  border: 1px solid palette(mid);"
        "  border-left: none;"
        "  border-top-left-radius: 0px;"
        "  border-bottom-left-radius: 0px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom-right-radius: 10px;"
        "  background-color: palette(base);"
        "}"
        "QWidget#KailleraPaneHeader {"
        "  border: none;"
        "  border-bottom: 1px solid palette(mid);"
        "  min-height: 30px;"
        "  background-color: transparent;"
        "}"
        "QSplitter#KailleraJoinedSplitter::handle {"
        "  background-color: palette(mid);"
        "  margin: 0px;"
        "  padding: 0px;"
        "}"
        "QSplitter#KailleraJoinedSplitter::handle:horizontal {"
        "  width: 1px;"
        "  border: none;"
        "}"
        "QLabel#KailleraPaneTitle {"
        "  font-weight: 400;"
        "  font-size: 13px;"
        "  letter-spacing: 0.6px;"
        "}"
        "QLabel#KailleraPaneMeta {"
        "  color: palette(mid);"
        "  font-weight: 600;"
        "}"
        "QTextBrowser#KailleraSurface, QTableWidget#KailleraSurface {"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QTableWidget#KailleraSurface {"
        "  gridline-color: transparent;"
        "  alternate-background-color: palette(alternate-base);"
        "}"
        "QHeaderView::section {"
        "  background-color: palette(window);"
        "  border: none;"
        "  border-bottom: 1px solid palette(mid);"
        "  border-left: 1px solid palette(mid);"
        "  padding: 6px 8px;"
        "  font-weight: 600;"
        "}"
        "QHeaderView::section:first {"
        "  border-left: none;"
        "}"
        "QWidget#KailleraChatComposer {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "}"
        "QLineEdit#KailleraChatComposerInput {"
        "  border: none;"
        "  background: transparent;"
        "  padding: 1px 0px;"
        "  min-height: 16px;"
        "}"
        "QLineEdit#KailleraChatComposerInput:focus {"
        "  border: none;"
        "}"
        "QPushButton#KailleraChatComposerSendButton {"
        "  border: none;"
        "  border-radius: 10px;"
        "  min-width: 20px;"
        "  min-height: 20px;"
        "  max-width: 20px;"
        "  max-height: 20px;"
        "  padding: 0px;"
        "  background-color: transparent;"
        "  color: palette(text);"
        "  font-size: 15px;"
        "  font-weight: 900;"
        "}"
        "QPushButton#KailleraChatComposerSendButton:hover {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraChatComposerSendButton:pressed {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(midlight);"
        "}"
        "QWidget#KailleraSidebar {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "}"
        "QPushButton#KailleraPrimaryButton {"
        "  border: 1px solid #0066b4;"
        "  border-radius: 8px;"
        "  min-height: 24px;"
        "  padding: 4px 12px;"
        "  color: white;"
        "  background-color: #0078D7;"
        "}"
        "QPushButton#KailleraPrimaryButton:hover {"
        "  background-color: #1c88dc;"
        "}"
        "QPushButton#KailleraPrimaryButton:pressed {"
        "  background-color: #005a9e;"
        "}"
        "QPushButton#KailleraSecondaryButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 8px;"
        "  min-height: 24px;"
        "  padding: 4px 10px;"
        "  background-color: palette(window);"
        "}"
        "QPushButton#KailleraSecondaryButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraSecondaryButton:pressed {"
        "  background-color: palette(midlight);"
        "}"
        "QPushButton#KailleraTinyButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  min-height: 20px;"
        "  max-height: 20px;"
        "  min-width: 102px;"
        "  max-width: 102px;"
        "  padding: 0px 6px;"
        "  background-color: palette(window);"
        "  font-weight: 600;"
        "}"
        "QPushButton#KailleraTinyButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraTinyButton:pressed {"
        "  background-color: palette(midlight);"
        "}"
        "QPushButton#KailleraStartButton {"
        "  border: 1px solid #005a9e;"
        "  border-radius: 9px;"
        "  min-height: 34px;"
        "  padding: 6px 14px;"
        "  color: white;"
        "  background-color: #0078D7;"
        "  font-size: 15px;"
        "  font-weight: 700;"
        "}"
        "QPushButton#KailleraStartButton:hover {"
        "  background-color: #1c88dc;"
        "}"
        "QPushButton#KailleraStartButton:pressed {"
        "  background-color: #005a9e;"
        "}"
        "QLabel#KailleraStatValue {"
        "  font-weight: 600;"
        "}"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // === TOP SECTION (always visible) ===

    // Lobby chat (left) + User table (right)
    m_topSplitter = new QSplitter(Qt::Horizontal, this);
    m_topSplitter->setObjectName("KailleraJoinedSplitter");
    m_topSplitter->setHandleWidth(1);
    auto* topSplitterStyle = new ThinSplitterStyle();
    m_topSplitter->setStyle(topSplitterStyle);

    auto* lobbyPane = new QWidget(this);
    lobbyPane->setObjectName("KailleraPaneLeftJoined");
    auto* lobbyPaneLayout = new QVBoxLayout(lobbyPane);
    lobbyPaneLayout->setContentsMargins(0, 0, 0, 0);
    lobbyPaneLayout->setSpacing(0);

    auto* lobbyHeader = new QWidget(lobbyPane);
    lobbyHeader->setObjectName("KailleraPaneHeader");
    auto* lobbyHeaderLayout = new QHBoxLayout(lobbyHeader);
    lobbyHeaderLayout->setContentsMargins(10, 4, 10, 4);
    lobbyHeaderLayout->setSpacing(6);
    auto* lobbyHeaderIcon = new QLabel(lobbyHeader);
    lobbyHeaderIcon->setPixmap(themedLineIcon("server-line").pixmap(14, 14));
    auto* lobbyHeaderTitle = new QLabel("SERVER CHAT", lobbyHeader);
    lobbyHeaderTitle->setObjectName("KailleraPaneTitle");
    lobbyHeaderLayout->addWidget(lobbyHeaderIcon);
    lobbyHeaderLayout->addWidget(lobbyHeaderTitle);
    lobbyHeaderLayout->addStretch();
    m_btnCreateSwap = new QPushButton("Create", lobbyHeader);
    m_btnCreateSwap->setObjectName("KailleraPrimaryButton");
    m_btnCreateSwap->setToolTip("Create a game");
    m_btnCreateSwap->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_btnCreateSwap->setMinimumWidth(86);
    connect(m_btnCreateSwap, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onCreateOrSwap);
    lobbyHeaderLayout->addWidget(m_btnCreateSwap);

    auto* lobbyBody = new QWidget(lobbyPane);
    auto* lobbyBodyLayout = new QVBoxLayout(lobbyBody);
    lobbyBodyLayout->setContentsMargins(8, 8, 8, 8);
    lobbyBodyLayout->setSpacing(8);

    m_lobbyChat = new QTextBrowser(lobbyBody);
    m_lobbyChat->setObjectName("KailleraSurface");
    m_lobbyChat->setReadOnly(true);
    m_lobbyChat->setOpenExternalLinks(true);
    lobbyBodyLayout->addWidget(m_lobbyChat);

    auto* lobbyComposer = new QWidget(lobbyBody);
    lobbyComposer->setObjectName("KailleraChatComposer");
    lobbyComposer->setFixedHeight(24);
    auto* lobbyComposerLayout = new QHBoxLayout(lobbyComposer);
    lobbyComposerLayout->setContentsMargins(10, 2, 4, 2);
    lobbyComposerLayout->setSpacing(4);

    m_lobbyChatInput = new QLineEdit(lobbyComposer);
    m_lobbyChatInput->setPlaceholderText("Type a message...");
    m_lobbyChatInput->setClearButtonEnabled(true);
    m_lobbyChatInput->setObjectName("KailleraChatComposerInput");
    m_lobbyChatInput->setFrame(false);

    m_btnSendLobby = new QPushButton(lobbyComposer);
    m_btnSendLobby->setObjectName("KailleraChatComposerSendButton");
    m_btnSendLobby->setToolTip("Send lobby chat message");
    m_btnSendLobby->setText("");
    m_btnSendLobby->setIcon(themedLineIcon("play-line"));
    m_btnSendLobby->setIconSize(QSize(13, 13));
    connect(m_btnSendLobby, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onSendLobbyChat);
    connect(m_lobbyChatInput, &QLineEdit::returnPressed, this, &KailleraServerBrowserDialog::onSendLobbyChat);

    lobbyComposerLayout->addWidget(m_lobbyChatInput);
    lobbyComposerLayout->addWidget(m_btnSendLobby, 0, Qt::AlignVCenter);
    lobbyBodyLayout->addWidget(lobbyComposer);

    lobbyPaneLayout->addWidget(lobbyHeader);
    lobbyPaneLayout->addWidget(lobbyBody, 1);
    m_topSplitter->addWidget(lobbyPane);

    // User list — styled as a selectable list
    auto* usersPane = new QWidget(this);
    usersPane->setObjectName("KailleraPaneRightJoined");
    auto* usersPaneLayout = new QVBoxLayout(usersPane);
    usersPaneLayout->setContentsMargins(0, 0, 0, 0);
    usersPaneLayout->setSpacing(0);

    auto* usersHeader = new QWidget(usersPane);
    usersHeader->setObjectName("KailleraPaneHeader");
    auto* usersHeaderLayout = new QHBoxLayout(usersHeader);
    usersHeaderLayout->setContentsMargins(10, 4, 10, 4);
    usersHeaderLayout->setSpacing(6);
    auto* usersHeaderIcon = new QLabel(usersHeader);
    usersHeaderIcon->setPixmap(themedLineIcon("list-check").pixmap(14, 14));
    auto* usersHeaderTitle = new QLabel("PLAYERS", usersHeader);
    usersHeaderTitle->setObjectName("KailleraPaneTitle");
    usersHeaderLayout->addWidget(usersHeaderIcon);
    usersHeaderLayout->addWidget(usersHeaderTitle);
    usersHeaderLayout->addStretch();
    m_connectedPlayersCountLabel = new QLabel("(0)", usersHeader);
    m_connectedPlayersCountLabel->setObjectName("KailleraPaneMeta");
    usersHeaderLayout->addWidget(m_connectedPlayersCountLabel);

    auto* usersBody = new QWidget(usersPane);
    auto* usersBodyLayout = new QVBoxLayout(usersBody);
    usersBodyLayout->setContentsMargins(0, 0, 1, 1);
    usersBodyLayout->setSpacing(0);

    m_userTable = new QTableWidget(0, 4, usersBody);
    m_userTable->setObjectName("KailleraSurface");
    m_userTable->setHorizontalHeaderLabels({"Name", "Ping", "UID", "Status"});
    m_userTable->horizontalHeader()->setStretchLastSection(true);
    m_userTable->verticalHeader()->setVisible(false);
    m_userTable->setShowGrid(false);
    m_userTable->setAlternatingRowColors(true);
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
    usersBodyLayout->addWidget(m_userTable);
    usersPaneLayout->addWidget(usersHeader);
    usersPaneLayout->addWidget(usersBody, 1);
    m_topSplitter->addWidget(usersPane);
    m_userTable->sortByColumn(1, Qt::AscendingOrder);

    m_topSplitter->setStretchFactor(0, 3);
    m_topSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(m_topSplitter, 3);

    // === BOTTOM SECTION (swappable between game list and game room) ===

    m_bottomStack = new QStackedWidget(this);
    m_bottomStack->addWidget(createGameListWidget());   // index 0 = game table
    m_bottomStack->addWidget(createGameRoomWidget());   // index 1 = game room
    m_bottomStack->setCurrentIndex(0);
    mainLayout->addWidget(m_bottomStack, 2);

    // Build game list context menu for Create Game
    m_gameListMenu = new QMenu(this);
    buildGameListMenu();
    updateHeaderCounts();
}

QWidget* KailleraServerBrowserDialog::createGameListWidget()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_gameTable = new QTableWidget(0, 6, widget);
    m_gameTable->setObjectName("KailleraSurface");
    m_gameTable->setHorizontalHeaderLabels({"Game", "GameID", "Emulator", "User", "Status", "Users"});
    m_gameTable->horizontalHeader()->setStretchLastSection(true);
    m_gameTable->verticalHeader()->setVisible(false);
    m_gameTable->setShowGrid(false);
    m_gameTable->setAlternatingRowColors(true);
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
    layout->setSpacing(8);

    // Main game room area: Game chat (left) + Player table (center) + Buttons (right)
    m_roomSplitter = new QSplitter(Qt::Horizontal, widget);
    m_roomSplitter->setObjectName("KailleraJoinedSplitter");
    m_roomSplitter->setHandleWidth(1);
    auto* roomSplitterStyle = new ThinSplitterStyle();
    m_roomSplitter->setStyle(roomSplitterStyle);
    auto* roomSplitter = m_roomSplitter;

    // Game chat (left)
    auto* chatPane = new QWidget(widget);
    chatPane->setObjectName("KailleraPaneLeftJoined");
    auto* chatPaneLayout = new QVBoxLayout(chatPane);
    chatPaneLayout->setContentsMargins(0, 0, 0, 0);
    chatPaneLayout->setSpacing(0);

    auto* chatHeader = new QWidget(chatPane);
    chatHeader->setObjectName("KailleraPaneHeader");
    auto* chatHeaderLayout = new QHBoxLayout(chatHeader);
    chatHeaderLayout->setContentsMargins(10, 4, 10, 4);
    chatHeaderLayout->setSpacing(6);
    auto* chatHeaderIcon = new QLabel(chatHeader);
    chatHeaderIcon->setPixmap(themedLineIcon("file-list-line").pixmap(14, 14));
    auto* chatHeaderTitle = new QLabel("LOBBY CHAT", chatHeader);
    chatHeaderTitle->setObjectName("KailleraPaneTitle");
    chatHeaderLayout->addWidget(chatHeaderIcon);
    chatHeaderLayout->addWidget(chatHeaderTitle);
    chatHeaderLayout->addStretch();
    m_btnSwapChat = new QPushButton("Show lobbies", chatHeader);
    m_btnSwapChat->setObjectName("KailleraTinyButton");
    m_btnSwapChat->setIcon(themedLineIcon("swap-line"));
    m_btnSwapChat->setToolTip("Show open lobbies");
    connect(m_btnSwapChat, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onCreateOrSwap);
    chatHeaderLayout->addWidget(m_btnSwapChat);

    m_roomChatStack = new QStackedWidget(chatPane);

    auto* gameChatPage = new QWidget(chatPane);
    auto* chatVBox = new QVBoxLayout(gameChatPage);
    chatVBox->setContentsMargins(8, 8, 8, 8);
    chatVBox->setSpacing(8);

    m_gameChat = new QTextBrowser(gameChatPage);
    m_gameChat->setObjectName("KailleraSurface");
    m_gameChat->setReadOnly(true);
    m_gameChat->setOpenExternalLinks(true);
    chatVBox->addWidget(m_gameChat);

    // Game chat input + send
    auto* gameChatRow = new QHBoxLayout();
    gameChatRow->setContentsMargins(0, 0, 0, 0);

    auto* gameComposer = new QWidget(gameChatPage);
    gameComposer->setObjectName("KailleraChatComposer");
    gameComposer->setFixedHeight(24);
    auto* gameComposerLayout = new QHBoxLayout(gameComposer);
    gameComposerLayout->setContentsMargins(10, 2, 4, 2);
    gameComposerLayout->setSpacing(4);

    m_gameChatInput = new QLineEdit(gameComposer);
    m_gameChatInput->setPlaceholderText("Type a message...");
    m_gameChatInput->setClearButtonEnabled(true);
    m_gameChatInput->setObjectName("KailleraChatComposerInput");
    m_gameChatInput->setFrame(false);

    m_btnSendGame = new QPushButton(gameComposer);
    m_btnSendGame->setObjectName("KailleraChatComposerSendButton");
    m_btnSendGame->setToolTip("Send game chat message");
    m_btnSendGame->setText("");
    m_btnSendGame->setIcon(themedLineIcon("play-line"));
    m_btnSendGame->setIconSize(QSize(13, 13));
    connect(m_btnSendGame, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onSendGameChat);
    connect(m_gameChatInput, &QLineEdit::returnPressed, this, &KailleraServerBrowserDialog::onSendGameChat);

    gameComposerLayout->addWidget(m_gameChatInput);
    gameComposerLayout->addWidget(m_btnSendGame, 0, Qt::AlignVCenter);
    gameChatRow->addWidget(gameComposer);
    chatVBox->addLayout(gameChatRow);

    auto* roomLobbiesPage = new QWidget(chatPane);
    auto* roomLobbiesLayout = new QVBoxLayout(roomLobbiesPage);
    roomLobbiesLayout->setContentsMargins(1, 0, 0, 1);
    roomLobbiesLayout->setSpacing(0);
    m_roomLobbyTable = new QTableWidget(0, 4, roomLobbiesPage);
    m_roomLobbyTable->setObjectName("KailleraSurface");
    m_roomLobbyTable->setHorizontalHeaderLabels({"Game", "Host", "Status", "Users"});
    m_roomLobbyTable->horizontalHeader()->setStretchLastSection(false);
    m_roomLobbyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_roomLobbyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_roomLobbyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_roomLobbyTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_roomLobbyTable->verticalHeader()->setVisible(false);
    m_roomLobbyTable->setShowGrid(false);
    m_roomLobbyTable->setAlternatingRowColors(true);
    m_roomLobbyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_roomLobbyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_roomLobbyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_roomLobbyTable->setSortingEnabled(false);
    roomLobbiesLayout->addWidget(m_roomLobbyTable);

    m_roomChatStack->addWidget(gameChatPage);
    m_roomChatStack->addWidget(roomLobbiesPage);
    m_roomChatStack->setCurrentIndex(0);
    chatPaneLayout->addWidget(chatHeader);
    chatPaneLayout->addWidget(m_roomChatStack, 1);

    roomSplitter->addWidget(chatPane);

    // Player table (center)
    auto* playersPane = new QWidget(widget);
    playersPane->setObjectName("KailleraPaneRightJoined");
    auto* playersPaneLayout = new QVBoxLayout(playersPane);
    playersPaneLayout->setContentsMargins(0, 0, 0, 0);
    playersPaneLayout->setSpacing(0);

    auto* playersHeader = new QWidget(playersPane);
    playersHeader->setObjectName("KailleraPaneHeader");
    auto* playersHeaderLayout = new QHBoxLayout(playersHeader);
    playersHeaderLayout->setContentsMargins(10, 4, 10, 4);
    playersHeaderLayout->setSpacing(6);
    auto* playersHeaderIcon = new QLabel(playersHeader);
    playersHeaderIcon->setPixmap(themedLineIcon("gamepad-line").pixmap(14, 14));
    auto* playersHeaderTitle = new QLabel("LOBBY", playersHeader);
    playersHeaderTitle->setObjectName("KailleraPaneTitle");
    playersHeaderLayout->addWidget(playersHeaderIcon);
    playersHeaderLayout->addWidget(playersHeaderTitle);
    playersHeaderLayout->addStretch();
    m_playersInGameCountLabel = new QLabel("(0/?)", playersHeader);
    m_playersInGameCountLabel->setObjectName("KailleraPaneMeta");
    playersHeaderLayout->addWidget(m_playersInGameCountLabel);

    auto* playersBody = new QWidget(playersPane);
    auto* playersBodyLayout = new QVBoxLayout(playersBody);
    playersBodyLayout->setContentsMargins(0, 0, 1, 1);
    playersBodyLayout->setSpacing(0);

    m_playerTable = new QTableWidget(0, 3, playersBody);
    m_playerTable->setObjectName("KailleraSurface");
    m_playerTable->setHorizontalHeaderLabels({"Nick", "Ping", "Delay"});
    m_playerTable->horizontalHeader()->setStretchLastSection(true);
    m_playerTable->verticalHeader()->setVisible(false);
    m_playerTable->setShowGrid(false);
    m_playerTable->setAlternatingRowColors(true);
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
    m_playerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_playerTable, &QWidget::customContextMenuRequested,
            this, &KailleraServerBrowserDialog::onPlayerListContextMenu);
    playersBodyLayout->addWidget(m_playerTable);
    playersPaneLayout->addWidget(playersHeader);
    playersPaneLayout->addWidget(playersBody, 1);
    roomSplitter->addWidget(playersPane);

    // Buttons + stats (right)
    auto* rightWidget = new QWidget();
    rightWidget->setObjectName("KailleraSidebar");
    auto* rightVBox = new QVBoxLayout(rightWidget);
    rightVBox->setContentsMargins(10, 10, 10, 10);
    rightVBox->setSpacing(8);

    m_btnStart = new QPushButton("Start", rightWidget);
    m_btnStart->setObjectName("KailleraStartButton");
    m_btnStart->setIcon(whiteLineIcon("play-line"));
    m_btnStart->setIconSize(QSize(16, 16));
    rightVBox->addWidget(m_btnStart);

    auto* dropLeaveRow = new QHBoxLayout();
    dropLeaveRow->setSpacing(6);
    m_btnDrop = new QPushButton("Drop", rightWidget);
    m_btnDrop->setObjectName("KailleraSecondaryButton");
    m_btnDrop->setIcon(style()->standardIcon(QStyle::SP_BrowserStop));
    m_btnDrop->setIconSize(QSize(16, 16));
    m_btnLeave = new QPushButton("Leave", rightWidget);
    m_btnLeave->setObjectName("KailleraSecondaryButton");
    m_btnLeave->setIcon(themedLineIcon("door-open-line"));
    m_btnLeave->setIconSize(QSize(16, 16));
    dropLeaveRow->addWidget(m_btnDrop);
    dropLeaveRow->addWidget(m_btnLeave);
    rightVBox->addLayout(dropLeaveRow);

    auto* statsOptionRow = new QHBoxLayout();
    statsOptionRow->setSpacing(6);
    m_btnLagStat = new QPushButton("Lagstat", rightWidget);
    m_btnLagStat->setObjectName("KailleraSecondaryButton");
    m_btnLagStat->setIcon(themedLineIcon("search-line"));
    m_btnLagStat->setIconSize(QSize(16, 16));
    m_btnOptions = new QPushButton("Options", rightWidget);
    m_btnOptions->setObjectName("KailleraSecondaryButton");
    m_btnOptions->setIcon(themedLineIcon("settings-3-line"));
    m_btnOptions->setIconSize(QSize(16, 16));
    statsOptionRow->addWidget(m_btnLagStat);
    statsOptionRow->addWidget(m_btnOptions);
    rightVBox->addLayout(statsOptionRow);

    auto* advRow = new QHBoxLayout();
    advRow->setSpacing(6);
    m_btnAdvertise = new QPushButton("Advertise", rightWidget);
    m_btnAdvertise->setObjectName("KailleraSecondaryButton");
    m_btnAdvertise->setIcon(themedLineIcon("volume-up-line"));
    m_btnAdvertise->setIconSize(QSize(16, 16));
    advRow->addWidget(m_btnAdvertise);
    rightVBox->addLayout(advRow);

    m_recordCheck = new QCheckBox("Record game", rightWidget);
    rightVBox->addWidget(m_recordCheck);

    m_fpsLabel = new QLabel("<span style='color:#8b8b8b;'>Rate</span> <span style='font-weight:600;'>0 fps / 0 pps</span>", rightWidget);
    m_fpsLabel->setObjectName("KailleraStatValue");
    m_delayLabel = new QLabel("<span style='color:#8b8b8b;'>Delay</span> <span style='font-weight:600;'>0 frames</span>", rightWidget);
    m_delayLabel->setObjectName("KailleraStatValue");
    m_fpsLabel->setTextFormat(Qt::RichText);
    m_delayLabel->setTextFormat(Qt::RichText);
    rightVBox->addWidget(m_fpsLabel);
    rightVBox->addWidget(m_delayLabel);

    rightVBox->addStretch();

    connect(m_btnStart, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onStartGame);
    connect(m_btnDrop, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onDropGame);
    connect(m_btnLeave, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onLeaveGame);
    connect(m_btnLagStat, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onLagStat);
    connect(m_btnOptions, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onOptions);
    connect(m_btnAdvertise, &QPushButton::clicked, this, &KailleraServerBrowserDialog::onAdvertise);

    roomSplitter->setStretchFactor(0, 3);  // lobby chat
    roomSplitter->setStretchFactor(1, 2);  // lobby

    auto* roomRow = new QHBoxLayout();
    roomRow->setContentsMargins(0, 0, 0, 0);
    roomRow->setSpacing(8);
    roomRow->addWidget(roomSplitter, 1);
    roomRow->addWidget(rightWidget);
    layout->addLayout(roomRow);

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
    m_inGameRoom = false;
    m_isHost = false;
    m_currentGameName.clear();
    m_roomMaxPlayers = 0;
    m_statsTimer->stop();

    // Clear player table
    m_playerTable->setRowCount(0);
    m_gameChat->clear();
    setRoomChatSwapView(false);
    updateHeaderCounts();

    // Switch bottom to game list and re-enable lobby create action
    showBottomGameList();
    if (m_btnCreateSwap != nullptr)
    {
        m_btnCreateSwap->setVisible(true);
        m_btnCreateSwap->setEnabled(true);
    }

    updateTitle();
}

void KailleraServerBrowserDialog::switchToGameRoom()
{
    m_inGameRoom = true;
    m_roomMaxPlayers = m_isHost ? CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPlayers)
                                : detectCurrentRoomMaxPlayers();

    // Clear game room state
    m_playerTable->setRowCount(0);
    m_gameChat->clear();
    updateHeaderCounts();
    m_lastFrameCount = kaillera_get_frames_count();
    m_lastPPS = SOCK_SEND_PACKETS;

    // Start stats updates
    m_statsTimer->start(1000);

    // Detect non-game lobbies — Start should be disabled
    bool isNonGameLobby = (m_currentGameName == "*Away (leave messages)" ||
                           m_currentGameName == "*Chat (not game)");

    // Update button visibility based on host status
    m_btnStart->setEnabled(m_isHost && !isNonGameLobby);

    // Switch bottom to game room and reset left panel swap to chat view
    showBottomGameRoom();
    setRoomChatSwapView(false);
    if (m_btnCreateSwap != nullptr)
    {
        m_btnCreateSwap->setVisible(false);
    }

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

void KailleraServerBrowserDialog::setRoomChatSwapView(bool showLobbies)
{
    m_roomShowingLobbies = showLobbies;
    if (m_roomChatStack != nullptr)
    {
        m_roomChatStack->setCurrentIndex(showLobbies ? 1 : 0);
    }
    if (m_btnSwapChat != nullptr)
    {
        m_btnSwapChat->setText(showLobbies ? "Show chat" : "Show lobbies");
        m_btnSwapChat->setToolTip(showLobbies ? "Show lobby chat" : "Show open lobbies");
    }
    if (showLobbies)
    {
        refreshRoomLobbyTable();
    }
}

void KailleraServerBrowserDialog::refreshRoomLobbyTable()
{
    if (m_roomLobbyTable == nullptr || m_gameTable == nullptr)
    {
        return;
    }

    m_roomLobbyTable->setSortingEnabled(false);
    m_roomLobbyTable->setRowCount(0);
    for (int row = 0; row < m_gameTable->rowCount(); ++row)
    {
        QTableWidgetItem* gameItem = m_gameTable->item(row, 0);
        if (gameItem == nullptr)
        {
            continue;
        }

        const QString gameName = gameItem->text();
        QTableWidgetItem* statusItem = m_gameTable->item(row, 4);
        if (statusItem == nullptr)
            continue;

        const int outRow = m_roomLobbyTable->rowCount();
        m_roomLobbyTable->insertRow(outRow);
        m_roomLobbyTable->setItem(outRow, 0, new QTableWidgetItem(gameName));
        m_roomLobbyTable->setItem(outRow, 1, new QTableWidgetItem(m_gameTable->item(row, 3) ? m_gameTable->item(row, 3)->text() : ""));
        m_roomLobbyTable->setItem(outRow, 2, new QTableWidgetItem(statusItem->text()));
        m_roomLobbyTable->setItem(outRow, 3, new QTableWidgetItem(m_gameTable->item(row, 5) ? m_gameTable->item(row, 5)->text() : ""));
    }
    m_roomLobbyTable->setSortingEnabled(true);
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
    updateHeaderCounts();
    if (m_roomShowingLobbies)
    {
        refreshRoomLobbyTable();
    }
}

int KailleraServerBrowserDialog::detectCurrentRoomMaxPlayers()
{
    if (m_roomMaxPlayers > 0)
    {
        return m_roomMaxPlayers;
    }

    if (m_currentGameName.isEmpty() || m_gameTable == nullptr)
    {
        return 0;
    }

    int gameRow = findRowByText(m_gameTable, 0, m_currentGameName);
    if (gameRow < 0)
    {
        return 0;
    }

    QTableWidgetItem* usersItem = m_gameTable->item(gameRow, 5);
    if (usersItem == nullptr)
    {
        return 0;
    }

    const QString usersText = usersItem->text().trimmed();
    const QStringList parts = usersText.split('/');
    if (parts.size() != 2)
    {
        return 0;
    }

    bool ok = false;
    int parsedMax = parts[1].trimmed().toInt(&ok);
    return (ok && parsedMax > 0) ? parsedMax : 0;
}

void KailleraServerBrowserDialog::updateHeaderCounts()
{
    if (m_connectedPlayersCountLabel != nullptr && m_userTable != nullptr)
    {
        m_connectedPlayersCountLabel->setText(QString("(%1)").arg(m_userTable->rowCount()));
    }

    if (m_playersInGameCountLabel == nullptr || m_playerTable == nullptr)
    {
        return;
    }

    int currentPlayers = m_playerTable->rowCount();
    int maxPlayers = detectCurrentRoomMaxPlayers();
    if (maxPlayers > 0)
    {
        m_playersInGameCountLabel->setText(QString("(%1/%2)").arg(currentPlayers).arg(maxPlayers));
    }
    else
    {
        m_playersInGameCountLabel->setText(QString("(%1/?)").arg(currentPlayers));
    }
}

QString KailleraServerBrowserDialog::timestamp(const QString& baseColor)
{
    QColor messageColor;
    if (!baseColor.isEmpty())
    {
        messageColor = QColor(baseColor);
    }
    if (!messageColor.isValid())
    {
        messageColor = palette().text().color();
    }

    const QColor backgroundColor = palette().base().color();
    const bool darkTheme = backgroundColor.value() < 128;
    QColor tsColor = darkTheme
        ? blendColors(messageColor, QColor(255, 255, 255), 72)
        : blendColors(messageColor, backgroundColor, 42);

    return QString("<span style='color:%1;'>[%2] </span>")
        .arg(tsColor.name(QColor::HexRgb), QTime::currentTime().toString("h:mm AP"));
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

    const QString color = infoMessageColor();
    m_lobbyChat->append("<span style='color:" + color + ";'>" + timestamp(color) + "* Joins: " + name.toHtmlEscaped()
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
    const QString color = infoMessageColor();
    m_lobbyChat->append("<span style='color:" + color + ";'>" + timestamp(color) + msg + "</span>");
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
    if (m_roomShowingLobbies)
    {
        refreshRoomLobbyTable();
    }
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
    if (m_roomShowingLobbies)
    {
        refreshRoomLobbyTable();
    }

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

    if (m_roomShowingLobbies)
    {
        refreshRoomLobbyTable();
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

        QTableWidgetItem* gameNameItem = m_gameTable->item(row, 0);
        if (m_inGameRoom && gameNameItem && gameNameItem->text() == m_currentGameName && maxPlayers > 0)
        {
            m_roomMaxPlayers = maxPlayers;
            updateHeaderCounts();
        }

        if (m_roomShowingLobbies)
        {
            refreshRoomLobbyTable();
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
        constexpr const char* pmColor = "lime";
        m_lobbyChat->append("<span style='color:lime;'>" + timestamp(pmColor)
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
    constexpr const char* motdColor = "green";
    m_lobbyChat->append("<span style='color:green;'>" + timestamp(motdColor) + "- " + linkify(message) + "</span>");
}

void KailleraServerBrowserDialog::onLoginStatus(QString message)
{
    constexpr const char* errorColor = "red";
    m_lobbyChat->append("<span style='color:red;'>" + timestamp(errorColor) + message.toHtmlEscaped() + "</span>");
}

void KailleraServerBrowserDialog::onError(QString message)
{
    constexpr const char* errorColor = "red";
    m_lobbyChat->append("<span style='color:red;'>" + timestamp(errorColor) + "Error: " + message.toHtmlEscaped() + "</span>");
}

// ---- Game room handlers ----

void KailleraServerBrowserDialog::onUserGameCreated()
{
    m_isHost = true;
    switchToGameRoom();
    const QString color = infoMessageColor();
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color) + "You created a game. Waiting for players...</span>");
    executeOptions();
}

void KailleraServerBrowserDialog::onUserGameJoined()
{
    m_isHost = false;
    switchToGameRoom();
    const QString color = infoMessageColor();
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color) + "You joined the game. Waiting for host to start...</span>");
}

void KailleraServerBrowserDialog::onUserGameClosed()
{
    constexpr const char* errorColor = "red";
    m_gameChat->append("<span style='color:red;'>" + timestamp(errorColor) + "Game has been closed.</span>");
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
    updateHeaderCounts();
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
    updateHeaderCounts();

    const QString color = infoMessageColor();
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color) + "* " + name.toHtmlEscaped() + " joined the game</span>");

    // Update this user's status in the lobby user table
    updateUserStatus(name, "Waiting", 0);

    // Auto-kick players who join while game is already running (host only)
    if (m_isHost && kaillera_is_game_running())
    {
        kaillera_kick_user(uid);
        return;
    }

    // Auto-send configured host join message
    if (m_isHost)
    {
        QString joinMsg = QString::fromStdString(
            CoreSettingsGetStringValue(SettingsID::Kaillera_JoinMessageHost)).trimmed();
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
    updateHeaderCounts();

    const QString color = infoMessageColor();
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color) + "* " + name.toHtmlEscaped() + " left the game</span>");

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
    constexpr const char* warnColor = "orange";
    m_gameChat->append("<span style='color:orange;'>" + timestamp(warnColor)
        + "* " + name.toHtmlEscaped() + " (player " + QString::number(player) + ") has dropped</span>");

    // Host (player 1) dropping forces everyone else to drop too.
    if (player == 1 && player != playerno)
    {
        kaillera_game_drop();
    }
}

void KailleraServerBrowserDialog::onGameStarted(QString game, int player, int numPlayers)
{
    const QString color = infoMessageColor();
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color)
        + "* Starting: " + game.toHtmlEscaped()
        + " (" + QString::number(player) + "/" + QString::number(numPlayers) + ")</span>");
    m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color)
        + "- press \"Drop\" if emulator fails</span>");

    if (n02_kaillera_recording_enabled)
    {
        m_gameChat->append("<span style='color:" + color + ";'>" + timestamp(color)
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
    m_fpsLabel->setText("<span style='color:#8b8b8b;'>Rate</span> <span style='font-weight:600;'>Waiting for others...</span>");
    int secs = tx / 1000;
    int tenths = (tx % 1000) / 100;
    m_delayLabel->setText(QString("<span style='color:#8b8b8b;'>Sync wait</span> <span style='font-weight:600;'>%1.%2s</span>")
        .arg(secs, 3, 10, QChar('0'))
        .arg(tenths));
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

void KailleraServerBrowserDialog::onPlayerListContextMenu(const QPoint& pos)
{
    int row = m_playerTable->rowAt(pos.y());
    if (row < 0)
        return;

    m_playerTable->selectRow(row);

    QMenu menu(this);
    QAction* kickAction = menu.addAction(
        QIcon::fromTheme("user-trash", style()->standardIcon(QStyle::SP_DialogCancelButton)),
        "Kick Player");

    if (!m_isHost)
    {
        kickAction->setEnabled(false);
        kickAction->setText("Kick Player (Host only)");
    }
    else if (row == 0)
    {
        kickAction->setEnabled(false);
        kickAction->setText("Kick Player (Cannot kick host)");
    }

    QAction* chosen = menu.exec(m_playerTable->viewport()->mapToGlobal(pos));
    if (chosen == kickAction && kickAction->isEnabled())
    {
        onKickPlayer();
    }
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
        return;
    }

    // Game room mode: toggle only the left game-chat card between chat and lobby snapshot
    setRoomChatSwapView(!m_roomShowingLobbies);
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
        if (m_isHost)
        {
            m_roomMaxPlayers = CoreSettingsGetIntValue(SettingsID::Kaillera_MaxPlayers);
            updateHeaderCounts();
        }
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
            // Toggle only the game-chat card between chat and lobby snapshot
            if (m_inGameRoom)
            {
                setRoomChatSwapView(!m_roomShowingLobbies);
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

    m_fpsLabel->setText(QString("<span style='color:#8b8b8b;'>Rate</span> <span style='font-weight:600;'>%1 fps / %2 pps</span>")
        .arg(fps)
        .arg(pps));

    int delay = kaillera_get_delay();
    m_delayLabel->setText(QString("<span style='color:#8b8b8b;'>Delay</span> <span style='font-weight:600;'>%1 frames</span>")
        .arg(delay));
}

#endif // _WIN32
