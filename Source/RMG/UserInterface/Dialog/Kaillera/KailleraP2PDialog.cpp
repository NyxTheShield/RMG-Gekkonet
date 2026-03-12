/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraP2PDialog.hpp"

#ifdef NETPLAY

#include "../../KailleraUIBridge.hpp"

#include <RMG-Core/Emulation.hpp>
#include <RMG-Core/Kaillera.hpp>

#include "core/p2p_core.h"
#include "n02_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QIcon>

#include <cstring>

#ifndef _WIN32
#include <chrono>
static inline unsigned long GetTickCount() {
    using namespace std::chrono;
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

static const char* kN02TraversalHost = "nat.smash64.net";
static const int kN02TraversalPort = 6264;
static const char* kSsrvHost = "kaillerareborn.2manygames.fr";
static const int kSsrvPort = 27887;

static QString timestamp()
{
    return QDateTime::currentDateTime().toString("[h:mm AP] ");
}

// Check if a string looks like a NAT traversal code rather than an IP address.
// Matches n02-rmg's LooksLikeTraversalCode().
static bool looksLikeTraversalCode(const QString& s)
{
    if (s.isEmpty()) return false;

    // IP addresses and hostnames contain '.', ':', or '/'
    for (const QChar& ch : s)
    {
        if (ch == '.' || ch == ':' || ch == '/') return false;
    }

    int alnumCount = 0;
    for (const QChar& ch : s)
    {
        if (ch.isLetterOrNumber())
        {
            alnumCount++;
            continue;
        }
        if (ch == '-' || ch == '_') continue;
        return false;
    }

    return (alnumCount >= 6 && alnumCount <= 16);
}

// Try to extract an IPv4 address and optional port from a string.
// Returns true if an IP was found.
static bool tryExtractIPv4AndPort(const QByteArray& s, QString& outIp, int& outPort)
{
    outIp.clear();
    outPort = 0;

    const char* data = s.constData();
    int len = s.size();

    for (int i = 0; i < len; i++)
    {
        if (data[i] < '0' || data[i] > '9') continue;

        unsigned int octets[4];
        const char* cur = data + i;
        bool ok = true;
        for (int o = 0; o < 4; o++)
        {
            unsigned int val = 0;
            int digits = 0;
            while (*cur >= '0' && *cur <= '9' && digits < 3)
            {
                val = val * 10 + (unsigned int)(*cur - '0');
                cur++;
                digits++;
            }
            if (digits == 0 || val > 255) { ok = false; break; }
            octets[o] = val;
            if (o < 3)
            {
                if (*cur != '.') { ok = false; break; }
                cur++;
            }
        }
        if (!ok) continue;

        outIp = QString("%1.%2.%3.%4").arg(octets[0]).arg(octets[1]).arg(octets[2]).arg(octets[3]);

        // Optional ":port"
        while (*cur == ' ' || *cur == '\t') cur++;
        if (*cur == ':') cur++;
        else if (*cur != 0 && (*cur < '0' || *cur > '9')) return true;

        unsigned int port = 0;
        int digits = 0;
        while (*cur >= '0' && *cur <= '9' && digits < 5)
        {
            port = port * 10 + (unsigned int)(*cur - '0');
            cur++;
            digits++;
        }
        if (digits > 0 && port > 0 && port <= 65535)
            outPort = (int)port;
        return true;
    }

    return false;
}

KailleraP2PDialog::KailleraP2PDialog(bool isHost, const QString& gameName,
                                     const QString& username,
                                     const QString& joinCode, QWidget* parent)
    : QDialog(parent, Qt::Window), m_isHost(isHost), m_gameName(gameName), m_username(username)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    setupUI();
    connectSignals();

    // P2P step timer — drives the p2p state machine (1ms)
    m_stepTimer = new QTimer(this);
    connect(m_stepTimer, &QTimer::timeout, this, &KailleraP2PDialog::onStepTimer);
    m_stepTimer->start(1);

    // NAT traversal housekeeping timer (1s)
    m_travTimer = new QTimer(this);
    connect(m_travTimer, &QTimer::timeout, this, &KailleraP2PDialog::onTravTimer);
    m_travTimer->start(1000);

    travResetState();

    if (m_isHost)
    {
        // Host always uses NAT traversal (host by code)
        m_travHostEnabled = true;
        travSendReg();
        m_travNextRegMs = QDateTime::currentMSecsSinceEpoch() + 2000;
        updateHostCodeUI();

        m_chat->append("<span style='color:green;'>" + timestamp() +
                       "Hosting " + m_gameName.toHtmlEscaped() +
                       " on port " + QString::number(p2p_core_get_port()) + "</span>");
    }
    else if (!joinCode.isEmpty() && looksLikeTraversalCode(joinCode))
    {
        // Join by traversal code
        m_travJoinEnabled = true;
        m_travJoinCode = joinCode;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        travSendJoin();
        m_travNextJoinMs = now + 3000;
        m_travJoinDeadlineMs = now + 15000;

        m_chat->append("<span style='color:green;'>" + timestamp() +
                       "Looking up host for code " + joinCode.toHtmlEscaped() + "</span>");
    }
}

