/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERAP2PDIALOG_HPP
#define KAILLERAP2PDIALOG_HPP

#ifdef NETPLAY

#include <QDialog>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QTimer>
#include <QGroupBox>
#include <QAction>

class KailleraP2PDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraP2PDialog(bool isHost, const QString& gameName,
                               const QString& username,
                               const QString& joinCode = QString(),
                               QWidget* parent = nullptr);
    ~KailleraP2PDialog() override;

signals:
    void peerNicknameResolved(QString nickname);
    void rollbackSessionReady(QString gameName, QString remoteAddress, int localPort, int remotePort, int localPlayer, int frameDelay, int predictionWindow);

protected:
    void reject() override;

private slots:
    void onChatReceived(QString nick, QString message);
    void onGameStarted(QString game, int player, int maxPlayers);
    void onGameEnded();
    void onClientDropped(QString nick, int player);
    void onDebug(QString message);
    void onHostedGame(QString game);
    void onPingUpdated(int ping);
    void onPeerJoined();
    void onPeerLeft();
    void onPeerInfo(QString name, QString app);
    void onFodippResult(QString host);
    void onSsrvPacketReceived(QByteArray cmd, QByteArray saddr);

    void onSendChat();
    void onReady();
    void onDrop();
    void onCopyConnectCode();
    void onStepTimer();
    void onTravTimer();

private:
    enum class GameLayer
    {
        Standard,
        Rollback
    };

    void setupUI();
    void connectSignals();
    void cleanupSessionForClose();
    void setGameLayer(GameLayer layer, bool announceToPeer, bool resetReady);
    void applyGameLayerUI();
    void sendGameLayer();
    void resetReadyState();
    bool parseGameLayerMessage(const QString& message, GameLayer& layer) const;
    bool isRollbackMode() const;

    // NAT traversal helpers
    void travSendToServer(const QByteArray& msg);
    void travSendClaimAuto();
    void travSendClaimAck();
    void travSendHostOpen();
    void travSendHostKeep();
    void travSendHostClose();
    void travSendJoin();
    void travPunchEndpoint(const QString& hostIp, int hostPort, const QString& token);
    void travResetState();
    bool travTryFallbackConnect(const QString& reason);
    void updateHostCodeUI();
    void travLoadIdentity();
    void travSaveIdentity() const;
    void travClearIdentity();
    void ssrvSend(const QByteArray& cmd);
    void ssrvWhatIsMyIp();
    void enlistGame();
    void unenlistGame();
    QString buildEnlistAppName();

    bool m_isHost;
    GameLayer m_gameLayer = GameLayer::Standard;
    bool m_rollbackGameActive = false;
    bool m_closeCleanupDone = false;
    bool m_ready = false;
    QString m_gameName;
    QString m_username;

    // Top
    QLabel* m_gameLabel = nullptr;

    // Chat area
    QTextBrowser* m_chat = nullptr;
    QLineEdit* m_chatInput = nullptr;
    QPushButton* m_btnChat = nullptr;

    // Control buttons
    QPushButton* m_btnReady = nullptr;
    QPushButton* m_btnDrop = nullptr;
    QCheckBox* m_recordCheck = nullptr;
    QCheckBox* m_enlistCheck = nullptr;
    QLabel* m_pingLabel = nullptr;
    QPushButton* m_standardLayerButton = nullptr;
    QPushButton* m_rollbackLayerButton = nullptr;

    // Host group
    QGroupBox* m_hostGroup = nullptr;
    QLabel* m_gameLayerStatusLabel = nullptr;
    QLabel* m_frameDelayLabel = nullptr;
    QWidget* m_frameDelayRow = nullptr;
    QComboBox* m_frameDelayCombo = nullptr;
    QWidget* m_predictionWindowRow = nullptr;
    QComboBox* m_predictionWindowCombo = nullptr;
    QLineEdit* m_connectCodeEdit = nullptr;
    QAction* m_copyAction = nullptr;

    // Timers
    QTimer* m_stepTimer = nullptr;
    QTimer* m_travTimer = nullptr;
    QTimer* m_copyFeedbackTimer = nullptr;

    // ---- NAT traversal state ----
    bool m_travHostEnabled = false;
    bool m_travJoinEnabled = false;
    QString m_travCode;
    QString m_travToken;
    QString m_travLiveToken;
    int m_travRegAttempts = 0;
    bool m_travHostSessionSuspended = false;
    bool m_travHostFallbackActive = false;
    bool m_travHostIpPending = false;
    QString m_travHostIpPort;

    qint64 m_travNextRegMs = 0;
    qint64 m_travNextKeepMs = 0;

    // Join-by-code state
    qint64 m_travNextJoinMs = 0;
    qint64 m_travJoinDeadlineMs = 0;
    QString m_travJoinCode;
    bool m_travJoinGotHost = false;
    QString m_travJoinToken;
    QString m_travJoinHostIp;
    int m_travJoinHostPort = 0;
    qint64 m_travNextConnectMs = 0;
    QString m_travJoinFallbackIpPort;
    bool m_travJoinFallbackTried = false;
    bool m_travJoinBusy = false;
    int m_travJoinPunchAttempts = 0;

    // Host peer-punching state
    QString m_travHostPeerIp;
    int m_travHostPeerPort = 0;
    qint64 m_travHostPeerDeadlineMs = 0;
    qint64 m_travNextHostPunchMs = 0;

    bool m_ssrvCopyMyIpPending = false;
    int m_travTimerStep = 0;
};

#endif // NETPLAY
#endif // KAILLERAP2PDIALOG_HPP
