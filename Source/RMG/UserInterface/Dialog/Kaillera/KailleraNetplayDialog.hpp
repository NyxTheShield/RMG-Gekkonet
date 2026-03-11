/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERANETPLAYDIALOG_HPP
#define KAILLERANETPLAYDIALOG_HPP

#ifdef NETPLAY

#include <QDialog>
#include <QTimer>
#include <QTabWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>

struct ServerEntry {
    QString name;
    QString host; // "ip:port"
    QString ping;
};

class KailleraNetplayDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraNetplayDialog(QWidget* parent = nullptr);
    ~KailleraNetplayDialog() override;

private slots:
    void onStateMachineTimer();

    // Server tab
    void onAddServer();
    void onEditServer();
    void onDeleteServer();
    void onConnectServer();
    void onServerDoubleClicked(int row, int column);
    void onServerRightClicked(QPoint pos);
    void onLiveServerList();
    void onWaitingGames();

    // P2P tab
    void onP2PHost();
    void onP2PJoin();
    void onP2PPasteAndGo();
    void onP2PAddStored();
    void onP2PEditStored();
    void onP2PDeleteStored();
    void onP2PWaitingGames();
    void onP2PStoredClicked(int row, int column);

    // Network replies
    void onWaitingGamesReply(QNetworkReply* reply);

    // Tab changed
    void onTabChanged(int index);

    // Playback tab
    void onPlaybackPlay();
    void onPlaybackStop();
    void onPlaybackDelete();
    void onPlaybackRefresh();
    void onPlaybackOpenFolder();
    void onPlaybackDoubleClicked(int row, int column);

private:
    void setupUI();
    QWidget* createServerTab();
    QWidget* createP2PTab();
    QWidget* createPlaybackTab();

    void loadServerList();
    void saveServerList();
    void refreshServerListDisplay();
    void saveSettings();
    void loadSettings();
    void pingServerRow(int row);
    int serverIndexFromRow(int row);

    // P2P stored users
    void loadP2PStoredUsers();
    void saveP2PStoredUsers();
    void refreshP2PStoredDisplay();

    // Playback
    void populatePlaybackList();

    // State machine timer (replaces blocking KSSDFA loop)
    QTimer* m_stateMachineTimer = nullptr;

    // Mode tabs
    QTabWidget* m_tabWidget = nullptr;

    // Server list (Server tab)
    QTableWidget* m_serverTable = nullptr;
    QVector<ServerEntry> m_servers;

    // Server tab buttons
    QPushButton* m_btnAdd = nullptr;
    QPushButton* m_btnEdit = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnConnect = nullptr;
    QPushButton* m_btnLiveList = nullptr;
    QPushButton* m_btnWaitingGames = nullptr;

    // P2P tab controls (Host sub-tab)
    QLineEdit* m_p2pGameEdit = nullptr;
    QTableWidget* m_p2pGameList = nullptr;
    QLineEdit* m_p2pPortEdit = nullptr;
    QPushButton* m_btnP2PHost = nullptr;

    // P2P tab controls (Connect sub-tab)
    QLineEdit* m_p2pHostEdit = nullptr;
    QPushButton* m_btnP2PJoin = nullptr;
    QPushButton* m_btnP2PPasteGo = nullptr;
    QTableWidget* m_p2pStoredTable = nullptr;
    QPushButton* m_btnP2PAddStored = nullptr;
    QPushButton* m_btnP2PEditStored = nullptr;
    QPushButton* m_btnP2PDeleteStored = nullptr;
    QPushButton* m_btnP2PWaitingGames = nullptr;

    struct P2PStoredEntry {
        QString name;
        QString host;
    };
    QVector<P2PStoredEntry> m_p2pStoredUsers;

    // Shared settings (shown above tabs)
    QLineEdit* m_usernameEdit = nullptr;

    // Frame delay (Server tab only)
    QComboBox* m_frameDelayCombo = nullptr;

    // Playback tab
    QTableWidget* m_playbackTable = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnPBDelete = nullptr;
    QPushButton* m_btnPBRefresh = nullptr;
    QPushButton* m_btnOpenFolder = nullptr;
    bool m_playbackWasActive = false;

    // Network manager for master list fetching
    QNetworkAccessManager* m_netManager = nullptr;

    // Bottom bar
    QPushButton* m_btnClose = nullptr;
};

#endif // NETPLAY
#endif // KAILLERANETPLAYDIALOG_HPP