KailleraP2PDialog::~KailleraP2PDialog()
{
    if (m_stepTimer)  m_stepTimer->stop();
    if (m_travTimer)  m_travTimer->stop();
}

void KailleraP2PDialog::setupUI()
{
    setWindowTitle(m_isHost ? "Hosting P2P" : "P2P Game");
    setMinimumSize(520, 420);
    resize(560, 480);

    auto* mainLayout = new QVBoxLayout(this);

    // Game label at top
    m_gameLabel = new QLabel(this);
    m_gameLabel->setText("Game: " + m_gameName);
    m_gameLabel->setFrameStyle(QFrame::Box | QFrame::Sunken);
    mainLayout->addWidget(m_gameLabel);

    // Chat / status area (takes most space)
    m_chat = new QTextBrowser(this);
    m_chat->setOpenExternalLinks(true);
    mainLayout->addWidget(m_chat, 1);

    // Chat input row
    auto* chatInputLayout = new QHBoxLayout();
    m_chatInput = new QLineEdit(this);
    m_btnChat = new QPushButton("Chat", this);
    chatInputLayout->addWidget(m_chatInput, 1);
    chatInputLayout->addWidget(m_btnChat);
    mainLayout->addLayout(chatInputLayout);

    // Button row: Ready, Drop Game, Record game checkbox  |  Host group
    auto* bottomLayout = new QHBoxLayout();

    // Left side: buttons
    auto* leftLayout = new QVBoxLayout();

    auto* btnRow = new QHBoxLayout();
    m_btnReady = new QPushButton("Ready", this);
    m_btnDrop = new QPushButton("Drop Game", this);
    btnRow->addWidget(m_btnReady);
    btnRow->addWidget(m_btnDrop);
    btnRow->addStretch();
    m_recordCheck = new QCheckBox("Record game", this);
    const bool recordingEnabledByDefault = CoreGetKailleraEffectiveRecordingDefault();
    extern bool n02_kaillera_recording_enabled;
    n02_kaillera_recording_enabled = recordingEnabledByDefault;
    m_recordCheck->setChecked(recordingEnabledByDefault);
    connect(m_recordCheck, &QCheckBox::toggled, this, [](bool checked) {
        extern bool n02_kaillera_recording_enabled;
        n02_kaillera_recording_enabled = checked;
    });
    btnRow->addWidget(m_recordCheck);
    leftLayout->addLayout(btnRow);

    bottomLayout->addLayout(leftLayout, 1);

    // Right side: Host group (host only)
    if (m_isHost)
    {
        m_hostGroup = new QGroupBox("Host:", this);
        auto* hostLayout = new QVBoxLayout(m_hostGroup);

        auto* fdlyLayout = new QHBoxLayout();
        fdlyLayout->addWidget(new QLabel("Frame Delay:", m_hostGroup));
        m_frameDelayCombo = new QComboBox(m_hostGroup);
        m_frameDelayCombo->addItem("Auto");
        for (int i = 1; i <= 9; i++)
        {
            m_frameDelayCombo->addItem(QString::number(i));
        }
        fdlyLayout->addWidget(m_frameDelayCombo);
        hostLayout->addLayout(fdlyLayout);

        hostLayout->addWidget(new QLabel("Connect code:", m_hostGroup));

        m_connectCodeEdit = new QLineEdit(m_hostGroup);
        m_connectCodeEdit->setReadOnly(true);
        m_connectCodeEdit->setText("{waiting}");
        hostLayout->addWidget(m_connectCodeEdit);

        m_btnCopy = new QPushButton("Copy", m_hostGroup);
        hostLayout->addWidget(m_btnCopy);

        m_enlistCheck = new QCheckBox("Show on public\ngame list", m_hostGroup);
        hostLayout->addWidget(m_enlistCheck);

        bottomLayout->addWidget(m_hostGroup);

        connect(m_btnCopy, &QPushButton::clicked, this, &KailleraP2PDialog::onCopyConnectCode);
        connect(m_enlistCheck, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked)
                enlistGame();
            else
                unenlistGame();
        });
    }

    mainLayout->addLayout(bottomLayout);

    // Connect button actions
    connect(m_btnChat, &QPushButton::clicked, this, &KailleraP2PDialog::onSendChat);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &KailleraP2PDialog::onSendChat);
    connect(m_btnReady, &QPushButton::clicked, this, &KailleraP2PDialog::onReady);
    connect(m_btnDrop, &QPushButton::clicked, this, &KailleraP2PDialog::onDrop);
}

