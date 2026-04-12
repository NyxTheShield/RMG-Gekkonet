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
#include "KailleraTraversalConfig.hpp"

#ifdef _WIN32

#include "../../KailleraUIBridge.hpp"

#include <RMG-Core/Emulation.hpp>
#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Settings.hpp>

#include "core/p2p_core.h"
#include "n02_client.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QIcon>
#include <QFrame>
#include <QListView>

#include <algorithm>
#include <cstring>

static const char* kSsrvHost = "kaillerareborn.2manygames.fr";
static const int kSsrvPort = 27887;

static QString timestamp()
{
    return QDateTime::currentDateTime().toString("[h:mm AP] ");
}

static QString formatTraversalCode(const QString& prefix, int number)
{
    return prefix + "@" + QString::number(number);
}

static QIcon themedP2PIcon(const QString& iconName)
{
    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    return QIcon(QString(":/icons/%1/svg/%2.svg")
        .arg(darkTheme ? "white" : "black", iconName));
}

static QString buildP2PStyleSheet(const QString& theme)
{
    if (theme != "Modern")
    {
        return QString();
    }

    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    const QString comboArrowIcon = QString(":/icons/%1/svg/arrow-down-s-line.svg")
        .arg(darkTheme ? "white" : "black");

    return QString(
        "QDialog#KailleraP2PDialog {"
        "  background-color: palette(window);"
        "}"
        "QLabel#KailleraP2PGameBanner {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "  padding: 6px 10px;"
        "  font-weight: 500;"
        "}"
        "QLabel#KailleraP2PStatusLabel {"
        "  color: palette(text);"
        "  padding: 0 2px;"
        "  font-weight: 600;"
        "}"
        "QTextBrowser#KailleraP2PSurface {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "  padding: 6px;"
        "}"
        "QLineEdit#KailleraP2PInput {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 5px 8px;"
        "  min-height: 24px;"
        "}"
        "QLineEdit#KailleraP2PInput:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QComboBox#KailleraP2PCombo {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  background-color: palette(base);"
        "  padding: 4px 8px;"
        "  min-height: 24px;"
        "}"
        "QComboBox#KailleraP2PCombo:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QComboBox#KailleraP2PCombo::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: top right;"
        "  width: 24px;"
        "  border: none;"
        "  border-left: 1px solid palette(mid);"
        "  border-top-right-radius: 7px;"
        "  border-bottom-right-radius: 7px;"
        "  background-color: transparent;"
        "  margin: 1px;"
        "}"
        "QComboBox#KailleraP2PCombo::down-arrow {"
        "  image: url(%1);"
        "  width: 12px;"
        "  height: 12px;"
        "}"
        "QComboBox#KailleraP2PCombo QAbstractItemView {"
        "  border: 1px solid palette(mid);"
        "  background-color: palette(base);"
        "  selection-background-color: palette(highlight);"
        "  selection-color: palette(highlighted-text);"
        "  outline: none;"
        "  padding: 0px;"
        "  margin: 0px;"
        "}"
        "QComboBox#KailleraP2PCombo QAbstractItemView::item {"
        "  padding: 0px 8px;"
        "  min-height: 18px;"
        "  margin: 0px;"
        "}"
        "QGroupBox#KailleraP2PGroup {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 10px;"
        "  background-color: palette(base);"
        "  margin-top: 10px;"
        "  padding-top: 6px;"
        "}"
        "QGroupBox#KailleraP2PGroup::title {"
        "  subcontrol-origin: margin;"
        "  left: 10px;"
        "  padding: 0 4px;"
        "  font-weight: 600;"
        "}"
        "QWidget#KailleraChatComposer {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
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
        "  border-radius: 6px;"
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
        "QPushButton#KailleraP2PPrimaryButton {"
        "  border: 1px solid #0066b4;"
        "  border-radius: 7px;"
        "  min-height: 24px;"
        "  padding: 4px 12px;"
        "  font-weight: 700;"
        "  color: white;"
        "  background-color: #0078D7;"
        "}"
        "QPushButton#KailleraP2PPrimaryButton:hover {"
        "  background-color: #1584dd;"
        "}"
        "QPushButton#KailleraP2PPrimaryButton:pressed,"
        "QPushButton#KailleraP2PPrimaryButton:checked {"
        "  background-color: #0063b1;"
        "}"
        "QPushButton#KailleraP2PSecondaryButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 7px;"
        "  min-height: 24px;"
        "  padding: 4px 12px;"
        "  background-color: palette(window);"
        "  font-weight: 600;"
        "}"
        "QPushButton#KailleraP2PSecondaryButton:hover {"
        "  border-color: palette(dark);"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraP2PSecondaryButton:pressed {"
        "  border-color: palette(shadow);"
        "  background-color: palette(mid);"
        "  padding-top: 5px;"
        "  padding-bottom: 3px;"
        "}"
        "QPushButton#KailleraP2PIconButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  padding: 0px;"
        "  background-color: palette(button);"
        "}"
        "QPushButton#KailleraP2PIconButton:hover {"
        "  background-color: palette(light);"
        "}"
        "QPushButton#KailleraP2PIconButton:pressed {"
        "  background-color: palette(midlight);"
        "}"
    ).arg(comboArrowIcon);
}

static void configureP2PComboPopup(QComboBox* combo, const QString& theme)
{
    if (combo == nullptr || theme != "Modern")
    {
        return;
    }

    auto* popupView = new QListView(combo);
    popupView->setUniformItemSizes(true);
    popupView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    popupView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const QPalette appPalette = QApplication::palette();
    const QColor windowColor = appPalette.window().color();
    const QColor baseColor = appPalette.base().color();
    const bool darkTheme = windowColor.value() < 128;
    const QColor popupColor = darkTheme
        ? baseColor.darker(106)
        : baseColor.darker(108);
    const QColor borderColor = darkTheme
        ? windowColor.lighter(142)
        : windowColor.darker(132);

    popupView->setStyleSheet(QString(
        "QListView {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  outline: none;"
        "  padding: 2px 0px;"
        "}"
        "QListView::item {"
        "  padding: 2px 8px;"
        "  min-height: 18px;"
        "  margin: 0px;"
        "}"
        "QListView::item:selected {"
        "  background-color: palette(highlight);"
        "  color: palette(highlighted-text);"
        "}").arg(popupColor.name(QColor::HexRgb), borderColor.name(QColor::HexRgb)));

    combo->setView(popupView);
}

// Check if a string looks like a NAT traversal code rather than an IP address.
static QString normalizeTraversalCode(const QString& input)
{
    QString s = input.trimmed().toUpper();
    s.remove(' ');
    if (s.isEmpty()) return QString();

    for (const QChar& ch : s)
    {
        if (ch == '.' || ch == ':' || ch == '/') return QString();
    }

    if (s.size() < 4) return QString();

    int prefixLength = 0;
    while (prefixLength < s.size() && prefixLength < 4 && s[prefixLength].isLetter())
    {
        ++prefixLength;
    }
    if (prefixLength < 3) return QString();
    if (prefixLength < s.size() && s[prefixLength].isLetter()) return QString();

    const QString prefix = s.left(prefixLength);
    QString digits = s.mid(prefixLength);
    if (digits.startsWith('@') || digits.startsWith('#') || digits.startsWith('-') || digits.startsWith('_'))
        digits.remove(0, 1);
    if (digits.isEmpty()) return QString();
    if (digits.size() > 3) return QString();

    for (const QChar& ch : digits)
    {
        if (!ch.isDigit()) return QString();
    }

    bool ok = false;
    const int number = digits.toInt(&ok);
    if (!ok || number < 0) return QString();

    return formatTraversalCode(prefix, number);
}

// Matches n02-rmg's LooksLikeTraversalCode() intent, but accepts legacy separators and leading zeroes.
static bool looksLikeTraversalCode(const QString& s)
{
    return !normalizeTraversalCode(s).isEmpty();
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
    : QDialog(parent), m_isHost(isHost), m_gameName(gameName), m_username(username)
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
    travLoadIdentity();

    if (m_isHost)
    {
        // Host always uses NAT traversal (host by code)
        m_travHostEnabled = true;
        if (!m_travToken.isEmpty() && !m_travCode.isEmpty())
        {
            travSendHostOpen();
        }
        else
        {
            travSendClaimAuto();
        }
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
        m_travJoinCode = normalizeTraversalCode(joinCode);
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
    setObjectName("KailleraP2PDialog");
    setWindowTitle(m_isHost ? "Hosting P2P" : "P2P Game");
    setMinimumSize(520, 420);
    resize(560, 480);

    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    setStyleSheet(buildP2PStyleSheet(theme));

    auto* mainLayout = new QVBoxLayout(this);

    // Game label at top
    m_gameLabel = new QLabel(this);
    m_gameLabel->setObjectName("KailleraP2PGameBanner");
    m_gameLabel->setText("Game: " + m_gameName);
    m_gameLabel->setFrameStyle(QFrame::Box | QFrame::Sunken);
    mainLayout->addWidget(m_gameLabel);

    // Chat / status area (takes most space)
    m_chat = new QTextBrowser(this);
    m_chat->setObjectName("KailleraP2PSurface");
    m_chat->setOpenExternalLinks(true);
    m_chat->document()->setMaximumBlockCount(1000);
    mainLayout->addWidget(m_chat, 1);

    // Chat input row
    auto* chatInputLayout = new QHBoxLayout();
    chatInputLayout->setContentsMargins(0, 0, 0, 0);

    auto* chatComposer = new QWidget(this);
    chatComposer->setObjectName("KailleraChatComposer");
    chatComposer->setFixedHeight(24);
    auto* chatComposerLayout = new QHBoxLayout(chatComposer);
    chatComposerLayout->setContentsMargins(10, 2, 4, 2);
    chatComposerLayout->setSpacing(4);

    m_chatInput = new QLineEdit(chatComposer);
    m_chatInput->setPlaceholderText("Type a message...");
    m_chatInput->setClearButtonEnabled(true);
    m_chatInput->setObjectName("KailleraChatComposerInput");
    m_chatInput->setFrame(false);

    m_btnChat = new QPushButton(chatComposer);
    m_btnChat->setObjectName("KailleraChatComposerSendButton");
    m_btnChat->setToolTip("Send message");
    m_btnChat->setText("");
    m_btnChat->setIcon(themedP2PIcon("play-line"));
    m_btnChat->setIconSize(QSize(13, 13));

    chatComposerLayout->addWidget(m_chatInput);
    chatComposerLayout->addWidget(m_btnChat, 0, Qt::AlignVCenter);
    chatInputLayout->addWidget(chatComposer, 1);
    mainLayout->addLayout(chatInputLayout);

    // Button row: Ready, Drop Game, Record game checkbox  |  Host group
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);
    bottomLayout->setAlignment(Qt::AlignTop);

    // Left side: buttons
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, m_isHost ? 12 : 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);
    m_btnReady = new QPushButton("Ready", this);
    m_btnReady->setCheckable(true);
    m_btnReady->setObjectName("KailleraP2PPrimaryButton");
    m_btnDrop = new QPushButton("Drop Game", this);
    m_btnDrop->setObjectName("KailleraP2PSecondaryButton");
    btnRow->addWidget(m_btnReady);
    btnRow->addWidget(m_btnDrop);
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
    btnRow->addStretch();
    leftLayout->addLayout(btnRow);

    bottomLayout->addWidget(leftWidget, 0, Qt::AlignTop);
    bottomLayout->addStretch(1);

    // Right side: Host group (host only)
    if (m_isHost)
    {
        m_hostGroup = new QGroupBox("Host:", this);
        m_hostGroup->setObjectName("KailleraP2PGroup");
        auto* hostLayout = new QVBoxLayout(m_hostGroup);
        hostLayout->setContentsMargins(9, 7, 9, 9);
        hostLayout->setSpacing(8);

        auto* fdlyLayout = new QHBoxLayout();
        fdlyLayout->setContentsMargins(0, 0, 0, 0);
        fdlyLayout->setSpacing(6);
        auto* frameDelayLabel = new QLabel("Frame Delay:", m_hostGroup);
        fdlyLayout->addWidget(frameDelayLabel);
        m_frameDelayCombo = new QComboBox(m_hostGroup);
        m_frameDelayCombo->setObjectName("KailleraP2PCombo");
        m_frameDelayCombo->setMinimumWidth(140);
        m_frameDelayCombo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
        configureP2PComboPopup(m_frameDelayCombo, theme);
        m_frameDelayCombo->addItem("Auto");
        m_frameDelayCombo->addItem("1 frame (0-33ms)");
        m_frameDelayCombo->addItem("2 frames (34-67ms)");
        m_frameDelayCombo->addItem("3 frames (68-99ms)");
        m_frameDelayCombo->addItem("4 frames (100-133ms)");
        m_frameDelayCombo->addItem("5 frames (134-167ms)");
        m_frameDelayCombo->addItem("6 frames (168-199ms)");
        m_frameDelayCombo->addItem("7 frames (200-233ms)");
        m_frameDelayCombo->addItem("8 frames (234-267ms)");
        m_frameDelayCombo->addItem("9 frames (268+ms)");
        KailleraUIBridge::instance().setSelectedDelay(m_frameDelayCombo->currentIndex());
        connect(m_frameDelayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [](int index) {
            KailleraUIBridge::instance().setSelectedDelay(index);
        });
        fdlyLayout->addWidget(m_frameDelayCombo);
        hostLayout->addLayout(fdlyLayout);

        const QMargins hostMargins = hostLayout->contentsMargins();
        const int hostMinWidth =
            hostMargins.left() +
            frameDelayLabel->sizeHint().width() +
            fdlyLayout->spacing() +
            m_frameDelayCombo->minimumWidth() +
            hostMargins.right();
        m_hostGroup->setMinimumWidth(hostMinWidth);

        hostLayout->addWidget(new QLabel("Connect code:", m_hostGroup));

        auto* codeRow = new QHBoxLayout();
        codeRow->setContentsMargins(0, 0, 0, 0);
        codeRow->setSpacing(0);

        m_connectCodeEdit = new QLineEdit(m_hostGroup);
        m_connectCodeEdit->setObjectName("KailleraP2PInput");
        m_connectCodeEdit->setReadOnly(true);
        m_connectCodeEdit->setText("{waiting}");
        m_copyAction = m_connectCodeEdit->addAction(themedP2PIcon("copy-line"), QLineEdit::TrailingPosition);
        m_copyAction->setToolTip("Copy to clipboard");
        connect(m_copyAction, &QAction::triggered, this, &KailleraP2PDialog::onCopyConnectCode);
        codeRow->addWidget(m_connectCodeEdit, 1);
        hostLayout->addLayout(codeRow);

        m_enlistCheck = new QCheckBox("Show on public list", m_hostGroup);
        hostLayout->addWidget(m_enlistCheck);

        bottomLayout->addWidget(m_hostGroup, 0, Qt::AlignTop);
        connect(m_enlistCheck, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked)
                enlistGame();
            else
                unenlistGame();
        });
    }

    mainLayout->addLayout(bottomLayout);

    auto* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);
    m_pingLabel = new QLabel("Ping: -- ms", this);
    m_pingLabel->setObjectName("KailleraP2PStatusLabel");
    statusLayout->addWidget(m_pingLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);

    // Connect button actions
    connect(m_btnChat, &QPushButton::clicked, this, &KailleraP2PDialog::onSendChat);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &KailleraP2PDialog::onSendChat);
    connect(m_btnReady, &QPushButton::clicked, this, &KailleraP2PDialog::onReady);
    connect(m_btnDrop, &QPushButton::clicked, this, &KailleraP2PDialog::onDrop);

    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, [this]() {
        if (m_copyAction == nullptr)
        {
            return;
        }

        m_copyAction->setIcon(themedP2PIcon("copy-line"));
        m_copyAction->setToolTip("Copy to clipboard");
    });
}

