/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERAUIBRIDGE_HPP
#define KAILLERAUIBRIDGE_HPP

#ifdef NETPLAY

#include <QObject>
#include <QString>

//
// KailleraUIBridge
//
// Singleton QObject that registers n02::UICallbacks and re-emits
// every callback as a Qt signal (thread-safe via QueuedConnection).
// Qt dialogs connect to these signals to drive the UI.
//
class KailleraUIBridge : public QObject
{
    Q_OBJECT

public:
    // Get the singleton instance
    static KailleraUIBridge& instance();

    // Register n02::UICallbacks so core callbacks route to Qt signals.
    // Must be called after n02::init() and before selectServerDialog().
    void registerCallbacks();

    // Unregister callbacks (clears n02::UICallbacks)
    void unregisterCallbacks();

signals:
    // ---- P2P signals ----
    void p2pChatReceived(QString nick, QString message);
    void p2pGameStarted(QString game, int player, int maxPlayers);
    void p2pGameEnded();
    void p2pClientDropped(QString nick, int player);
    void p2pDebugMessage(QString message);
    void p2pHostedGame(QString game);
    void p2pPingUpdated(int ping);
    void p2pPeerLeft();
    void p2pPeerJoined();
    void p2pPeerInfo(QString name, QString app);
    void p2pFodippResult(QString host);
    void p2pSsrvPacketReceived(QByteArray cmd, QByteArray saddr);

    // ---- Kaillera server signals ----
    void kailleraUserAdded(QString name, int ping, int status, unsigned short id, char conn);
    void kailleraGameAdded(QString gameName, unsigned int id, QString emulator, QString owner, QString users, char status);
    void kailleraChatReceived(QString name, QString message);
    void kailleraGameChatReceived(QString name, QString message);
    void kailleraMotdReceived(QString name, QString message);
    void kailleraUserJoined(QString name, int ping, unsigned short id, char conn);
    void kailleraUserLeft(QString name, QString quitMsg, unsigned short id);
    void kailleraGameCreated(QString gameName, unsigned int id, QString emulator, QString owner);
    void kailleraGameClosed(unsigned int id);
    void kailleraGameStatusChanged(unsigned int id, char status, int players, int maxPlayers);
    void kailleraUserGameCreated();
    void kailleraUserGameJoined();
    void kailleraUserGameClosed();
    void kailleraPlayerAdded(QString name, int ping, unsigned short id, char conn);
    void kailleraPlayerJoined(QString name, int ping, unsigned short uid, char conn);
    void kailleraPlayerLeft(QString name, unsigned short id);
    void kailleraUserKicked();
    void kailleraLoginStatus(QString message);
    void kailleraPlayerDropped(QString name, int player);
    void kailleraGameStarted(QString game, int player, int numPlayers);
    void kailleraNetsyncWait(int tx);
    void kailleraGameEnded();
    void kailleraDebugMessage(QString message);
    void kailleraErrorMessage(QString message);
    void recordingFileClosed();

public:
    // Getter/setter for the selected frame delay (used by p2p_getSelectedDelay callback)
    void setSelectedDelay(int delay);
    int getSelectedDelay() const;

private:
    KailleraUIBridge();
    ~KailleraUIBridge() override = default;
    KailleraUIBridge(const KailleraUIBridge&) = delete;
    KailleraUIBridge& operator=(const KailleraUIBridge&) = delete;

    int m_selectedDelay = 0;
};

#endif // NETPLAY
#endif // KAILLERAUIBRIDGE_HPP