void KailleraP2PDialog::connectSignals()
{
    auto& bridge = KailleraUIBridge::instance();

    connect(&bridge, &KailleraUIBridge::p2pChatReceived, this, &KailleraP2PDialog::onChatReceived);
    connect(&bridge, &KailleraUIBridge::p2pGameStarted, this, &KailleraP2PDialog::onGameStarted);
    connect(&bridge, &KailleraUIBridge::p2pGameEnded, this, &KailleraP2PDialog::onGameEnded);
    connect(&bridge, &KailleraUIBridge::p2pClientDropped, this, &KailleraP2PDialog::onClientDropped);
    connect(&bridge, &KailleraUIBridge::p2pDebugMessage, this, &KailleraP2PDialog::onDebug);
    connect(&bridge, &KailleraUIBridge::p2pPingUpdated, this, &KailleraP2PDialog::onPingUpdated);
    connect(&bridge, &KailleraUIBridge::p2pPeerJoined, this, &KailleraP2PDialog::onPeerJoined);
    connect(&bridge, &KailleraUIBridge::p2pPeerLeft, this, &KailleraP2PDialog::onPeerLeft);
    connect(&bridge, &KailleraUIBridge::p2pPeerInfo, this, &KailleraP2PDialog::onPeerInfo);
    connect(&bridge, &KailleraUIBridge::p2pFodippResult, this, &KailleraP2PDialog::onFodippResult);
    connect(&bridge, &KailleraUIBridge::p2pSsrvPacketReceived, this, &KailleraP2PDialog::onSsrvPacketReceived);
}