void KailleraP2PDialog::connectSignals()
{
    auto& bridge = KailleraUIBridge::instance();
    constexpr Qt::ConnectionType kUiCallbackConnection = Qt::QueuedConnection;

    connect(&bridge, &KailleraUIBridge::p2pChatReceived, this, &KailleraP2PDialog::onChatReceived,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pGameStarted, this, &KailleraP2PDialog::onGameStarted,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pGameEnded, this, &KailleraP2PDialog::onGameEnded,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pClientDropped, this, &KailleraP2PDialog::onClientDropped,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pDebugMessage, this, &KailleraP2PDialog::onDebug,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPingUpdated, this, &KailleraP2PDialog::onPingUpdated,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerJoined, this, &KailleraP2PDialog::onPeerJoined,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerLeft, this, &KailleraP2PDialog::onPeerLeft,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pPeerInfo, this, &KailleraP2PDialog::onPeerInfo,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pFodippResult, this, &KailleraP2PDialog::onFodippResult,
            kUiCallbackConnection);
    connect(&bridge, &KailleraUIBridge::p2pSsrvPacketReceived, this, &KailleraP2PDialog::onSsrvPacketReceived,
            kUiCallbackConnection);
}

void KailleraP2PDialog::reject()
{
    if (m_stepTimer) m_stepTimer->stop();
    if (m_travTimer) m_travTimer->stop();
    m_ready = false;
    if (m_btnReady) m_btnReady->setChecked(false);

    // Remove from public game list
    if (m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();

    if (m_isHost && m_travHostEnabled)
        travSendHostClose();

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
    m_travLiveToken.clear();
    m_travRegAttempts = 0;
    m_travHostSessionSuspended = false;
    m_travHostFallbackActive = false;
    m_travHostIpPending = false;
    m_travHostIpPort.clear();
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

void KailleraP2PDialog::travLoadIdentity()
{
    const QString storedCode =
        normalizeTraversalCode(QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_P2PStaticCode)));
    const QString storedToken =
        QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken)).trimmed();

    if (!storedCode.isEmpty() && !storedToken.isEmpty())
    {
        m_travCode = storedCode;
        m_travToken = storedToken;
    }
    else
    {
        m_travCode.clear();
        m_travToken.clear();
    }
}

