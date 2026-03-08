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

#ifdef _WIN32

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

#include <future>

struct ServerEntry {
    QString name;
    QString host; // "ip:port"
    QString players = "-";
    int playerCount = -1;
    QString ping;
    int pingValue = 999999;
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
    void onConnectServer();
    void onServerDoubleClicked(int row, int column);
    void onServerRightClicked(QPoint pos);
    void onWaitingGames();

    // P2P tab
    void onP2PHost();
    void onP2PJoin();
    void onP2PPasteAndGo();
    void onP2PWaitingGames();
    void onP2PStoredClicked(int row, int column);

    // Network replies
    void onWaitingGamesReply(QNetworkReply* reply);

    // Tab changed
    void onTabChanged(int index);

private:
    void setupUI();
    QWidget* createServerTab();
    QWidget* createP2PTab();

    void loadServerList();
    void saveServerList();
    void refreshServerListDisplay();
    void fetchLiveServerList();
    void schedulePingAllServers();
    void pingAllServers();
    void startNextServerPing();
    void pollServerPing();
    QVector<ServerEntry> parseLiveServerList(const QByteArray& data) const;
    int favoriteServerIndexByHost(const QString& host) const;
    int cachedServerIndexByHost(const QString& host) const;
    void toggleFavoriteServer(const QString& host, const QString& name);
    void moveFavoriteServer(int favoriteIndex, int delta);
    void updateServerPing(const QString& host, int pingMs);
    void updateVisibleServerPing(const QString& host, const QString& pingText);
    void cacheVisibleLiveServerOrder();
    void updateServerButtons();
    void saveSettings();
    void loadSettings();

    // P2P recent/favorite peers
    void loadP2PStoredUsers();
    void saveP2PStoredUsers();
    void refreshP2PStoredDisplay();
    int p2pStoredIndexByHost(const QString& host) const;
    int p2pFavoriteCount() const;
    void toggleP2PStoredFavorite(int row);
    void rememberP2PStoredEntry(const QString& host, const QString& nickname = QString());
    void updateP2PStoredNickname(const QString& host, const QString& nickname);

    // State machine timer (replaces blocking KSSDFA loop)
    QTimer* m_stateMachineTimer = nullptr;

    // Mode tabs
    QTabWidget* m_tabWidget = nullptr;

    // Server list (Server tab)
    QTableWidget* m_serverTable = nullptr;
    QVector<ServerEntry> m_favoriteServers;
    QVector<ServerEntry> m_cachedLiveServers;
    QVector<ServerEntry> m_displayServers;

    // Server tab buttons
    QPushButton* m_btnAdd = nullptr;
    QPushButton* m_btnConnect = nullptr;
    QPushButton* m_btnWaitingGames = nullptr;

    // P2P host controls
    QComboBox* m_p2pGameCombo = nullptr;
    QLineEdit* m_p2pPortEdit = nullptr;
    QPushButton* m_btnP2PHost = nullptr;

    // P2P connect controls
    QLineEdit* m_p2pHostEdit = nullptr;
    QPushButton* m_btnP2PJoin = nullptr;
    QPushButton* m_btnP2PPasteGo = nullptr;
    QTableWidget* m_p2pStoredTable = nullptr;
    QPushButton* m_btnP2PWaitingGames = nullptr;

    struct P2PStoredEntry {
        QString name;
        QString host;
        bool favorite = false;
    };
    QVector<P2PStoredEntry> m_p2pStoredUsers;

    // Shared settings (shown above tabs)
    QLineEdit* m_usernameEdit = nullptr;

    // Frame delay (Server tab only)
    QComboBox* m_frameDelayCombo = nullptr;

    // Network manager for master list fetching
    QNetworkAccessManager* m_netManager = nullptr;
    QTimer* m_serverPingPollTimer = nullptr;
    QStringList m_pendingPingHosts;
    QString m_activePingHost;
    std::future<int> m_activePingFuture;
    bool m_serverListNeedsRefresh = false;
    bool m_pingAllQueued = false;
    bool m_pingAllInProgress = false;

};

#endif // _WIN32
#endif // KAILLERANETPLAYDIALOG_HPP