void KailleraP2PDialog::reject()
{
    if (m_stepTimer) m_stepTimer->stop();
    if (m_travTimer) m_travTimer->stop();

    // Remove from public game list
    if (m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();

    if (m_isHost && m_travHostEnabled)
        travSendClose();

    p2p_disconnect();
    p2p_core_cleanup();
    travResetState();
    QDialog::reject();
}

// ---- NAT traversal helpers ----

void KailleraP2PDialog::travResetState()
{
    m_travHostEnabled = false;
    m_travJoinEnabled = false;
    m_travCode.clear();
    m_travToken.clear();
    m_travRegAttempts = 0;
    m_travHostFallbackActive = false;
    m_travHostIpPending = false;
    m_travHostIpPort.clear();
    m_travHostRegSuspended = false;
    m_travNextRegMs = 0;
    m_travNextKeepMs = 0;
    m_travNextJoinMs = 0;
    m_travJoinDeadlineMs = 0;
    m_travJoinCode.clear();
    m_travJoinGotHost = false;
    m_travJoinToken.clear();
    m_travJoinHostIp.clear();
    m_travJoinHostPort = 0;
    m_travNextConnectMs = 0;
    m_travJoinFallbackIpPort.clear();
    m_travJoinFallbackTried = false;
    m_travJoinBusy = false;
    m_travJoinPunchAttempts = 0;
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;
    m_ssrvCopyMyIpPending = false;
}

void KailleraP2PDialog::travSendToServer(const QByteArray& msg)
{
    if (msg.isEmpty()) return;
    // NAT traversal messages must NOT include the trailing NUL byte.
    p2p_send_ssrv_packet(const_cast<char*>(msg.constData()), msg.size(),
                         const_cast<char*>(kN02TraversalHost), kN02TraversalPort);
}

void KailleraP2PDialog::travSendReg()
{
    QByteArray msg = QByteArray("N02TRAV1|REG|") +
                     QByteArray::number((quint32)GetTickCount());
    travSendToServer(msg);
    if (m_travHostEnabled)
        m_travRegAttempts++;
}

void KailleraP2PDialog::travSendKeep()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray("N02TRAV1|KEEP|") + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendClose()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray("N02TRAV1|CLOSE|") + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendJoin()
{
    if (m_travJoinCode.isEmpty()) return;
    QByteArray msg = QByteArray("N02TRAV1|JOIN|") +
                     m_travJoinCode.toUtf8() + "|" +
                     QByteArray::number((quint32)GetTickCount());
    travSendToServer(msg);
}

void KailleraP2PDialog::travPunchEndpoint(const QString& hostIp, int hostPort, const QString& token)
{
    if (hostIp.isEmpty() || hostPort <= 0) return;

    QByteArray msg = QByteArray("N02TRAV1|PUNCH|") + token.toUtf8();
    QByteArray ipBytes = hostIp.toUtf8();

    // Send 10 punch packets to open NAT mappings
    for (int i = 0; i < 10; i++)
    {
        p2p_send_ssrv_packet(const_cast<char*>(msg.constData()), msg.size(),
                             const_cast<char*>(ipBytes.constData()), hostPort);
    }
}

bool KailleraP2PDialog::travTryFallbackConnect(const QString& reason)
{
    if (m_travJoinFallbackTried) return false;

    QString fallback;
    if (!m_travJoinFallbackIpPort.isEmpty())
        fallback = m_travJoinFallbackIpPort;
    else if (!m_travJoinHostIp.isEmpty())
        fallback = m_travJoinHostIp;
    if (fallback.isEmpty()) return false;

    m_travJoinFallbackTried = true;

    QString ip;
    int port = 0;
    QByteArray fallbackBytes = fallback.toUtf8();
    if (!tryExtractIPv4AndPort(fallbackBytes, ip, port))
        return false;
    if (port <= 0) port = 27886;

    m_chat->append("<span style='color:green;'>" + timestamp() +
                   "NAT traversal: " + reason.toHtmlEscaped() +
                   ". Falling back to " + ip + ":" + QString::number(port) + "</span>");

    QByteArray ipBytes = ip.toUtf8();
    if (!p2p_core_connect(ipBytes.data(), port))
    {
        m_chat->append("<span style='color:red;'>" + timestamp() +
                       "NAT traversal: fallback connect failed</span>");
        return false;
    }
    return true;
}

void KailleraP2PDialog::updateHostCodeUI()
{
    if (!m_isHost || !m_connectCodeEdit) return;

    bool showCode = m_isHost && !m_travHostRegSuspended;
    if (m_hostGroup) m_hostGroup->setVisible(showCode);
    if (!showCode) return;

    if (!m_travCode.isEmpty())
    {
        m_connectCodeEdit->setText(m_travCode);
        if (m_btnCopy) m_btnCopy->setEnabled(true);
    }
    else if (!m_travHostIpPort.isEmpty())
    {
        m_connectCodeEdit->setText(m_travHostIpPort);
        if (m_btnCopy) m_btnCopy->setEnabled(true);
    }
    else if (m_travHostIpPending)
    {
        m_connectCodeEdit->setText("(checking ip)");
        if (m_btnCopy) m_btnCopy->setEnabled(false);
    }
    else
    {
        m_connectCodeEdit->setText("(waiting)");
        if (m_btnCopy) m_btnCopy->setEnabled(false);
    }
}

void KailleraP2PDialog::ssrvSend(const QByteArray& cmd)
{
    // Send to super-server (SSRV). Includes trailing NUL byte per ssrv protocol.
    int sendLen = cmd.size() + 1;
    p2p_send_ssrv_packet(const_cast<char*>(cmd.constData()), sendLen,
                         const_cast<char*>(kSsrvHost), kSsrvPort);
}

void KailleraP2PDialog::ssrvWhatIsMyIp()
{
    m_ssrvCopyMyIpPending = true;
    ssrvSend("WHATISMYIP");
}

QString KailleraP2PDialog::buildEnlistAppName()
{
    extern char APP[128];
    QString app = QString::fromUtf8(APP);

    // Append traversal code if hosting by code
    if (m_isHost && m_travHostEnabled && !m_travCode.isEmpty())
    {
        app += " {CC:" + m_travCode + "}";
    }
    return app;
}

void KailleraP2PDialog::enlistGame()
{
    QString app = buildEnlistAppName();
    int port = p2p_core_get_port();

    // Use ENLISP (with port) when hosting by code or on non-default port
    if (m_travHostEnabled || port != 27886)
    {
        QByteArray msg = QString("ENLISP %1|%2|%3|%4")
            .arg(m_gameName, app, m_username)
            .arg(port).toUtf8();
        ssrvSend(msg);
    }
    else
    {
        QByteArray msg = QString("ENLIST %1|%2|%3")
            .arg(m_gameName, app, m_username).toUtf8();
        ssrvSend(msg);
    }
}

void KailleraP2PDialog::unenlistGame()
{
    ssrvSend("UNENLIST");
}

// ---- SSRV packet handler (NAT traversal + super-server responses) ----

void KailleraP2PDialog::onSsrvPacketReceived(QByteArray cmd, QByteArray saddr)
{
    if (cmd.isEmpty()) return;

    // Null-terminate for safe string operations
    QByteArray cmdBuf = cmd;
    if (cmdBuf.size() > 0 && cmdBuf[cmdBuf.size() - 1] != '\0')
        cmdBuf.append('\0');

    const char* cmdStr = cmdBuf.constData();

    // ---- NAT traversal (N02TRAV1) ----
    if (strncmp(cmdStr, "N02TRAV1|", 9) == 0)
    {
        // Split the message by '|'
        QByteArray parseBuf = cmdBuf;
        char* parts[8] = { nullptr };
        int partCount = 0;
        parts[partCount++] = parseBuf.data();
        for (char* p = parseBuf.data(); *p && partCount < 8; p++)
        {
            if (*p == '|')
            {
                *p = 0;
                parts[partCount++] = p + 1;
            }
        }

        if (partCount < 2) return;
        const char* type = parts[1];

        // REGOK: Registration succeeded — we got a traversal code.
        if (strcmp(type, "REGOK") == 0 && partCount >= 6)
        {
            if (!m_travHostEnabled) return;

            m_travCode = QString::fromUtf8(parts[2]);
            m_travToken = QString::fromUtf8(parts[3]);
            m_travRegAttempts = 0;
            m_travHostIpPending = false;
            m_travHostIpPort.clear();

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Received connect code: " + m_travCode.toHtmlEscaped() + "</span>");

            // Auto-copy to clipboard
            QApplication::clipboard()->setText(m_travCode);
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Copied connect code to clipboard</span>");

            updateHostCodeUI();

            // Start keepalive cadence
            m_travNextKeepMs = 0;

            // Enlist on public game list if checkbox is checked
            if (m_enlistCheck && m_enlistCheck->isChecked())
                enlistGame();

            return;
        }

        // HOST: Joiner received the host's endpoint from the traversal server.
        if (strcmp(type, "HOST") == 0 && partCount >= 5)
        {
            if (!m_travJoinEnabled) return;

            QString token = QString::fromUtf8(parts[2]);
            QString hostIp = QString::fromUtf8(parts[3]);
            int hostPort = atoi(parts[4]);

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal: got host " + hostIp + ":" + QString::number(hostPort) + "</span>");

            m_travJoinToken = token;
            m_travJoinHostIp = hostIp;
            m_travJoinHostPort = hostPort;
            m_travJoinGotHost = true;

            if (m_travJoinFallbackIpPort.isEmpty())
            {
                m_travJoinFallbackIpPort = hostIp;
                m_travJoinFallbackTried = false;
            }

            // Stop asking the server
            m_travNextJoinMs = 0;

            // Try connecting immediately
            m_travJoinPunchAttempts++;
            travPunchEndpoint(m_travJoinHostIp, m_travJoinHostPort, m_travJoinToken);

            QByteArray ipBytes = m_travJoinHostIp.toUtf8();
            if (!p2p_core_connect(ipBytes.data(), m_travJoinHostPort))
            {
                travTryFallbackConnect("connect failed");
            }
            if (m_travJoinPunchAttempts >= 3)
            {
                travTryFallbackConnect("trying direct IP/port");
            }
            return;
        }

        // PEER: Host received a peer's endpoint — need to punch their NAT.
        if (strcmp(type, "PEER") == 0 && partCount >= 5)
        {
            if (!m_travHostEnabled) return;

            QString token = QString::fromUtf8(parts[2]);
            QString peerIp = QString::fromUtf8(parts[3]);
            int peerPort = atoi(parts[4]);

            if (!m_travToken.isEmpty() && token != m_travToken) return;

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal: peer " + peerIp + ":" + QString::number(peerPort) + "</span>");

            m_travHostPeerIp = peerIp;
            m_travHostPeerPort = peerPort;
            m_travHostPeerDeadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
            m_travNextHostPunchMs = 0;

            travPunchEndpoint(peerIp, peerPort, token);
            return;
        }

        // ERR: Server error
        if (strcmp(type, "ERR") == 0 && partCount >= 3)
        {
            if (m_travJoinEnabled && strcmp(parts[2], "BUSY") == 0)
            {
                m_travJoinBusy = true;
                m_travJoinDeadlineMs = QDateTime::currentMSecsSinceEpoch();
                m_travNextJoinMs = 0;
                return;
            }
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal error: " + QString::fromUtf8(parts[2]).toHtmlEscaped() + "</span>");
            return;
        }

        // OK: Acknowledgement
        if (strcmp(type, "OK") == 0) return;

        // PUNCH: No-op payload for NAT hole punching
        if (strcmp(type, "PUNCH") == 0) return;

        return;
    }

    // ---- PINGRQ: Super-server ping request ----
    if (strncmp(cmdStr, "PINGRQ", 6) == 0)
    {
        if (!saddr.isEmpty())
        {
            p2p_send_ssrv_packet(const_cast<char*>("xxxxxxxxxx"), 11, saddr.data());
        }
        return;
    }

    // ---- MSG: Ignore ----
    if (strncmp(cmdStr, "MSG", 3) == 0) return;

    // ---- WHATISMYIP response handling ----
    if (m_ssrvCopyMyIpPending)
    {
        QString ip;
        int port = 0;
        if (tryExtractIPv4AndPort(cmd, ip, port))
        {
            if (port <= 0) port = p2p_core_get_port();
            if (port > 0)
            {
                m_travHostIpPort = ip + ":" + QString::number(port);
                m_travHostIpPending = false;
                updateHostCodeUI();

                if (m_travHostFallbackActive)
                {
                    m_chat->append("<span style='color:green;'>" + timestamp() +
                                   "Your IP address is: " + m_travHostIpPort + "</span>");
                }
            }
        }
        m_ssrvCopyMyIpPending = false;
    }
}

// ---- Signal handlers ----

void KailleraP2PDialog::onChatReceived(QString nick, QString message)
{
    m_chat->append("<b>" + nick.toHtmlEscaped() + ":</b> " + message.toHtmlEscaped());
}

void KailleraP2PDialog::onGameStarted(QString game, int player, int maxPlayers)
{
    (void)player;
    (void)maxPlayers;
    m_chat->append("<span style='color:green;'>" + timestamp() + "Game started: " + game.toHtmlEscaped() + "</span>");
}

void KailleraP2PDialog::onGameEnded()
{
    CoreMarkKailleraGameInactive();
    CoreStopEmulation();
    m_ready = false;
    m_chat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "Game ended.</span>");
}