void KailleraP2PDialog::travSaveIdentity() const
{
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCode, m_travCode.toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken, m_travToken.toStdString());
    CoreSettingsSave();
}

void KailleraP2PDialog::travClearIdentity()
{
    m_travCode.clear();
    m_travToken.clear();
    m_travLiveToken.clear();

    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCode, std::string(""));
    CoreSettingsSetValue(SettingsID::Kaillera_P2PStaticCodeOwnerToken, std::string(""));
    CoreSettingsSave();
}

void KailleraP2PDialog::travSendToServer(const QByteArray& msg)
{
    if (msg.isEmpty()) return;
    // NAT traversal messages must NOT include the trailing NUL byte.
    p2p_send_ssrv_packet(const_cast<char*>(msg.constData()), msg.size(),
                         const_cast<char*>(kN02TraversalHost), kN02TraversalPort);
}

void KailleraP2PDialog::travSendClaimAuto()
{
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|CLAIM|AUTO";
    travSendToServer(msg);
    if (m_travHostEnabled)
        m_travRegAttempts++;
}

void KailleraP2PDialog::travSendClaimAck()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|CLAIMACK|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostOpen()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTOPEN|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostKeep()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTKEEP|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendHostClose()
{
    if (m_travToken.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|HOSTCLOSE|" + m_travToken.toUtf8();
    travSendToServer(msg);
}

void KailleraP2PDialog::travSendJoin()
{
    if (m_travJoinCode.isEmpty()) return;
    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|JOIN|" +
                     m_travJoinCode.toUtf8() + "|" +
                     QByteArray::number((quint32)GetTickCount());
    travSendToServer(msg);
}

void KailleraP2PDialog::travPunchEndpoint(const QString& hostIp, int hostPort, const QString& token)
{
    if (hostIp.isEmpty() || hostPort <= 0) return;

    QByteArray msg = QByteArray(kN02TraversalProtocol) + "|PUNCH|" + token.toUtf8();
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

    if (m_hostGroup) m_hostGroup->setVisible(true);
    const bool codeActive = !m_travHostSessionSuspended;
    if (m_connectCodeEdit) m_connectCodeEdit->setEnabled(codeActive);
    if (m_copyAction) m_copyAction->setEnabled(codeActive);
    if (m_enlistCheck) m_enlistCheck->setEnabled(codeActive);

    if (!codeActive)
    {
        m_connectCodeEdit->setText("(peer connected)");
        return;
    }

    if (!m_travCode.isEmpty())
    {
        m_connectCodeEdit->setText(m_travCode);
    }
    else if (!m_travHostIpPort.isEmpty())
    {
        m_connectCodeEdit->setText(m_travHostIpPort);
    }
    else if (m_travHostIpPending)
    {
        m_connectCodeEdit->setText("(checking ip)");
        if (m_copyAction) m_copyAction->setEnabled(false);
    }
    else
    {
        m_connectCodeEdit->setText("(waiting)");
        if (m_copyAction) m_copyAction->setEnabled(false);
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

    // ---- NAT traversal ----
    const QByteArray traversalPrefix = QByteArray(kN02TraversalProtocol) + "|";
    if (strncmp(cmdStr, traversalPrefix.constData(), traversalPrefix.size()) == 0)
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

        if (strcmp(type, "CLAIMOK") == 0 && partCount >= 4)
        {
            if (!m_travHostEnabled) return;

            m_travCode = normalizeTraversalCode(QString::fromUtf8(parts[2]));
            m_travToken = QString::fromUtf8(parts[3]);
            m_travLiveToken.clear();
            m_travRegAttempts = 0;
            m_travHostIpPending = false;
            m_travHostIpPort.clear();
            travSaveIdentity();

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Claimed connect code: " + m_travCode.toHtmlEscaped() + "</span>");

            // Auto-copy to clipboard
            QApplication::clipboard()->setText(m_travCode);
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Copied connect code to clipboard</span>");

            updateHostCodeUI();
            travSendClaimAck();
            travSendHostOpen();

            return;
        }

        if (strcmp(type, "CLAIMSUGGEST") == 0 && partCount >= 4)
        {
            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Requested code " + QString::fromUtf8(parts[2]).toHtmlEscaped() +
                           " is unavailable. Suggested: " +
                           QString::fromUtf8(parts[3]).toHtmlEscaped() + "</span>");
            return;
        }

        if (strcmp(type, "HOSTOK") == 0 && partCount >= 6)
        {
            if (!m_travHostEnabled) return;

            m_travCode = normalizeTraversalCode(QString::fromUtf8(parts[2]));
            m_travLiveToken = QString::fromUtf8(parts[3]);
            m_travRegAttempts = 0;
            m_travHostIpPending = false;
            m_travHostIpPort.clear();
            m_travNextRegMs = 0;
            m_travNextKeepMs = 0;
            travSaveIdentity();
            updateHostCodeUI();

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "Host session opened for " + m_travCode.toHtmlEscaped() + "</span>");

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

            if (!m_travLiveToken.isEmpty() && token != m_travLiveToken) return;

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
            const QString reason = QString::fromUtf8(parts[2]);

            if (m_travJoinEnabled && strcmp(parts[2], "BUSY") == 0)
            {
                m_travJoinBusy = true;
                m_travJoinDeadlineMs = QDateTime::currentMSecsSinceEpoch();
                m_travNextJoinMs = 0;
                return;
            }

            if (m_travJoinEnabled && (strcmp(parts[2], "OFFLINE") == 0 || strcmp(parts[2], "UNKNOWNCODE") == 0))
            {
                m_travJoinEnabled = false;
                m_travJoinDeadlineMs = 0;
                m_travNextJoinMs = 0;
            }

            if (m_travHostEnabled && strcmp(parts[2], "NOAUTH") == 0)
            {
                m_chat->append("<span style='color:green;'>" + timestamp() +
                               "Saved connect code identity was rejected. Claiming a new code.</span>");
                travClearIdentity();
                m_travRegAttempts = 0;
                m_travNextRegMs = 0;
                m_travNextKeepMs = 0;
                m_travHostIpPending = false;
                updateHostCodeUI();
                return;
            }

            if (m_travHostEnabled && strcmp(parts[2], "NOSESSION") == 0)
            {
                m_travLiveToken.clear();
                m_travNextRegMs = 0;
                m_travNextKeepMs = 0;
            }

            m_chat->append("<span style='color:green;'>" + timestamp() +
                           "NAT traversal error: " + reason.toHtmlEscaped() + "</span>");
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
    if (m_btnReady) m_btnReady->setChecked(false);
    m_chat->append("<span style='color:" + QString(QApplication::palette().window().color().value() < 128 ? "cornflowerblue" : "darkblue") + ";'>" + timestamp() + "Game ended.</span>");
}

void KailleraP2PDialog::onClientDropped(QString nick, int player)
{
    (void)player;
    if (m_pingLabel) m_pingLabel->setText("Ping: -- ms");
    m_chat->append("<span style='color:red;'>" + timestamp() + nick.toHtmlEscaped() + " dropped.</span>");
}

void KailleraP2PDialog::onDebug(QString message)
{
    m_chat->append("<span style='color:green;'>" + message.toHtmlEscaped() + "</span>");
}

void KailleraP2PDialog::onPingUpdated(int ping)
{
    if (!m_pingLabel)
    {
        return;
    }

    if (ping >= 0)
    {
        m_pingLabel->setText(QString("Ping: %1 ms").arg(ping));
    }
    else
    {
        m_pingLabel->setText("Ping: -- ms");
    }
}

void KailleraP2PDialog::onPeerJoined()
{
    if (m_pingLabel) m_pingLabel->setText("Ping: measuring...");
    m_chat->append("<span style='color:green;'>" + timestamp() + "Peer connected.</span>");
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;

    if (m_isHost && m_travHostEnabled)
    {
        m_travHostSessionSuspended = true;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;

        if (!m_travLiveToken.isEmpty())
        {
            travSendHostClose();
            m_travLiveToken.clear();
        }
    }

    // Remove from public game list while peer is connected
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        unenlistGame();
}

void KailleraP2PDialog::onPeerLeft()
{
    if (m_pingLabel) m_pingLabel->setText("Ping: -- ms");
    m_chat->append("<span style='color:red;'>" + timestamp() + "Peer disconnected.</span>");
    m_ready = false;
    if (m_btnReady) m_btnReady->setChecked(false);

    // Clear peer punching state (always, regardless of trav mode)
    m_travHostPeerIp.clear();
    m_travHostPeerPort = 0;
    m_travHostPeerDeadlineMs = 0;
    m_travNextHostPunchMs = 0;

    if (m_isHost && m_travHostEnabled)
    {
        m_travHostSessionSuspended = false;
        m_travNextRegMs = 0;
        m_travNextKeepMs = 0;
    }

    // Re-enlist on public game list now that we're waiting for a new peer
    if (m_isHost && m_enlistCheck && m_enlistCheck->isChecked())
        enlistGame();
}

void KailleraP2PDialog::onPeerInfo(QString name, QString app)
{
    emit peerNicknameResolved(name);
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
    m_ready = (m_btnReady != nullptr) ? m_btnReady->isChecked() : !m_ready;

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
    else
    {
        return;
    }

    if (m_copyAction != nullptr)
    {
        m_copyAction->setIcon(themedP2PIcon("copy-check-line"));
        m_copyAction->setToolTip("Copied");
    }

    if (m_copyFeedbackTimer != nullptr)
    {
        m_copyFeedbackTimer->start(1200);
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

    // Match the old p2p lobby behavior: keep refreshing ping once per second
    // while connected, but stop once the game has actually started.
    if (!n02::isGameRunning() && p2p_is_connected())
    {
        p2p_ping();
    }

    // ---- HOST: NAT traversal registration & keepalive ----
    if (m_isHost && m_travHostEnabled)
    {
        if (m_travHostSessionSuspended)
        {
            // Keep the static code identity locally, but do not advertise or keep
            // a live host session open while a peer is connected.
        }
        else if (m_travToken.isEmpty() || m_travCode.isEmpty())
        {
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

                    m_chat->append("<span style='color:red;'>" + timestamp() +
                                   "Failed to get a connect code from the NAT server. "
                                   "Hosting by IP instead. You may need to manually port forward.</span>");
                    ssrvWhatIsMyIp();
                }
                else
                {
                    travSendClaimAuto();
                    m_travNextRegMs = now + 2000;
                }
            }
        }
        else if (m_travLiveToken.isEmpty())
        {
            if (m_travNextRegMs == 0 || now >= m_travNextRegMs)
            {
                travSendHostOpen();
                m_travNextRegMs = now + 2000;
            }
        }
        else if (m_travNextKeepMs == 0 || now >= m_travNextKeepMs)
        {
            travSendHostKeep();
            m_travNextKeepMs = now + 10000;
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
                travPunchEndpoint(m_travHostPeerIp, m_travHostPeerPort, m_travLiveToken);
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

#endif // _WIN32