void KailleraP2PDialog::onClientDropped(QString nick, int player)
{
    (void)player;
    m_chat->append("<span style='color:red;'>" + timestamp() + nick.toHtmlEscaped() + " dropped.</span>");
}

void KailleraP2PDialog::onDebug(QString message)
{
    m_chat->append("<span style='color:green;'>" + message.toHtmlEscaped() + "</span>");
}

void KailleraP2PDialog::onPingUpdated(int ping)
{
    (void)ping;
}

void KailleraP2PDialog::onPeerJoined()
{
    m_chat->append("<span style='color:green;'>" + timestamp() + "Peer connected.</span>");

    // If hosting by code, release the code once the peer connects.
    if (m_isHost && m_travHostEnabled && !m_travHostRegSuspended)
    {
        travSendClose();

        m_travHostRegSuspended = true;
        m_travCode.clear();
        m_travToken.clear();
        m_travRegAttempts = 0;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;
        updateHostCodeUI();
    }

    // Remove from public game list while peer is connected
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();
}

void KailleraP2PDialog::onPeerLeft()
{
    m_chat->append("<span style='color:red;'>" + timestamp() + "Peer disconnected.</span>");
    m_ready = false;

    // If hosting by code, resume registration to mint a fresh code.
    if (m_isHost && m_travHostEnabled)
    {
        m_travHostRegSuspended = false;
        m_travCode.clear();
        m_travToken.clear();
        m_travRegAttempts = 0;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;
        updateHostCodeUI();
    }

    // Clear peer punching state (always, regardless of trav mode)
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;

    // Re-enlist on public game list now that we're waiting for a new peer
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        enlistGame();
}

void KailleraP2PDialog::onPeerInfo(QString name, QString app)
{
    m_chat->append("<span style='color:green;'>" + timestamp() + "Peer: " +
                   name.toHtmlEscaped() + " (" + app.toHtmlEscaped() + ")</span>");
}

void KailleraP2PDialog::onFodippResult(QString host)
{
    // FODIPP result serves as an additional fallback for IP display
    if (m_isHost && m_travHostIpPort.isEmpty())
    {
        int port = p2p_core_get_port();
        m_travHostIpPort = host + ":" + QString::number(port);
        m_travHostIpPending = false;
        updateHostCodeUI();
    }
    m_chat->append("<span style='color:green;'>" + timestamp() + "External IP: " + host.toHtmlEscaped() + "</span>");
}

// ---- Button actions ----

void KailleraP2PDialog::onSendChat()
{
    QString text = m_chatInput->text().trimmed();
    if (text.isEmpty()) return;

    QByteArray utf8 = text.toUtf8();
    p2p_send_chat(utf8.data());
    m_chatInput->clear();
}

void KailleraP2PDialog::onReady()
{
    m_ready = !m_ready;

    // Update the selected delay in UIBridge before setting ready
    if (m_isHost && m_frameDelayCombo)
    {
        KailleraUIBridge::instance().setSelectedDelay(m_frameDelayCombo->currentIndex());
    }

    p2p_set_ready(m_ready);

    if (m_ready)
    {
        m_chat->append(timestamp() + "Ready!");
    }
    else
    {
        m_chat->append(timestamp() + "Not ready.");
    }
}

void KailleraP2PDialog::onDrop()
{
    p2p_drop_game();
}

void KailleraP2PDialog::onCopyConnectCode()
{
    if (!m_connectCodeEdit) return;

    // Copy the best available code/address
    if (!m_travCode.isEmpty())
    {
        QApplication::clipboard()->setText(m_travCode);
        m_chat->append(timestamp() + "Copied connect code to clipboard");
    }
    else if (!m_travHostIpPort.isEmpty())
    {
        QApplication::clipboard()->setText(m_travHostIpPort);
        m_chat->append(timestamp() + "Copied " + m_travHostIpPort + " to clipboard");
    }
}

void KailleraP2PDialog::onStepTimer()
{
    // Only poll network from the UI thread when NOT in game.
    // During game (state 2), the emulation thread handles all network I/O
    // inside p2p_modify_play_values(). Calling p2p_step() here would race
    // with the emulation thread and steal packets meant for game sync.
    if (!n02::isGameRunning())
        p2p_step();
    n02::processStateMachineStep();
}

// ---- NAT traversal housekeeping (1-second timer) ----

void KailleraP2PDialog::onTravTimer()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_travTimerStep++;

    // ---- HOST: NAT traversal registration & keepalive ----
    if (m_isHost && m_travHostEnabled)
    {
        if (!m_travHostRegSuspended)
        {
            if (m_travToken.isEmpty())
            {
                // No token yet — keep retrying registration until REGOK
                if (m_travNextRegMs == 0 || now >= m_travNextRegMs)
                {
                    if (m_travRegAttempts >= 4)
                    {
                        // Fallback to IP-based hosting
                        m_travHostEnabled = false;
                        m_travHostFallbackActive = true;
                        m_travNextRegMs = 0;
                        m_travNextKeepMs = 0;
                        m_travHostIpPending = true;
                        m_travHostIpPort.clear();
                        updateHostCodeUI();

                        m_chat->append("<span style='color:green;'>" + timestamp() +
                                       "Unable to contact NAT server, hosting by IP. "
                                       "You may need to manually port forward.</span>");
                        ssrvWhatIsMyIp();
                    }
                    else
                    {
                        travSendReg();
                        m_travNextRegMs = now + 2000;
                    }
                }
            }
            else
            {
                // Have token — send keepalive
                if (m_travNextKeepMs == 0 || now >= m_travNextKeepMs)
                {
                    travSendKeep();
                    m_travNextKeepMs = now + 10000;
                }
            }
        }

        // While waiting for the peer's LOGN_REQ, keep punching their endpoint
        if (!p2p_is_connected() &&
            !m_travHostPeerIp.isEmpty() &&
            m_travHostPeerPort > 0 &&
            m_travHostPeerDeadlineMs != 0 &&
            now < m_travHostPeerDeadlineMs)
        {
            if (m_travNextHostPunchMs == 0 || now >= m_travNextHostPunchMs)
            {
                travPunchEndpoint(m_travHostPeerIp, m_travHostPeerPort, m_travToken);
                m_travNextHostPunchMs = now + 1000;
            }
        }
    }

    // ---- JOIN: traversal code lookup & connection retries ----
    if (!m_isHost && m_travJoinEnabled && !p2p_is_connected())
    {
        if (m_travJoinDeadlineMs != 0 && now >= m_travJoinDeadlineMs)
        {
            // Timed out
            if (m_travJoinBusy)
            {
                m_chat->append("<span style='color:red;'>" + timestamp() +
                               "NAT traversal: host is busy. Please wait and try again.</span>");
            }
            else
            {
                m_chat->append("<span style='color:red;'>" + timestamp() +
                               "NAT traversal: timed out (try direct IP/port-forwarding or server mode)</span>");
            }
            m_travJoinEnabled = false;
            if (!m_travJoinBusy)
            {
                travTryFallbackConnect("timed out");
            }
            m_travJoinBusy = false;
        }
        else if (!m_travJoinGotHost)
        {
            // Keep asking for the host's address
            if (m_travNextJoinMs == 0 || now >= m_travNextJoinMs)
            {
                travSendJoin();
                m_travNextJoinMs = now + 3000;
            }
        }
        else
        {
            // Got host endpoint — retry punch + connect
            if (m_travNextConnectMs == 0 || now >= m_travNextConnectMs)
            {
                m_travJoinPunchAttempts++;
                travPunchEndpoint(m_travJoinHostIp, m_travJoinHostPort, m_travJoinToken);

                QByteArray ipBytes = m_travJoinHostIp.toUtf8();
                p2p_core_connect(ipBytes.data(), m_travJoinHostPort);
                m_travNextConnectMs = now + 1000;

                if (m_travJoinPunchAttempts >= 3)
                {
                    travTryFallbackConnect("trying direct IP/port");
                }
            }
        }
    }
    else if (!m_isHost && m_travJoinEnabled && p2p_is_connected())
    {
        // Connected — stop traversal retries
        m_travJoinEnabled = false;
        m_travJoinDeadlineMs = 0;
    }

    // ---- Periodic re-enlist on public game list (every 30s while waiting) ----
    if (m_travTimerStep % 30 == 0 && !p2p_is_connected() &&
        m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
    {
        enlistGame();
    }
}

#endif // NETPLAY
