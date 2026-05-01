/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "MainWindow.hpp"

#include "UserInterface/Dialog/AboutDialog.hpp"
#include "Dialog/Cheats/CheatsDialog.hpp"
#include "Dialog/SettingsDialog.hpp"
#include "Dialog/RomInfoDialog.hpp"
#ifdef UPDATER
#include "UserInterface/Dialog/Update/DownloadUpdateDialog.hpp"
#include "UserInterface/Dialog/Update/InstallUpdateDialog.hpp"
#include "UserInterface/Dialog/Update/UpdateDialog.hpp"
#endif // UPDATER
#ifdef NETPLAY
#include "Dialog/Netplay/NetplaySessionBrowserDialog.hpp"
#include "Dialog/Netplay/CreateNetplaySessionDialog.hpp"
#include "Dialog/Netplay/NetplaySessionDialog.hpp"
#include "KailleraUIBridge.hpp"
#include "Dialog/Kaillera/KailleraPlaybackDialog.hpp"
#include "n02_client.h"
#endif // NETPLAY
#include "UserInterface/EventFilter.hpp"
#include "Utilities/QtKeyToSdl3Key.hpp"
#include "Utilities/QtMessageBox.hpp"
#include "OnScreenDisplay.hpp"
#include "Callbacks.hpp"
#include "VidExt.hpp"

#ifdef UPDATER
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#endif // UPDATER
#ifdef NETPLAY
#include <QWebSocket>
#endif // NETPLAY
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QStyleFactory>
#include <QActionGroup> 
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QMenuBar>
#include <QString>
#include <QTimer>
#include <QShowEvent>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>

#ifdef KCA_DRAG_DROP
#include <KUrlMimeData>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <QPainter>
#include <QProxyStyle>

#include <cstdlib>

#ifdef _WIN32
class NoAccentProxyStyle final : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == PE_PanelItemViewItem)
        {
            if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
            {
                // Paint alternating row background
                if (vopt->features & QStyleOptionViewItem::Alternate)
                {
                    painter->fillRect(vopt->rect, vopt->palette.alternateBase());
                }

                if (vopt->state & State_Selected)
                {
                    QColor sel = vopt->palette.highlight().color();
                    sel.setAlpha(80);
                    painter->fillRect(vopt->rect, sel);
                }
                else if (vopt->state & State_MouseOver)
                {
                    QColor hover = vopt->palette.highlight().color();
                    hover.setAlpha(30);
                    painter->fillRect(vopt->rect, hover);
                }
                return;
            }
        }
        if (element == PE_FrameFocusRect || element == PE_PanelItemViewRow)
        {
            return;
        }
        // Suppress the raised panel/border on toolbar buttons when not hovered/pressed
        if (element == PE_PanelButtonTool)
        {
            if (!(option->state & (State_MouseOver | State_Sunken)))
            {
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == CE_ItemViewItem)
        {
            // Strip focus only — keep Selected so text color changes correctly
            QStyleOptionViewItem opt(*qstyleoption_cast<const QStyleOptionViewItem*>(option));
            opt.state &= ~State_HasFocus;
            QProxyStyle::drawControl(element, &opt, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};
#endif
#include <chrono>
#include <cmath>
#include <algorithm>
#include <vector>

#include <RMG-Core/CachedRomHeaderAndSettings.hpp>
#include <RMG-Core/SpeedLimiter.hpp>
#include <RMG-Core/Directories.hpp>
#include <RMG-Core/SpeedFactor.hpp>
#include <RMG-Core/Screenshot.hpp>
#include <RMG-Core/Emulation.hpp>
#include <RMG-Core/SaveState.hpp>
#include <RMG-Core/Settings.hpp>
#include <RMG-Core/Plugins.hpp>
#include <RMG-Core/Raphnet.hpp>
#include <RMG-Core/Netplay.hpp>
#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Version.hpp>
#include <RMG-Core/Cheats.hpp>
#include <RMG-Core/Volume.hpp>
#include <RMG-Core/Error.hpp>
#include <RMG-Core/Video.hpp>
#include <RMG-Core/Core.hpp>
#include <RMG-Core/Key.hpp>

using namespace UserInterface;
using namespace Utilities;

namespace
{
QString resolveStyleFactoryKey(const QString& styleName)
{
    const QStringList styleKeys = QStyleFactory::keys();
    for (const QString& styleKey : styleKeys)
    {
        if (QString::compare(styleKey, styleName, Qt::CaseInsensitive) == 0)
        {
            return styleKey;
        }
    }

    return styleName;
}

QPalette resolveStyleStandardPalette(const QString& styleName, const QPalette& fallbackPalette)
{
    QStyle* style = QStyleFactory::create(styleName);
    if (style == nullptr)
    {
        return fallbackPalette;
    }

    const QPalette palette = style->standardPalette();
    delete style;
    return palette;
}

#ifdef NETPLAY
constexpr std::chrono::seconds kLocalEchoMaxAge(2);
constexpr size_t kLocalEchoMaxEntries = 8;

QString NormalizeOsdKailleraChatMessage(QString message)
{
    message = message.trimmed();
    message.replace('\r', ' ');
    message.replace('\n', ' ');
    return message;
}
#endif // NETPLAY
} // namespace

MainWindow::MainWindow() : QMainWindow(nullptr)
{
}

MainWindow::~MainWindow()
{
}

bool MainWindow::Init(QApplication* app, bool showUI, bool launchROM)
{
    if (!CoreInit())
    {
        this->showErrorMessage("CoreInit() Failed", QString::fromStdString(CoreGetError()));
        return false;
    }

    if (!CoreApplyPluginSettings())
    {
        this->showErrorMessage("CoreApplyPluginSettings() Failed", QString::fromStdString(CoreGetError()));
    }

    this->configureTheme(app);

    this->initializeUI(launchROM);
    this->initializeActions();
    this->configureUI(app, showUI);
#ifdef NETPLAY
    this->refreshKailleraRecordingStorageStatus(true);
#endif // NETPLAY

    this->connectActionSignals();
    this->configureActions();
    this->updateActions(false, false);

#ifdef UPDATER
    this->checkForUpdates(true, false);
#else
    this->action_Help_Update->setVisible(false);
#endif // UPDATER

#ifndef NETPLAY
    this->menuNetplay->menuAction()->setVisible(false);
#endif // NETPLAY

    this->initializeEmulationThread();
    this->connectEmulationThreadSignals();

    if (!SetupVidExt(this->emulationThread, this, &this->ui_Widget_OpenGL, &this->ui_Widget_Vulkan))
    {
        this->showErrorMessage("SetupVidExt() Failed", QString::fromStdString(CoreGetError()));
        return false;
    }

    this->coreCallBacks = new CoreCallbacks(this);

    // connect signals early due to pending debug callbacks
    connect(coreCallBacks, &CoreCallbacks::OnCoreDebugCallback, this, &MainWindow::on_Core_DebugCallback);
    connect(coreCallBacks, &CoreCallbacks::OnCoreStateCallback, this, &MainWindow::on_Core_StateCallback);
    connect(app, &QGuiApplication::applicationStateChanged, this, &MainWindow::on_QGuiApplication_applicationStateChanged);

    if (!this->coreCallBacks->Init())
    {
        this->showErrorMessage("CoreCallbacks::Init() Failed", QString::fromStdString(CoreGetError()));
        return false;
    }

    // add actions when there's no UI
    if (!showUI)
    {
        this->addActions();
    }

#ifdef NETPLAY
    this->ui_AutoStartNetplayOnStartupPending =
        showUI &&
        !launchROM &&
        CoreSettingsGetBoolValue(SettingsID::GUI_AutoStartNetplayOnStartup);
    // Note: tryAutoStartNetplayOnStartup() is called from showEvent()
    // to ensure the main window is visible before opening Kaillera dialog
#endif // NETPLAY

    // Check for raphnet plugin mismatch after window is visible
    this->ui_CheckRaphnetPluginMismatchPending = showUI && !launchROM;

    return true;
}

void MainWindow::OpenROM(QString file, QString disk, bool fullscreen, bool quitAfterEmulation, int stateSlot)
{
    this->ui_LaunchInFullscreen = fullscreen;
    this->ui_QuitAfterEmulation = quitAfterEmulation;

    // ensure we don't switch to the ROM browser
    // because it can cause a slight flicker,
    // if we just ensure the UI is in an emulation
    // state, then the transition will be smoother
    this->updateUI(true, false);

    this->launchEmulationThread(file, disk, true, stateSlot);
}

#ifdef _WIN32
void MainWindow::restoreDisplayMode(void)
{
    if (!this->ui_DisplayModeChanged)
    {
        return;
    }
    if (!this->ui_DisplayModeDevice.empty())
    {
        ChangeDisplaySettingsExW(this->ui_DisplayModeDevice.c_str(), NULL, NULL, 0, NULL);
    }
    else
    {
        ChangeDisplaySettingsW(NULL, 0);
    }
    this->ui_DisplayModeChanged = false;
    this->ui_DisplayModeDevice.clear();
}

bool MainWindow::applyExclusiveFullscreen(void)
{
    std::string monitor = CoreSettingsGetStringValue(SettingsID::GUI_ExclusiveFullscreenMonitor);
    std::string resolution = CoreSettingsGetStringValue(SettingsID::GUI_ExclusiveFullscreenResolution);
    int refreshRate = CoreSettingsGetIntValue(SettingsID::GUI_ExclusiveFullscreenRefreshRate);

    // determine device name
    LPCWSTR deviceName = nullptr;
    std::wstring deviceNameStr;
    if (!monitor.empty())
    {
        deviceNameStr = std::wstring(monitor.begin(), monitor.end());
        deviceName = deviceNameStr.c_str();
    }

    if (resolution.empty() && refreshRate == 0 && monitor.empty())
    {
        // all desktop defaults — nothing to change
        return true;
    }

    DEVMODEW devmode = {};
    devmode.dmSize = sizeof(devmode);

    // start from current display settings of the target monitor
    if (!EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &devmode))
    {
        return false;
    }

    if (!resolution.empty())
    {
        size_t xPos = resolution.find('x');
        if (xPos != std::string::npos)
        {
            devmode.dmPelsWidth  = std::stoul(resolution.substr(0, xPos));
            devmode.dmPelsHeight = std::stoul(resolution.substr(xPos + 1));
            devmode.dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT;
        }
    }

    if (refreshRate > 0)
    {
        devmode.dmDisplayFrequency = refreshRate;
        devmode.dmFields |= DM_DISPLAYFREQUENCY;
    }

    // move window to target monitor before display mode change
    // deviceName=NULL targets the primary monitor, so always position correctly
    {
        DEVMODEW currentMode = {};
        currentMode.dmSize = sizeof(currentMode);
        if (EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &currentMode))
        {
            this->move(currentMode.dmPosition.x, currentMode.dmPosition.y);
        }
    }

    LONG result;
    if (deviceName != nullptr)
    {
        result = ChangeDisplaySettingsExW(deviceName, &devmode, NULL, CDS_FULLSCREEN, NULL);
    }
    else
    {
        result = ChangeDisplaySettingsW(&devmode, CDS_FULLSCREEN);
    }

    if (result == DISP_CHANGE_SUCCESSFUL)
    {
        this->ui_DisplayModeChanged = true;
        if (deviceName != nullptr)
        {
            this->ui_DisplayModeDevice = deviceNameStr;
        }
        return true;
    }
    return false;
}
#endif // _WIN32

void MainWindow::closeEvent(QCloseEvent *event)
{
    bool inEmulation = this->emulationThread->isRunning();

    if (this->ui_ShowUI &&
        !this->ui_ForceClose &&
        inEmulation &&
        CoreSettingsGetBoolValue(SettingsID::GUI_ConfirmExitWhileInGame))
    {
        bool skipExitConfirmation = false;
        bool ret = QtMessageBox::Question(this, "Are you sure you want to exit RMG?", "Don't ask for confirmation again", skipExitConfirmation);
        if (!ret)
        {
            event->ignore();
            return;
        }

        // only save setting when user accepted
        CoreSettingsSetValue(SettingsID::GUI_ConfirmExitWhileInGame, !skipExitConfirmation);
    }

    // we have to make sure we save the geomtry
    // for the ROM browser when emulation
    // isn't running (or hasn't run at all)
    if (!this->ui_QuitAfterEmulation && 
        !inEmulation)
    {
        this->storeGeometry();
    }

    // store toolbar location to settings
    Qt::ToolBarArea toolbarArea = this->toolBarArea(this->toolBar);
    CoreSettingsSetValue(SettingsID::GUI_ToolbarArea, this->getToolbarSettingAreaFromArea(toolbarArea));

    // attempt to shutdown emulation
    this->ui_NoSwitchToRomBrowser = true;
    this->on_Action_System_Shutdown();

    // wait until emulation has shut down
    while (this->emulationThread->isRunning())
    {
        QCoreApplication::processEvents();
    }

    this->ui_Widget_RomBrowser->StopRefreshRomList();

    this->coreCallBacks->Stop();

#ifdef NETPLAY
    if (this->netplaySessionDialog != nullptr)
    {
        this->netplaySessionDialog->close();
    }

    // Close ALL top-level Kaillera dialogs, including those with nullptr parent
    // (e.g. KailleraServerBrowserDialog, KailleraP2PDialog).
    // Their reject() handlers call kaillera_disconnect() / p2p_disconnect() for
    // graceful server disconnect before we tear down the session.
    for (QWidget* w : QApplication::topLevelWidgets())
    {
        if (w != this && w->isVisible())
        {
            QDialog* dlg = qobject_cast<QDialog*>(w);
            if (dlg)
                dlg->close();
        }
    }

    // Process events so the dialog chain fully unwinds
    // (server browser close → netplay dialog close → showServerDialog returns)
    QCoreApplication::processEvents();

    // Shutdown Kaillera if still active (safety net — dialogs should have cleaned up)
    if (this->kailleraSessionManager != nullptr)
    {
        CoreEndKailleraGame();
        CoreShutdownKaillera();
        delete this->kailleraSessionManager;
        this->kailleraSessionManager = nullptr;
    }
#endif // NETPLAY

    this->logDialog.close();

#ifdef _WIN32
    this->restoreDisplayMode();
#endif
    CoreSettingsSave();
    CoreShutdown();

    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

#ifdef NETPLAY
    // Try to auto-start netplay after the window is visible
    // singleShot(0) queues the call to run after this event completes,
    // ensuring the window is painted before the Kaillera dialog opens
    if (this->ui_AutoStartNetplayOnStartupPending)
    {
        QTimer::singleShot(0, this, [this]() {
            this->tryAutoStartNetplayOnStartup();
        });
    }
#endif // NETPLAY

    // Check for raphnet plugin mismatch after the window is visible
    if (this->ui_CheckRaphnetPluginMismatchPending)
    {
        this->ui_CheckRaphnetPluginMismatchPending = false;
        QTimer::singleShot(0, this, [this]() {
            this->checkRaphnetPluginMismatch();
        });
    }
}

void MainWindow::initializeUI(bool launchROM)
{
    this->setupUi(this);

    this->ui_Widgets = new QStackedWidget(this);
    this->ui_Widget_RomBrowser = new Widget::RomBrowserWidget(this);
    this->ui_Widget_Dummy = new Widget::DummyWidget(this);

    this->ui_EventFilter = new EventFilter(this);
    this->ui_StatusBar_Label = new QLabel(this);
    this->ui_StatusBar_RenderModeLabel = new QLabel(this);

    // only start refreshing the ROM browser
    // when RMG isn't launched with a ROM 
    // specified on the commandline
    if (!launchROM)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }

    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::PlayGame, this,
            &MainWindow::on_RomBrowser_PlayGame);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::PlayGameWith, this,
            &MainWindow::on_RomBrowser_PlayGameWith);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::PlayGameWithDisk, this,
            &MainWindow::on_RomBrowser_PlayGameWithDisk);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::PlayGameWithSlot, this,
            &MainWindow::on_RomBrowser_PlayGameWithSlot);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::EditGameSettings, this,
            &MainWindow::on_RomBrowser_EditGameSettings);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::EditGameInputSettings, this,
            &MainWindow::on_RomBrowser_EditGameInputSettings);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::Cheats, this,
            &MainWindow::on_RomBrowser_Cheats);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::ChangeRomDirectory, this,
            &MainWindow::on_RomBrowser_ChangeRomDirectory);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::RomInformation, this,
            &MainWindow::on_RomBrowser_RomInformation);
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::FileDropped, this,
            &MainWindow::on_EventFilter_FileDropped);
#ifdef NETPLAY
    connect(this->ui_Widget_RomBrowser, &Widget::RomBrowserWidget::RomListRefreshFinished, this,
            &MainWindow::on_RomBrowser_RomListRefreshFinished);
#endif // NETPLAY

    connect(this->ui_EventFilter, &EventFilter::on_EventFilter_KeyPressed, this,
            &MainWindow::on_EventFilter_KeyPressed);
    connect(this->ui_EventFilter, &EventFilter::on_EventFilter_KeyReleased, this,
            &MainWindow::on_EventFilter_KeyReleased);
    connect(this->ui_EventFilter, &EventFilter::on_EventFilter_FileDropped, this,
            &MainWindow::on_EventFilter_FileDropped);
}

void MainWindow::configureUI(QApplication* app, bool showUI)
{
    this->setCentralWidget(this->ui_Widgets);

    QString geometry = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::RomBrowser_Geometry));
    bool maximized = CoreSettingsGetBoolValue(SettingsID::RomBrowser_Maximized);
    if (maximized)
    {
        this->showMaximized();
    }
    else if (!geometry.isEmpty())
    {
        this->restoreGeometry(QByteArray::fromBase64(geometry.toLocal8Bit()));
    }

    this->ui_ShowUI = showUI;

    if (this->ui_ShowUI)
    {
        this->ui_ShowMenubar = true;
        this->ui_ShowToolbar = CoreSettingsGetBoolValue(SettingsID::GUI_Toolbar);
        this->ui_ShowStatusbar = CoreSettingsGetBoolValue(SettingsID::GUI_StatusBar);
    }
    else
    {
        this->ui_ShowMenubar = false;
        this->ui_ShowToolbar = false;
        this->ui_ShowStatusbar = false;
    }

    this->menuBar()->setVisible(this->ui_ShowMenubar);
    this->toolBar->setVisible(this->ui_ShowToolbar);
    this->statusBar()->setVisible(this->ui_ShowStatusbar);
    this->statusBar()->addPermanentWidget(this->ui_StatusBar_Label, 99);
    this->statusBar()->addPermanentWidget(this->ui_StatusBar_RenderModeLabel, 1);

    // set toolbar position according to setting
    int toolbarAreaSetting = CoreSettingsGetIntValue(SettingsID::GUI_ToolbarArea);
    Qt::ToolBarArea toolbarArea = this->getToolbarAreaFromSettingArea(toolbarAreaSetting);
    if (this->toolBarArea(this->toolBar) != toolbarArea)
    {
        this->removeToolBar(this->toolBar);
        this->addToolBar(toolbarArea, this->toolBar);
    }

    this->ui_StatusBarTimerTimeout = CoreSettingsGetIntValue(SettingsID::GUI_StatusbarMessageDuration);

    this->ui_Widgets->addWidget(this->ui_Widget_RomBrowser);
    this->ui_Widgets->addWidget(this->ui_Widget_Dummy);
    this->ui_Widgets->setCurrentWidget(this->ui_Widget_RomBrowser);

    this->setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    this->installEventFilter(this->ui_EventFilter);
    this->ui_Widget_Dummy->installEventFilter(this->ui_EventFilter);

    this->ui_WindowTitle = this->getWindowTitle();
    this->setWindowTitle(this->ui_WindowTitle);
}

void MainWindow::configureTheme(QApplication* app)
{
    static const QString defaultStyleName = resolveStyleFactoryKey(app->style()->objectName());
    static const QPalette defaultPalette = app->palette();
    static const QPalette defaultStylePalette = resolveStyleStandardPalette(defaultStyleName, defaultPalette);
    static const QString defaultStyleSheet = app->styleSheet();
    static const QString defaultFallbackThemeName = QIcon::themeName();

    // we have to retrieve the fallback icon theme
    // before applying the app theme
    QString fallbackThemeName = defaultFallbackThemeName;

    // set theme style
    QString fallbackStyleSheet = "QTableView { border: none; color: #0096d3; selection-color: #FFFFFF; selection-background-color: #0096d3; }";
    this->setStyleSheet(fallbackStyleSheet);

    app->setStyleSheet(defaultStyleSheet);
    app->setPalette(defaultPalette);

    // set application theme
    QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    if (theme == "Modern" || theme == "Native")
    {
        QStyle* style = QStyleFactory::create(defaultStyleName);
        if (style != nullptr)
        {
#ifdef _WIN32
            style = new NoAccentProxyStyle(style);
#endif
            app->setStyle(style);
        }
        app->setPalette(defaultPalette);
    }

#ifdef _WIN32
    else if (theme == "Windows Vista")
    {
        app->setStyle(new NoAccentProxyStyle(QStyleFactory::create("WindowsVista")));
        app->setPalette(app->style()->standardPalette());
    }
#endif
    else if (theme == "Fusion")
    {
        app->setStyle(QStyleFactory::create("Fusion"));
        app->setPalette(app->style()->standardPalette());
    }
    else if (theme == "Fusion Warm")
    {
        app->setStyle(QStyleFactory::create("Fusion"));
        app->setPalette(defaultStylePalette);
    }
    else if (theme == "Fusion Dark")
    {
        // adapted from https://gist.github.com/QuantumCD/6245215
        app->setStyle(QStyleFactory::create("Fusion"));

        const QColor lighterGray(75, 75, 75);
        const QColor darkGray(53, 53, 53);
        const QColor gray(128, 128, 128);
        const QColor black(25, 25, 25);
        const QColor blue(198, 238, 255);

        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, darkGray);
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, black);
        darkPalette.setColor(QPalette::AlternateBase, darkGray);
        darkPalette.setColor(QPalette::ToolTipBase, darkGray);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::Link, blue);
        darkPalette.setColor(QPalette::Highlight, lighterGray);
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::PlaceholderText, QColor(Qt::white).darker());

        darkPalette.setColor(QPalette::Active, QPalette::Button, darkGray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, gray);
        darkPalette.setColor(QPalette::Disabled, QPalette::Light, darkGray);

        app->setPalette(darkPalette);

        app->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
    }
    else if (theme.endsWith(".qss"))
    {
        QString themePath;
        themePath = QString::fromStdString(CoreGetSharedDataDirectory().string());
        themePath += CORE_DIR_SEPERATOR_STR;
        themePath += "Styles";
        themePath += CORE_DIR_SEPERATOR_STR;
        themePath += theme;

        // use Fusion as a base for the stylesheet
        app->setStyle(QStyleFactory::create("Fusion"));
        app->setPalette(app->style()->standardPalette());

        // set the stylesheet theme,
        // if the file exists and can be opened
        QFile themeFile(themePath);
        if (themeFile.exists() && 
            themeFile.open(QIODevice::ReadOnly))
        {
            app->setStyleSheet(themeFile.readAll());
        }
    }

    // set application icon theme
    QString iconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));
    if (iconTheme == "White" || iconTheme == "Black")
    {
        QIcon::setThemeName(iconTheme.toLower());
    }
    else
    { // fallback to automatic
        QPalette palette = app->palette();
        bool dark = palette.windowText().color().value() > palette.window().color().value();
        QIcon::setThemeName(dark ? "white" : "black");
    }

    // fallback for icons we don't provide (i.e standard system icons)
    QIcon::setFallbackThemeName(fallbackThemeName);
}

void MainWindow::reapplyTheme(void)
{
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app == nullptr)
    {
        return;
    }

    this->configureTheme(app);

    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets)
    {
        if (widget == nullptr)
        {
            continue;
        }

        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

QString MainWindow::getWindowTitle(void)
{
    QString windowTitle = QCoreApplication::applicationName();
    windowTitle += " (";
    windowTitle += QString::fromStdString(CoreGetVersion());
    windowTitle += ")";

    return windowTitle;
}

void MainWindow::showErrorMessage(QString text, QString details, bool force)
{
    // fallback to helper when forced
    if (force)
    {
        QtMessageBox::Error(this, text, details);
        return;
    }

    // update the message box list and ensure
    // that we don't already have one open with
    // the error that we want to display
    for (const QMessageBox* messageBox : this->ui_MessageBoxList)
    {
        if (!messageBox->isVisible())
        {
            this->ui_MessageBoxList.removeOne(messageBox);
            continue;
        }

        if (messageBox->text() == text &&
            messageBox->detailedText() == details)
        {
            return;
        }
    }

    // ensure we only display 10 errors at
    // the same time, to prevent message dialog spam
    if (this->ui_MessageBoxList.size() >= 10)
    {
        return;
    }

    QMessageBox* msgBox = new QMessageBox(this);
    msgBox->setIcon(QMessageBox::Icon::Critical);
    msgBox->setWindowTitle("Error");
    msgBox->setText(text);
    msgBox->setDetailedText(details);
    msgBox->addButton(QMessageBox::Ok);

    // expand details by default
    if (!details.isEmpty())
    {
        for (const auto& button : msgBox->buttons())
        {
            if (msgBox->buttonRole(button) == QMessageBox::ActionRole)
            {
                button->click();
                break;
            }
        }
    }

    msgBox->show();

    this->ui_MessageBoxList.append(msgBox);
}

void MainWindow::checkRaphnetPluginMismatch(void)
{
    // Check if user has previously declined this prompt
    if (CoreSettingsGetBoolValue(SettingsID::GUI_DontAskRaphnetPluginSwitch))
    {
        return;
    }

    // Check if the current input plugin is the generic RMG-Input
    std::string inputPlugin = CoreSettingsGetStringValue(SettingsID::Core_INPUT_Plugin);

    // Only check if using RMG-Input (not raphnetraw or GCA)
    if (inputPlugin.find("RMG-Input") == std::string::npos ||
        inputPlugin.find("raphnetraw") != std::string::npos ||
        inputPlugin.find("GCA") != std::string::npos)
    {
        return;
    }

    // Check each player's configured device name for raphnet 3.0+ adapters
    bool foundRaphnet = false;
    for (int i = 0; i < 4; i++)
    {
        std::string section = "Rosalie's Mupen GUI - Input Plugin Profile " + std::to_string(i);
        std::string deviceName = CoreSettingsGetStringValue(SettingsID::Input_DeviceName, section);

        if (isRaphnet3Plus(deviceName))
        {
            foundRaphnet = true;
            break;
        }
    }

    if (!foundRaphnet)
    {
        return;
    }

    // Show dialog asking user if they want to switch to raphnetraw
    QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("raphnet Adapter Detected"),
        tr("A raphnet adapter is configured but you're using the generic input plugin. "
           "Would you like to switch to the raphnetraw plugin? (recommended)"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
    );

    if (result == QMessageBox::Yes)
    {
#ifdef _WIN32
        CoreSettingsSetValue(SettingsID::Core_INPUT_Plugin, std::string("mupen64plus-input-raphnetraw.dll"));
#else
        CoreSettingsSetValue(SettingsID::Core_INPUT_Plugin, std::string("mupen64plus-input-raphnetraw.so"));
#endif
        CoreSettingsSave();

        if (!CoreApplyPluginSettings())
        {
            this->showErrorMessage("CoreApplyPluginSettings() Failed", QString::fromStdString(CoreGetError()));
        }

        // Update input settings button enabled state
        bool hasInputConfig = CorePluginsHasConfig(CorePluginType::Input);
        this->action_Settings_Input->setEnabled(hasInputConfig);
        this->action_Toolbar_Input->setEnabled(hasInputConfig);
    }
    else
    {
        // User declined, don't ask again
        CoreSettingsSetValue(SettingsID::GUI_DontAskRaphnetPluginSwitch, true);
        CoreSettingsSave();
    }
}

void MainWindow::updateUI(bool inEmulation, bool isPaused)
{
    if (!this->ui_NoSwitchToRomBrowser)
    {
        this->updateActions(inEmulation, isPaused);
    }

    if (inEmulation)
    {
        CoreRomSettings settings;
        CoreGetCurrentRomSettings(settings);

        if (!settings.GoodName.empty())
        {
            QString goodName = QString::fromStdString(settings.GoodName);
            if (goodName.endsWith("(unknown rom)") ||
                goodName.endsWith("(unknown disk)"))
            {
                std::filesystem::path romPath;
                if (CoreGetRomPath(romPath))
                {
                    goodName = QString::fromStdString(romPath.filename().string());
                }
            }

            this->setWindowTitle(goodName + QString(" - ") + this->ui_WindowTitle);
        }

        if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
        {
            if (QSurfaceFormat::defaultFormat().renderableType() == QSurfaceFormat::OpenGLES)
            {
                this->ui_StatusBar_RenderModeLabel->setText("OpenGL ES");
            }
            else
            {
                this->ui_StatusBar_RenderModeLabel->setText("OpenGL");
            }
            this->ui_Widgets->setCurrentWidget(this->ui_Widget_OpenGL->GetWidget());
        }
        else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
        {
            this->ui_StatusBar_RenderModeLabel->setText("Vulkan");
            this->ui_Widgets->setCurrentWidget(this->ui_Widget_Vulkan->GetWidget());
        }
        else
        {
            // when the video extension hasn't been initialized correctly
            // yet, we'll show a dummy widget with a black color pallete
            // to minimize the flicker that would occur when switching
            // from the ROM browser to the render widget when you i.e
            // launch RMG with a ROM on the commandline or drag & drop
            this->ui_Widgets->setCurrentWidget(this->ui_Widget_Dummy);
        }

        this->storeGeometry();
    }
    else if (!this->ui_NoSwitchToRomBrowser)
    {
        this->setWindowTitle(this->ui_WindowTitle);
        this->ui_Widgets->setCurrentWidget(this->ui_Widget_RomBrowser);
        this->ui_StatusBar_RenderModeLabel->clear();
        this->loadGeometry();
    }
    else
    {
        this->ui_NoSwitchToRomBrowser = false;
    }

    // update timer timeout
    this->ui_StatusBarTimerTimeout = CoreSettingsGetIntValue(SettingsID::GUI_StatusbarMessageDuration);
}

void MainWindow::storeGeometry(void)
{
    if (this->ui_Geometry_Saved)
    {
        return;
    }

    this->ui_Geometry = this->saveGeometry();
    this->ui_Geometry_Maximized = this->isMaximized();
    this->ui_Geometry_Saved = true;

    std::string geometryStr = this->ui_Geometry.toBase64().toStdString();
    CoreSettingsSetValue(SettingsID::RomBrowser_Geometry, geometryStr);
    CoreSettingsSetValue(SettingsID::RomBrowser_Maximized, this->ui_Geometry_Maximized);
}

void MainWindow::loadGeometry(void)
{
    if (!this->ui_Geometry_Saved)
    {
        return;
    }

    if (this->ui_Geometry_Maximized)
    {
        this->showMaximized();
    }
    else
    {
        this->restoreGeometry(this->ui_Geometry);
    }

    if (this->isFullScreen())
    {
#ifdef _WIN32
        this->restoreDisplayMode();
#endif
        this->showNormal();
    }

    if (this->ui_ShowMenubar && this->menuBar()->isHidden())
    {
        this->menuBar()->show();
    }
    else if (!this->ui_ShowMenubar && !this->menuBar()->isHidden())
    {
        this->menuBar()->hide();
    }

    if (this->ui_ShowToolbar && this->toolBar->isHidden())
    {
        this->toolBar->show();
    }
    else if (!this->ui_ShowToolbar && !this->toolBar->isHidden())
    {
        this->toolBar->hide();
    }

    if (this->ui_ShowStatusbar && this->statusBar()->isHidden())
    {
        this->statusBar()->show();
    }
    else if (!this->ui_ShowStatusbar && !this->statusBar()->isHidden())
    {
        this->statusBar()->hide();
    }

    this->ui_Geometry_Saved = false;
}

void MainWindow::initializeEmulationThread(void)
{
    this->emulationThread = new Thread::EmulationThread(this);
}

void MainWindow::connectEmulationThreadSignals(void)
{
    connect(this->emulationThread, &Thread::EmulationThread::on_Emulation_Finished, this,
            &MainWindow::on_Emulation_Finished);
    connect(this->emulationThread, &Thread::EmulationThread::on_Emulation_Started, this,
            &MainWindow::on_Emulation_Started);

    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_Init, this, &MainWindow::on_VidExt_Init,
            Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_SetupOGL, this, &MainWindow::on_VidExt_SetupOGL,
            Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_SetWindowedMode, this,
            &MainWindow::on_VidExt_SetWindowedMode, Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_SetFullscreenMode, this,
            &MainWindow::on_VidExt_SetFullscreenMode, Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_ResizeWindow, this,
            &MainWindow::on_VidExt_ResizeWindow, Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_ToggleFS, this, &MainWindow::on_VidExt_ToggleFS,
            Qt::BlockingQueuedConnection);
    connect(this->emulationThread, &Thread::EmulationThread::on_VidExt_Quit, this, &MainWindow::on_VidExt_Quit,
            Qt::BlockingQueuedConnection);
}

void MainWindow::launchEmulationThread(QString cartRom, QString address, int port, int player)
{
    CoreSettingsSave();

    if (this->emulationThread->isRunning())
    {
        this->on_Action_System_Shutdown();

        while (this->emulationThread->isRunning())
        {
            QCoreApplication::processEvents();
        }
    }

    this->emulationThread->SetNetplay(address, port, player);
    this->launchEmulationThread(cartRom, "", false, -1, true);
}

void MainWindow::launchEmulationThread(QString cartRom, QString diskRom, bool refreshRomListAfterEmulation, int slot, bool netplay, bool dragdrop)
{
#ifdef NETPLAY
    if (this->netplaySessionDialog != nullptr && !netplay)
    {
        this->showErrorMessage("EmulationThread::run Failed", "Cannot start emulation when netplay session is active");
        return;
    }

    if (!netplay)
    {
        this->ui_AutoStartNetplayOnStartupPending = false;
    }
#endif // NETPLAY

    if (!dragdrop && this->emulationThread->isRunning())
    {
        this->showErrorMessage("EmulationThread::run Failed", "Cannot start emulation when emulation is already running or being started");
        return;
    }

    CoreSettingsSave();

    if (this->emulationThread->isRunning())
    {
        this->on_Action_System_Shutdown();

        while (this->emulationThread->isRunning())
        {
            QCoreApplication::processEvents();
        }
    }

    this->ui_RefreshRomListAfterEmulation = refreshRomListAfterEmulation || this->ui_Widget_RomBrowser->IsRefreshingRomList();
    if (this->ui_RefreshRomListAfterEmulation)
    {
        this->ui_Widget_RomBrowser->StopRefreshRomList();
    }

    if (!CoreArePluginsReady())
    {
        // always go back to ROM Browser
        this->ui_NoSwitchToRomBrowser = false;
        this->updateUI(false, false);

        this->showErrorMessage("CoreArePluginsReady() Failed", QString::fromStdString(CoreGetError()));
        return;
    }

    if (this->ui_LaunchInFullscreen || CoreSettingsGetBoolValue(SettingsID::GUI_AutomaticFullscreen))
    {
        this->ui_FullscreenTimerId = this->startTimer(100);
        this->ui_LaunchInFullscreen = false;
    }

    this->ui_LoadSaveStateSlotCounter = 0;
    this->ui_LoadSaveStateSlot = slot;
    if (slot != -1)
    {
        this->ui_LoadSaveStateSlotTimerId = this->startTimer(100);
    }

    this->ui_CheckVideoSizeTimerId = this->startTimer(2000);

    this->ui_HideCursorInEmulation = CoreSettingsGetBoolValue(SettingsID::GUI_HideCursorInEmulation);
    this->ui_HideCursorInFullscreenEmulation = CoreSettingsGetBoolValue(SettingsID::GUI_HideCursorInFullscreenEmulation);
#ifdef _WIN32
    this->ui_ExclusiveFullscreen = CoreSettingsGetBoolValue(SettingsID::GUI_ExclusiveFullscreen);
#endif

    if (this->ui_ShowUI)
    {
        this->ui_ShowToolbar = CoreSettingsGetBoolValue(SettingsID::GUI_Toolbar);
        this->ui_ShowStatusbar = CoreSettingsGetBoolValue(SettingsID::GUI_StatusBar);
    }

    this->emulationThread->SetRomFile(cartRom);
    this->emulationThread->SetDiskFile(diskRom);
    this->emulationThread->start();
}

void MainWindow::updateActions(bool inEmulation, bool isPaused)
{
    QString keyBinding;

    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_StartROM));
    this->action_System_StartRom->setShortcut(QKeySequence(keyBinding));
#ifdef NETPLAY
    this->action_System_StartRom->setEnabled(!inEmulation && this->kailleraSessionManager == nullptr);
#else
    this->action_System_StartRom->setEnabled(!inEmulation);
#endif
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_StartCombo));
    this->action_System_OpenCombo->setShortcut(QKeySequence(keyBinding));
    this->action_System_OpenCombo->setEnabled(!inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Shutdown));
    this->action_System_Shutdown->setShortcut(QKeySequence(keyBinding));
#ifdef NETPLAY
    this->action_System_Shutdown->setEnabled(inEmulation && !CoreHasInitNetplay());
#else
    this->action_System_Shutdown->setEnabled(inEmulation && !CoreHasInitNetplay());
#endif
    this->menuReset->setEnabled(inEmulation && !CoreHasInitNetplay() && !CoreHasInitKaillera());
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_SoftReset));
#ifdef NETPLAY
    this->action_Netplay_Start->setEnabled(!inEmulation && this->netplaySessionDialog == nullptr && this->kailleraSessionManager == nullptr);
#else
    this->action_Netplay_Start->setEnabled(inEmulation && !CoreHasInitNetplay() && !CoreHasInitKaillera());
#endif
    this->action_Netplay_Start->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_HardReset));
    this->action_System_HardReset->setEnabled(inEmulation && !CoreHasInitNetplay() && !CoreHasInitKaillera());
    this->action_System_HardReset->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Resume));
    this->action_System_Pause->setChecked(isPaused);
    this->action_System_Pause->setEnabled(inEmulation && !CoreHasInitNetplay() && !CoreHasInitKaillera());
    this->action_System_Pause->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Screenshot));
    this->action_System_Screenshot->setEnabled(inEmulation);
    this->action_System_Screenshot->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_LimitFPS));
    this->action_System_LimitFPS->setEnabled(inEmulation && !CoreHasInitNetplay());
    this->action_System_LimitFPS->setShortcut(QKeySequence(keyBinding));
    this->action_System_LimitFPS->setChecked(CoreIsSpeedLimiterEnabled());
    this->menuSpeedFactor->setEnabled(inEmulation && !CoreHasInitNetplay());
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_SaveState));
    this->action_System_SaveState->setEnabled(inEmulation);
    this->action_System_SaveState->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_SaveAs));
    this->action_System_SaveAs->setEnabled(inEmulation);
    this->action_System_SaveAs->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_LoadState));
    this->action_System_LoadState->setEnabled(inEmulation && !CoreHasInitNetplay());
    this->action_System_LoadState->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Load));
    this->action_System_Load->setEnabled(inEmulation && !CoreHasInitNetplay());
    this->action_System_Load->setShortcut(QKeySequence(keyBinding));
    this->menuCurrent_Save_State->setEnabled(inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Cheats));
    this->action_System_Cheats->setEnabled(inEmulation && !CoreHasInitNetplay());
    this->action_System_Cheats->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_GSButton));
    this->action_System_GSButton->setEnabled(inEmulation && !CoreHasInitNetplay());
    this->action_System_GSButton->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Exit));
    this->action_System_Exit->setShortcut(QKeySequence(keyBinding));

    // configure keybindings for speed factor
    QAction* speedActions[] =
    {
        this->actionSpeed25, this->actionSpeed50, this->actionSpeed75,
        this->actionSpeed100, this->actionSpeed125, this->actionSpeed150,
        this->actionSpeed175, this->actionSpeed200, this->actionSpeed225,
        this->actionSpeed250, this->actionSpeed275, this->actionSpeed300
    };
    SettingsID speedKeybindSettingsId[] =
    {
        SettingsID::KeyBinding_SpeedFactor25, SettingsID::KeyBinding_SpeedFactor50,
        SettingsID::KeyBinding_SpeedFactor75, SettingsID::KeyBinding_SpeedFactor100,
        SettingsID::KeyBinding_SpeedFactor125, SettingsID::KeyBinding_SpeedFactor150,
        SettingsID::KeyBinding_SpeedFactor175, SettingsID::KeyBinding_SpeedFactor200,
        SettingsID::KeyBinding_SpeedFactor225, SettingsID::KeyBinding_SpeedFactor250,
        SettingsID::KeyBinding_SpeedFactor275, SettingsID::KeyBinding_SpeedFactor300
    };
    for (int i = 0; i < 12; i++)
    {
        keyBinding = QString::fromStdString(CoreSettingsGetStringValue(speedKeybindSettingsId[i]));
        speedActions[i]->setShortcut(QKeySequence(keyBinding));
    }

    // configure keybindings for save slots
    SettingsID slotKeybindSettingsId[] =
    {
        SettingsID::KeyBinding_SaveStateSlot0, SettingsID::KeyBinding_SaveStateSlot1,
        SettingsID::KeyBinding_SaveStateSlot2, SettingsID::KeyBinding_SaveStateSlot3,
        SettingsID::KeyBinding_SaveStateSlot4, SettingsID::KeyBinding_SaveStateSlot5,
        SettingsID::KeyBinding_SaveStateSlot6, SettingsID::KeyBinding_SaveStateSlot7,
        SettingsID::KeyBinding_SaveStateSlot8, SettingsID::KeyBinding_SaveStateSlot9
    };
    int currentSlot = CoreGetSaveStateSlot();
    for (int i = 0; i < 10; i++)
    {
        keyBinding = QString::fromStdString(CoreSettingsGetStringValue(slotKeybindSettingsId[i]));
        this->ui_SlotActions[i]->setShortcut(QKeySequence(keyBinding));
        this->ui_SlotActions[i]->setChecked(i == currentSlot);
    }

    // configure text and filename data for save slots
    this->updateSaveStateSlotActions(inEmulation, isPaused);

    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_GraphicsSettings));
    this->action_Settings_Graphics->setEnabled(CorePluginsHasConfig(CorePluginType::Gfx));
    this->action_Settings_Graphics->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_AudioSettings));
    this->action_Settings_Audio->setEnabled(CorePluginsHasConfig(CorePluginType::Audio));
    this->action_Settings_Audio->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_RspSettings));
    this->action_Settings_Rsp->setEnabled(CorePluginsHasConfig(CorePluginType::Rsp));
    this->action_Settings_Rsp->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_InputSettings));
    this->action_Settings_Input->setEnabled(CorePluginsHasConfig(CorePluginType::Input));
    this->action_Settings_Input->setShortcut(QKeySequence(keyBinding));
    this->action_Toolbar_Input->setEnabled(CorePluginsHasConfig(CorePluginType::Input));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Settings));
    this->action_Settings_Settings->setShortcut(QKeySequence(keyBinding));

    this->action_View_GameList->setEnabled(!inEmulation);
    this->action_View_GameGrid->setEnabled(!inEmulation);
    this->action_View_UniformSize->setEnabled(!inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_Fullscreen));
    this->action_View_Fullscreen->setEnabled(inEmulation);
    this->action_View_Fullscreen->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_RefreshROMList));
    this->action_View_RefreshRoms->setEnabled(!inEmulation);
    this->action_View_RefreshRoms->setShortcut(QKeySequence(keyBinding));
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_ViewLog));
    this->action_View_Log->setShortcut(QKeySequence(keyBinding));
    this->action_View_ClearRomCache->setEnabled(!inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_ViewSearch));
    this->action_View_Search->setShortcut(QKeySequence(keyBinding));
    this->action_View_Search->setEnabled(!inEmulation);

#ifdef NETPLAY
    this->action_Netplay_BrowseSessions->setEnabled(!inEmulation && this->netplaySessionDialog == nullptr && this->kailleraSessionManager == nullptr);
#endif // NETPLAY

    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_IncreaseVolume));
    this->action_Audio_IncreaseVolume->setShortcut(QKeySequence(keyBinding));
    this->action_Audio_IncreaseVolume->setEnabled(inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_DecreaseVolume));
    this->action_Audio_DecreaseVolume->setShortcut(QKeySequence(keyBinding));
    this->action_Audio_DecreaseVolume->setEnabled(inEmulation);
    keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_ToggleMuteVolume));
    this->action_Audio_ToggleVolumeMute->setShortcut(QKeySequence(keyBinding));
    this->action_Audio_ToggleVolumeMute->setEnabled(inEmulation);
}

void MainWindow::updateSaveStateSlotActions(bool inEmulation, bool isPaused)
{
    std::filesystem::path saveStatePath;

    // do nothing when paused
    if (isPaused)
    {
        return;
    }

    for (int i = 0; i < 10; i++)
    {
        if (inEmulation &&
            CoreGetSaveStatePath(i, saveStatePath))
        {
            this->ui_SlotActions[i]->setData(QString::fromStdString(saveStatePath.string()));
            this->ui_SlotActions[i]->setText(this->getSaveStateSlotText(this->ui_SlotActions[i], i));
        }
        else
        {
            this->ui_SlotActions[i]->setData("");
            this->ui_SlotActions[i]->setText(this->getSaveStateSlotText(this->ui_SlotActions[i], i));
        }
    }
}

void MainWindow::addActions(void)
{
    if (this->ui_AddedActions)
    {
        return;
    }

    for (QAction* action : this->ui_Actions)
    {
        this->addAction(action);
    }

    this->ui_AddedActions = true;
}

void MainWindow::removeActions(void)
{
    if (!this->ui_AddedActions)
    {
        return;
    }

    for (QAction* action : this->ui_Actions)
    {
        this->removeAction(action);
    }

    this->ui_AddedActions = false;
}

QString MainWindow::getSaveStateSlotDateTimeText(QAction* action)
{
    QString saveStateSlotText;
    QString filePath;

    // retrieve file name
    filePath = action->data().toString();

    // check if file exists, if it does,
    // return a string with the datetime
    QFileInfo saveStateFileInfo(filePath);
    if (!filePath.isEmpty() && saveStateFileInfo.exists())
    {
        saveStateSlotText = saveStateFileInfo.lastModified().toString("dd/MM/yyyy hh:mm");
    }

    return saveStateSlotText;
}

QString MainWindow::getSaveStateSlotText(QAction* action, int slot)
{
    QString saveStateSlotText;
    QString dateTimeText;
    QString filePath;

    // base text
    saveStateSlotText = "Slot " + QString::number(slot);

    // add date and time text when it isnt empty
    dateTimeText = this->getSaveStateSlotDateTimeText(action);
    if (!dateTimeText.isEmpty())
    {
        saveStateSlotText += " - ";
        saveStateSlotText += dateTimeText;
    }

    return saveStateSlotText;
}

int MainWindow::getToolbarSettingAreaFromArea(Qt::ToolBarArea area)
{
    switch (area)
    {
    case Qt::ToolBarArea::TopToolBarArea:
        return 0;
    case Qt::ToolBarArea::BottomToolBarArea:
        return 1;
    case Qt::ToolBarArea::LeftToolBarArea:
        return 2;
    case Qt::ToolBarArea::RightToolBarArea:
        return 3;

    default:
        return 0;
    }
}

Qt::ToolBarArea MainWindow::getToolbarAreaFromSettingArea(int value)
{
    switch (value)
    {
    default:
    case 0:
        return Qt::ToolBarArea::TopToolBarArea;
    case 1:
        return Qt::ToolBarArea::BottomToolBarArea;
    case 2: 
        return Qt::ToolBarArea::LeftToolBarArea;
    case 3:
        return Qt::ToolBarArea::RightToolBarArea;
    }
}

void MainWindow::initializeActions(void)
{
    // Audio actions
    this->action_Audio_IncreaseVolume   = new QAction(this);
    this->action_Audio_DecreaseVolume   = new QAction(this);
    this->action_Audio_ToggleVolumeMute = new QAction(this);
    this->action_Audio_IncreaseVolume->setEnabled(false);
    this->action_Audio_DecreaseVolume->setEnabled(false);
    this->action_Audio_ToggleVolumeMute->setEnabled(false);

    // because these are hotkey exclusive actions,
    // we only have to add them once
    this->addAction(this->action_Audio_IncreaseVolume);
    this->addAction(this->action_Audio_DecreaseVolume);
    this->addAction(this->action_Audio_ToggleVolumeMute);
}


void MainWindow::configureActions(void)
{
    // configure actions list
    this->ui_Actions.append(
    {
        // System actions
        this->action_System_StartRom, this->action_System_OpenCombo,
        this->action_System_Shutdown, this->action_Netplay_Start,
        this->action_System_HardReset, this->action_System_Pause,
        this->action_System_Screenshot, this->action_System_LimitFPS,
        this->actionSpeed25, this->actionSpeed50, this->actionSpeed75,
        this->actionSpeed100, this->actionSpeed125, this->actionSpeed150,
        this->actionSpeed175, this->actionSpeed200, this->actionSpeed225,
        this->actionSpeed250, this->actionSpeed275, this->actionSpeed300,
        this->action_System_SaveState, this->action_System_SaveAs,
        this->action_System_LoadState, this->action_System_Load,
        this->actionSlot_0, this->actionSlot_1, this->actionSlot_2,
        this->actionSlot_3, this->actionSlot_4, this->actionSlot_5,
        this->actionSlot_6, this->actionSlot_7, this->actionSlot_8,
        this->actionSlot_9, this->action_System_Cheats,
        this->action_System_GSButton, this->action_System_Exit,
        // Settings actions
        this->action_Settings_Graphics, this->action_Settings_Audio,
        this->action_Settings_Rsp, this->action_Settings_Input,
        this->action_Settings_Settings,
        // View actions
        this->action_View_Fullscreen, this->action_View_RefreshRoms,
        this->action_View_Log,
        // Help actions
        this->action_Help_Github, this->action_Help_About,
    });

    // configure save slot actions
    this->ui_SlotActions.append(
    {
        this->actionSlot_0, this->actionSlot_1, this->actionSlot_2,
        this->actionSlot_3, this->actionSlot_4, this->actionSlot_5,
        this->actionSlot_6, this->actionSlot_7, this->actionSlot_8,
        this->actionSlot_9
    });

    // configure emulation speed actions
    QActionGroup* speedActionGroup = new QActionGroup(this);
    QAction* speedActions[] =
    {
        this->actionSpeed25, this->actionSpeed50, this->actionSpeed75,
        this->actionSpeed100, this->actionSpeed125, this->actionSpeed150,
        this->actionSpeed175, this->actionSpeed200, this->actionSpeed225,
        this->actionSpeed250, this->actionSpeed275, this->actionSpeed300
    };
    int speedActionNumbers[] =
    {
        25, 50, 75, 100,
        125, 150, 175, 200,
        225, 250, 275, 300
    };
    int currentSpeedFactor = CoreGetSpeedFactor();
    for (int i = 0; i < 12; i++)
    {
        QAction* speedAction = speedActions[i];
        int speedActionNumber = speedActionNumbers[i];

        speedAction->setCheckable(true);
        speedAction->setChecked(currentSpeedFactor == speedActionNumber);
        speedAction->setActionGroup(speedActionGroup);

        // connect emulation speed action here because we need to do
        // something special for them
        connect(speedAction, &QAction::triggered, [this, speedActionNumber](bool checked)
        {
            if (checked)
            {
                this->on_Action_System_SpeedFactor(speedActionNumber);
            }
        });
    }

    // configure save slot actions
    QActionGroup* slotActionGroup = new QActionGroup(this);
    for (int i = 0; i < 10; i++)
    {
        QAction* slotAction = this->ui_SlotActions[i];

        slotAction->setCheckable(true);
        slotAction->setChecked(i == 0);
        slotAction->setActionGroup(slotActionGroup);

        // connect slot action here because we need to do
        // something special for them
        connect(slotAction, &QAction::triggered, [this, i](bool checked)
        {
            if (checked)
            {
                this->on_Action_System_CurrentSaveState(i);
            }
        });
    }

    // configure toolbar & statusbar actions
    this->action_View_Toolbar->setChecked(CoreSettingsGetBoolValue(SettingsID::GUI_Toolbar));
    this->action_View_StatusBar->setChecked(CoreSettingsGetBoolValue(SettingsID::GUI_StatusBar));

    // configure ROM browser view actions
    QActionGroup* romBrowserViewActionGroup = new QActionGroup(this);
    int currentView = CoreSettingsGetIntValue(SettingsID::RomBrowser_ViewMode);
    QAction* romBrowserViewActions[] =
    {
        this->action_View_GameList,
        this->action_View_GameGrid
    };
    for (int i = 0; i < 2; i++)
    {
        QAction* action = romBrowserViewActions[i];

        action->setCheckable(true);
        action->setChecked(i == currentView);
        action->setActionGroup(romBrowserViewActionGroup);
    }

    // configure grid view options actions
    this->action_View_UniformSize->setChecked(CoreSettingsGetBoolValue(SettingsID::RomBrowser_GridViewUniformItemSizes));
}

void MainWindow::connectActionSignals(void)
{
    connect(this->action_System_StartRom, &QAction::triggered, this, &MainWindow::on_Action_System_OpenRom);
    connect(this->action_System_OpenCombo, &QAction::triggered, this, &MainWindow::on_Action_System_OpenCombo);
    connect(this->action_System_Exit, &QAction::triggered, this, &MainWindow::on_Action_System_Exit);

    connect(this->action_System_Shutdown, &QAction::triggered, this, &MainWindow::on_Action_System_Shutdown);
#ifdef NETPLAY
    connect(this->action_Netplay_Start, &QAction::triggered, this, &MainWindow::on_Action_Netplay_BrowseSessions);
#else
    connect(this->action_Netplay_Start, &QAction::triggered, this, &MainWindow::on_Action_System_SoftReset);
#endif
    connect(this->action_System_HardReset, &QAction::triggered, this, &MainWindow::on_Action_System_HardReset);
    connect(this->action_System_Pause, &QAction::triggered, this, &MainWindow::on_Action_System_Pause);
    connect(this->action_System_Screenshot, &QAction::triggered, this,
            &MainWindow::on_Action_System_Screenshot);
    connect(this->action_System_LimitFPS, &QAction::triggered, this, &MainWindow::on_Action_System_LimitFPS);
    connect(this->action_System_SaveState, &QAction::triggered, this, &MainWindow::on_Action_System_SaveState);
    connect(this->action_System_SaveAs, &QAction::triggered, this, &MainWindow::on_Action_System_SaveAs);
    connect(this->action_System_LoadState, &QAction::triggered, this, &MainWindow::on_Action_System_LoadState);
    connect(this->action_System_Load, &QAction::triggered, this, &MainWindow::on_Action_System_Load);
    connect(this->action_System_Cheats, &QAction::triggered, this, &MainWindow::on_Action_System_Cheats);
    connect(this->action_System_GSButton, &QAction::triggered, this, &MainWindow::on_Action_System_GSButton);

    connect(this->action_Settings_Graphics, &QAction::triggered, this, &MainWindow::on_Action_Settings_Graphics);
    connect(this->action_Settings_Audio, &QAction::triggered, this, &MainWindow::on_Action_Settings_Audio);
    connect(this->action_Settings_Rsp, &QAction::triggered, this, &MainWindow::on_Action_Settings_Rsp);
    connect(this->action_Settings_Input, &QAction::triggered, this,
            &MainWindow::on_Action_Settings_Input);
    connect(this->action_Settings_Settings, &QAction::triggered, this, &MainWindow::on_Action_Settings_Settings);
    connect(this->action_Settings_Plugins, &QAction::triggered, this, &MainWindow::on_Action_Settings_Plugins);
    connect(this->action_Toolbar_Input, &QAction::triggered, this, &MainWindow::on_Action_Settings_Input);
    connect(this->action_Toolbar_Playback, &QAction::triggered, this, &MainWindow::on_Action_Playback);

    connect(this->action_View_Toolbar, &QAction::toggled, this, &MainWindow::on_Action_View_Toolbar);
    connect(this->action_View_StatusBar, &QAction::toggled, this, &MainWindow::on_Action_View_StatusBar);
    connect(this->action_View_GameList, &QAction::toggled, this, &MainWindow::on_Action_View_GameList);
    connect(this->action_View_GameGrid, &QAction::toggled, this, &MainWindow::on_Action_View_GameGrid);
    connect(this->action_View_UniformSize, &QAction::toggled, this, &MainWindow::on_Action_View_UniformSize);
    connect(this->action_View_Fullscreen, &QAction::triggered, this, &MainWindow::on_Action_View_Fullscreen);
    connect(this->action_View_RefreshRoms, &QAction::triggered, this, &MainWindow::on_Action_View_RefreshRoms);
    connect(this->action_View_ClearRomCache, &QAction::triggered, this, &MainWindow::on_Action_View_ClearRomCache);
    connect(this->action_View_Log, &QAction::triggered, this, &MainWindow::on_Action_View_Log);
    connect(this->action_View_Search, &QAction::triggered, this, &MainWindow::on_Action_View_Search);

    connect(this->action_Netplay_BrowseSessions, &QAction::triggered, this, &MainWindow::on_Action_Netplay_BrowseSessions);

    connect(this->action_Help_Github, &QAction::triggered, this, &MainWindow::on_Action_Help_Github);
    connect(this->action_Help_About, &QAction::triggered, this, &MainWindow::on_Action_Help_About);
    connect(this->action_Help_Update, &QAction::triggered, this, &MainWindow::on_Action_Help_Update);

    connect(this->action_Audio_IncreaseVolume, &QAction::triggered, this, &MainWindow::on_Action_Audio_IncreaseVolume);
    connect(this->action_Audio_DecreaseVolume, &QAction::triggered, this, &MainWindow::on_Action_Audio_DecreaseVolume);
    connect(this->action_Audio_ToggleVolumeMute, &QAction::triggered, this, &MainWindow::on_Action_Audio_ToggleVolumeMute);
}

#ifdef UPDATER
void MainWindow::checkForUpdates(bool silent, bool force)
{
    if (!force && !CoreSettingsGetBoolValue(SettingsID::GUI_CheckForUpdates))
    {
        return;
    }

    // only check for updates on stable versions unless forced
    QString currentVersion = QString::fromStdString(CoreGetVersion());
    if (!force)
    {
        static const QRegularExpression stableVersionRegex("^v?\\d+(?:\\.\\d+){0,2}$");
        if (!stableVersionRegex.match(currentVersion).hasMatch())
        {
            return;
        }
    }

    QString dateTimeFormat = "dd-MM-yyyy_hh:mm";
    QString lastUpdateCheckDateTimeString = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_LastUpdateCheck));
    QDateTime lastUpdateCheckDateTime = QDateTime::fromString(lastUpdateCheckDateTimeString, dateTimeFormat);
    QDateTime currentDateTime = QDateTime::currentDateTime();

    // only check for updates once every hour unless forced
    if (!force &&
        lastUpdateCheckDateTime.isValid() &&
        lastUpdateCheckDateTime.addSecs(3600) > currentDateTime)
    {
        return;
    }

    // update last update check date & time
    CoreSettingsSetValue(SettingsID::GUI_LastUpdateCheck, currentDateTime.toString(dateTimeFormat).toStdString());

    // whether or not the update check is silent
    this->ui_SilentUpdateCheck = silent;

    // execute update check
    QNetworkAccessManager* networkAccessManager = new QNetworkAccessManager(this);
    connect(networkAccessManager, &QNetworkAccessManager::finished, this, &MainWindow::on_networkAccessManager_Finished);
    networkAccessManager->setTransferTimeout(15000);
    networkAccessManager->get(QNetworkRequest(QUrl("https://api.github.com/repos/Jay-Day/RMG-K/releases/latest")));
}
#endif // UPDATER

#ifdef NETPLAY
void MainWindow::showNetplaySessionDialog(QWebSocket* webSocket, QJsonObject json, QString sessionFile)
{
    if (this->netplaySessionDialog != nullptr)
    {
        this->netplaySessionDialog->deleteLater();
        this->netplaySessionDialog = nullptr;
    }
    
    this->netplaySessionDialog = new Dialog::NetplaySessionDialog(nullptr, webSocket, json, sessionFile);
    connect(this->netplaySessionDialog, &Dialog::NetplaySessionDialog::OnPlayGame, this, &MainWindow::on_Netplay_PlayGame);
    connect(this->netplaySessionDialog, &Dialog::NetplaySessionDialog::rejected, this, &MainWindow::on_NetplaySessionDialog_rejected);
    this->netplaySessionDialog->show();

    // force refresh of actions
    this->updateActions(false, false);
}

void MainWindow::tryAutoStartNetplayOnStartup(void)
{
    if (!this->ui_AutoStartNetplayOnStartupPending)
    {
        return;
    }

    if (this->ui_Widget_RomBrowser != nullptr &&
        this->ui_Widget_RomBrowser->IsRefreshingRomList())
    {
        return;
    }

    if (this->kailleraSessionManager != nullptr)
    {
        this->ui_AutoStartNetplayOnStartupPending = false;
        return;
    }

    this->ui_AutoStartNetplayOnStartupPending = false;
    QTimer::singleShot(0, this, [this]() {
        this->on_Action_Netplay_BrowseSessions();
    });
}

void MainWindow::refreshKailleraRecordingStorageStatus(bool showStartupWarning)
{
    const bool overCap = CoreRefreshKailleraRecordingStorageStatus();

    if (!showStartupWarning || !overCap || !this->ui_ShowStatusbar)
    {
        return;
    }

    int capMB = CoreSettingsGetIntValue(SettingsID::Kaillera_RecordingCapMB);
    if (capMB < 1)
    {
        capMB = 1;
    }

    const uint64_t bytes = CoreGetKailleraRecordingStorageBytes();
    QString usageText;
    if (bytes >= (1024ULL * 1024ULL * 1024ULL))
    {
        const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        usageText = QString::number(gib, 'f', 2) + " GB";
    }
    else
    {
        const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        usageText = QString::number(mib, 'f', 1) + " MB";
    }

    this->ui_StatusBar_Label->setText(
        QString("Kaillera recordings folder is over cap (%1 used / %2 MB cap). Record game defaults to off.")
            .arg(usageText)
            .arg(capMB));
}

void MainWindow::on_RomBrowser_RomListRefreshFinished(bool canceled)
{
    if (canceled)
    {
        return;
    }

    this->tryAutoStartNetplayOnStartup();
}
#endif // NETPLAY

void MainWindow::timerEvent(QTimerEvent *event)
{
    int timerId = event->timerId();

    if (timerId == this->ui_ResetStatusBarTimerId)
    {
        this->ui_StatusBar_Label->clear();
    }
    else if (timerId == this->ui_FullscreenTimerId)
    {
        // only try to go to fullscreen
        // when emulation is running
        if (!CoreIsEmulationRunning())
        {
            return;
        }

        // we're finished when we're in fullscreen already,
        // or when switching to fullscreen succeeds
        if (this->isFullScreen() || CoreToggleFullscreen())
        {
            this->killTimer(timerId);
            this->ui_FullscreenTimerId = 0;
        }
    }
    else if (timerId == this->ui_GamesharkButtonTimerId)
    {
        if (!CoreIsEmulationRunning())
        {
            return;
        }

        if (CorePressGamesharkButton(false))
        {
            this->killTimer(timerId);
            this->ui_GamesharkButtonTimerId = 0;
        }
    }
    else if (timerId == this->ui_UpdateSaveStateSlotTimerId)
    {
        this->updateSaveStateSlotActions(CoreIsEmulationRunning(), false);
        this->killTimer(timerId);
        this->ui_UpdateSaveStateSlotTimerId = 0;
    }
    else if (timerId == this->ui_CheckVideoSizeTimerId)
    {
        if (!CoreIsEmulationRunning())
        {
            return;
        }

        int width  = 0;
        int height = 0;
        if (!CoreGetVideoSize(width, height))
        {
            return;
        }

        int expectedWidth  = 0;
        int expectedHeight = 0;
        if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
        {
            expectedWidth  = this->ui_Widget_OpenGL->GetWidget()->width()  * this->devicePixelRatio();
            expectedHeight = this->ui_Widget_OpenGL->GetWidget()->height() * this->devicePixelRatio();
        }
        else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
        {
            expectedWidth  = this->ui_Widget_Vulkan->GetWidget()->width()  * this->devicePixelRatio();
            expectedHeight = this->ui_Widget_Vulkan->GetWidget()->height() * this->devicePixelRatio();
        }

        if (width  != expectedWidth ||
            height != expectedHeight)
        {
            CoreSetVideoSize(expectedWidth, expectedHeight);
        }
    }
    else if (timerId == this->ui_LoadSaveStateSlotTimerId)
    {
        if (!CoreIsEmulationRunning())
        {
            return;
        }

        if (!CoreSetSaveStateSlot(this->ui_LoadSaveStateSlot) ||
            !CoreLoadSaveState())
        {
            this->ui_LoadSaveStateSlotCounter++;
            if (this->ui_LoadSaveStateSlotCounter >= 5)
            { // give up after 5 attempts
                this->killTimer(this->ui_LoadSaveStateSlotTimerId);
                this->ui_LoadSaveStateSlotCounter = 0;
                this->ui_LoadSaveStateSlotTimerId = -1;
                this->ui_LoadSaveStateSlot        = -1;
            }
            return;
        }

        this->killTimer(this->ui_LoadSaveStateSlotTimerId);
    }
}

void MainWindow::on_EventFilter_KeyPressed(QKeyEvent *event)
{
#ifdef NETPLAY
    if (this->handleNetplayChatKeyPress(event))
    {
        return;
    }
#endif // NETPLAY

    if (!CoreIsEmulationRunning())
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    int key = Utilities::QtKeyToSdl3Key(event->key());
    int mod = Utilities::QtModKeyToSdl3ModKey(event->modifiers());

    CoreSetKeyDown(key, mod);
}

void MainWindow::on_EventFilter_KeyReleased(QKeyEvent *event)
{
#ifdef NETPLAY
    if (this->ui_NetplayChatInputActive)
    {
        return;
    }
#endif // NETPLAY

    if (!CoreIsEmulationRunning())
    {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    int key = Utilities::QtKeyToSdl3Key(event->key());
    int mod = Utilities::QtModKeyToSdl3ModKey(event->modifiers());

    CoreSetKeyUp(key, mod);
}

void MainWindow::on_EventFilter_FileDropped(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    QList<QUrl> urls = mimeData->urls();
#ifdef KCA_DRAG_DROP
    urls = KUrlMimeData::urlsFromMimeData(mimeData, KUrlMimeData::PreferLocalUrls);
#endif

    if (!mimeData->hasUrls() || urls.empty() ||
        !urls.first().isLocalFile())
    {
        return;
    }

    bool inEmulation     = (this->ui_Widgets->currentIndex() != 0);
    bool confirmDragDrop = CoreSettingsGetBoolValue(SettingsID::GUI_ConfirmDragDrop);
    bool refreshRomList  = false;
    QString file;

    // when we're still opening the ROM while emulation is running,
    // ignore the event
    if (this->emulationThread->isRunning() && !CoreHasRomOpen())
    {
        return;
    }

    if (inEmulation && confirmDragDrop)
    {
        confirmDragDrop = false;
        bool ret = QtMessageBox::Question(this, "Are you sure you want to launch the drag & dropped ROM?", 
                                                "Don't ask for confirmation again", confirmDragDrop);
        if (!ret)
        {
            return;
        }

        CoreSettingsSetValue(SettingsID::GUI_ConfirmDragDrop, !confirmDragDrop);
    }

    file = urls.first().toLocalFile();

    if (inEmulation)
    {
        this->ui_NoSwitchToRomBrowser = true;
        // we have to keep the state of this->ui_RefreshRomListAfterEmulation,
        // because when that's reset on every launch, and because when
        // RMG is launched with a ROM specified, the ROM browser
        // hasn't loaded yet, we should pass on the state to ensure
        // it does refresh when you do the following:
        // launch RMG with a ROM -> drag & drop -> return to ROM browser
        refreshRomList = this->ui_RefreshRomListAfterEmulation;
    }

    this->launchEmulationThread(file, "", refreshRomList, -1, false, true);
}

void MainWindow::on_QGuiApplication_applicationStateChanged(Qt::ApplicationState state)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    bool pauseOnFocusLoss = CoreSettingsGetBoolValue(SettingsID::GUI_PauseEmulationOnFocusLoss);
    bool resumeOnFocus = CoreSettingsGetBoolValue(SettingsID::GUI_ResumeEmulationOnFocus);

    switch (state)
    {
        default:
            break;

        case Qt::ApplicationState::ApplicationInactive:
        {
            // Don't pause if Kaillera netplay is active (can't pause during synchronized netplay)
            if (pauseOnFocusLoss && isRunning && !isPaused && !CoreHasInitKaillera())
            {
                this->on_Action_System_Pause();
                this->ui_ManuallyPaused = false;
            }
        } break;

        case Qt::ApplicationState::ApplicationActive:
        {
            if (resumeOnFocus && isPaused && !this->ui_ManuallyPaused)
            {
                this->on_Action_System_Pause();
            }
        } break;
    }
}

#ifdef UPDATER
void MainWindow::on_networkAccessManager_Finished(QNetworkReply* reply)
{
    if (reply->error())
    {
        if (!this->ui_SilentUpdateCheck)
        {
            this->showErrorMessage("Failed to check for updates", reply->errorString());
        }
        reply->deleteLater();
        return;
    }

    QJsonDocument jsonDocument = QJsonDocument::fromJson(reply->readAll());
    QJsonObject jsonObject = jsonDocument.object();

    QString currentVersion = QString::fromStdString(CoreGetVersion());
    QString latestVersion = jsonObject.value("tag_name").toString();

    reply->deleteLater();

    // do nothing when versions match
    if (currentVersion == latestVersion)
    {
        if (!this->ui_SilentUpdateCheck)
        {
            Utilities::QtMessageBox::Info(this, "You're already on the latest version");
        }
        return;
    }

    int ret = 0;

    Dialog::UpdateDialog updateDialog(this, jsonObject, !this->ui_SilentUpdateCheck);
    ret = updateDialog.exec();
    if (ret != QDialog::Accepted)
    {
        return;
    }

    Dialog::DownloadUpdateDialog downloadUpdateDialog(this, updateDialog.GetUrl(), updateDialog.GetFileName());
    ret = downloadUpdateDialog.exec();
    if (ret != QDialog::Accepted)
    {
        return;
    }

#ifdef APPIMAGE_UPDATER
    this->ui_ForceClose = true;
    this->close();
#else // normal updater
    Dialog::InstallUpdateDialog installUpdateDialog(this, QCoreApplication::applicationDirPath(), downloadUpdateDialog.GetTempDirectory(), downloadUpdateDialog.GetFileName());
    ret = installUpdateDialog.exec();
    if (ret != QDialog::Accepted)
    {
        return;
    }
#endif // APPIMAGE_UPDATER
}
#endif // UPDATER

void MainWindow::on_Action_System_OpenRom(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    QString romFile = QFileDialog::getOpenFileName(this, tr("Open N64 ROM or 64DD Disk"), "", "N64 ROMs & Disks (*.n64 *.z64 *.v64 *.ndd *.d64 *.zip *.7z)");
    if (romFile.isEmpty())
    {
        if (isRunning && !isPaused)
        {
            this->on_Action_System_Pause();
        }
        return;
    }

    if (this->ui_Widgets->currentIndex() != 0)
    {
        this->ui_NoSwitchToRomBrowser = true;
    }

    this->launchEmulationThread(romFile);
}

void MainWindow::on_Action_System_OpenCombo(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    QString cartRom = QFileDialog::getOpenFileName(this, tr("Open N64 ROM"), "", "N64 ROMs (*.n64 *.z64 *.v64 *.zip *.7z)");
    if (cartRom.isEmpty())
    {
        if (isRunning && !isPaused)
        {
            this->on_Action_System_Pause();
        }
        return;
    }


    QString diskRom = QFileDialog::getOpenFileName(this, tr("Open 64DD Disk"), "", "N64DD Disk Image (*.ndd *.d64 *.zip *.7z)");
    if (diskRom.isEmpty())
    {
        if (isRunning && !isPaused)
        {
            this->on_Action_System_Pause();
        }
        return;
    }

    if (this->ui_Widgets->currentIndex() != 0)
    {
        this->ui_NoSwitchToRomBrowser = true;
    }

    this->launchEmulationThread(cartRom, diskRom);
}

void MainWindow::on_Action_System_Shutdown(void)
{
    if (CoreIsEmulationPaused())
    {
        this->on_Action_System_Pause();
    }

    if (!CoreIsEmulationRunning())
    {
        return;
    }

#ifdef NETPLAY
    if (this->kailleraSessionManager != nullptr && this->kailleraSessionManager->isGameActive())
    {
        this->kailleraSessionManager->endGame();
        return;
    }
    if (CoreHasInitKaillera())
    {
        CoreEndKailleraGame();
    }
#endif

    if (!CoreStopEmulation())
    {
        this->showErrorMessage("CoreStopEmulation() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_Exit(void)
{
    this->close();
}

void MainWindow::on_Action_System_SoftReset(void)
{
    if (!CoreResetEmulation(false))
    {
        this->showErrorMessage("CoreResetEmulation() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_HardReset(void)
{
    if (!CoreResetEmulation(true))
    {
        this->showErrorMessage("CoreResetEmulation() Failed", QString::fromStdString(CoreGetError()));
    }
}
void MainWindow::on_Action_System_Pause(void)
{
    if (!this->action_System_Pause->isEnabled())
    {
        return;
    }

    bool isPaused = CoreIsEmulationPaused();

    bool ret;
    QString error;

    if (!isPaused)
    {
        ret = CorePauseEmulation();
        error = "CorePauseEmulation() Failed";
    }
    else
    {
        ret = CoreResumeEmulation();
        error = "CoreResumeEmulation() Failed";
    }

    if (!ret)
    {
        this->showErrorMessage(error, QString::fromStdString(CoreGetError()));
    }

    this->updateUI(true, (!isPaused && ret));
    this->ui_ManuallyPaused = true;
}

void MainWindow::on_Action_System_Screenshot(void)
{
    if (!CoreTakeScreenshot())
    {
        this->showErrorMessage("CoreTakeScreenshot() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_LimitFPS(void)
{
    bool enabled = this->action_System_LimitFPS->isChecked();

    if (!CoreSetSpeedLimiterState(enabled))
    {
        this->showErrorMessage("CoreSetSpeedLimiterState() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_SpeedFactor(int factor)
{
    if (!CoreSetSpeedFactor(factor))
    {
        this->showErrorMessage("CoreSetSpeedFactor() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_SaveState(void)
{
    this->ui_ManuallySavedState = true;

    if (!CoreSaveState())
    {
        this->ui_ManuallySavedState = false;
        this->showErrorMessage("CoreSaveState() Failed", QString::fromStdString(CoreGetError()));
    }
    else
    {
        OnScreenDisplaySetMessage("Saved state to slot: " + std::to_string(CoreGetSaveStateSlot()));
    }
}

void MainWindow::on_Action_System_SaveAs(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save State"), "", tr("Save State (*.state);;Project64 Save State (*.pj);;All Files (*)"));
    if (!fileName.isEmpty())
    {
        this->ui_ManuallySavedState = true;

        CoreSaveStateType type = fileName.endsWith(".pj") ? 
                                    CoreSaveStateType::Project64 :
                                    CoreSaveStateType::Mupen64Plus;

        if (!CoreSaveState(fileName.toStdU32String(), type))
        {
            this->ui_ManuallySavedState = false;
            this->showErrorMessage("CoreSaveState() Failed", QString::fromStdString(CoreGetError()));
        }
        else
        {
            OnScreenDisplaySetMessage("Saved state to: " + QDir::toNativeSeparators(fileName).toStdString());
        }
    }

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }
}

void MainWindow::on_Action_System_LoadState(void)
{
    this->ui_ManuallyLoadedState = true;

    if (!CoreLoadSaveState())
    {
        this->ui_ManuallyLoadedState = false;
        this->showErrorMessage("CoreLoadSaveState() Failed", QString::fromStdString(CoreGetError()));
    }
    else
    {
        OnScreenDisplaySetMessage("State loaded from slot: " + std::to_string(CoreGetSaveStateSlot()));
    }
}

void MainWindow::on_Action_System_Load(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    QString fileName =
        QFileDialog::getOpenFileName(this, tr("Open Save State"), "", tr("Save State (*.dat *.state *.st* *.pj*);;All Files (*)"));

    if (!fileName.isEmpty())
    {
        this->ui_ManuallyLoadedState = true;

        if (!CoreLoadSaveState(fileName.toStdU32String()))
        {
            this->ui_ManuallyLoadedState = false;
            this->showErrorMessage("CoreLoadSaveState() Failed", QString::fromStdString(CoreGetError()));
        }
        else
        {
            OnScreenDisplaySetMessage("State loaded from: " + QDir::toNativeSeparators(fileName).toStdString());
        }
    }

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }
}

void MainWindow::on_Action_System_CurrentSaveState(int slot)
{
    if (!CoreSetSaveStateSlot(slot))
    {
        this->showErrorMessage("CoreSetSaveStateSlot() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_System_Cheats(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    Dialog::CheatsDialog dialog(this);
    if (!dialog.HasFailed())
    {
        dialog.exec();
    }

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }
}

void MainWindow::on_Action_System_GSButton(void)
{
    if (!CorePressGamesharkButton(true))
    {
        this->showErrorMessage("CorePressGamesharkButton() Failed", QString::fromStdString(CoreGetError()));
    }
    else
    {
        // only hold the gameshark button for 1 second
        this->ui_GamesharkButtonTimerId = this->startTimer(1000);
    }
}

void MainWindow::on_Action_Settings_Graphics(void)
{
    CorePluginsOpenConfig(CorePluginType::Gfx, this);
}

void MainWindow::on_Action_Settings_Audio(void)
{
    CorePluginsOpenConfig(CorePluginType::Audio, this);
}

void MainWindow::on_Action_Settings_Rsp(void)
{
    CorePluginsOpenConfig(CorePluginType::Rsp, this);
}

void MainWindow::on_Action_Settings_Input(void)
{
    // Clear the plugin switch flag before opening config
    CoreSettingsSetValue(SettingsID::Internal_InputPluginSwitchRequested, false);

    CorePluginsOpenConfig(CorePluginType::Input, this);

    // Check if a plugin switch was requested (e.g., raphnet to raphnetraw)
    if (CoreSettingsGetBoolValue(SettingsID::Internal_InputPluginSwitchRequested))
    {
        CoreSettingsSetValue(SettingsID::Internal_InputPluginSwitchRequested, false);
        if (!CoreApplyPluginSettings())
        {
            this->showErrorMessage("CoreApplyPluginSettings() Failed", QString::fromStdString(CoreGetError()));
        }

        // Update input settings button enabled state
        bool hasInputConfig = CorePluginsHasConfig(CorePluginType::Input);
        this->action_Settings_Input->setEnabled(hasInputConfig);
        this->action_Toolbar_Input->setEnabled(hasInputConfig);
    }
}

void MainWindow::on_Action_Playback(void)
{
#ifdef NETPLAY
    // If already open, just bring it to front
    auto* existing = findChild<KailleraPlaybackDialog*>();
    if (existing)
    {
        existing->raise();
        existing->activateWindow();
        return;
    }

    // Initialize Kaillera if not already initialized
    if (!CoreInitKaillera())
    {
        this->showErrorMessage("Kaillera Error", QString::fromStdString(CoreGetError()));
        return;
    }

    // Ensure session manager exists so gameCallback is wired up.
    // Without this, the state machine reaches state 1 but has no
    // callback to actually start emulation.
    if (this->kailleraSessionManager == nullptr)
    {
        this->kailleraSessionManager = new KailleraSessionManager(this);
        connect(this->kailleraSessionManager, &KailleraSessionManager::gameStarted,
                this, &MainWindow::on_Kaillera_GameStarted);
        connect(this->kailleraSessionManager, &KailleraSessionManager::chatReceived,
                this, &MainWindow::on_Kaillera_ChatReceived);
        connect(&KailleraUIBridge::instance(), &KailleraUIBridge::kailleraGameChatReceived,
                this, &MainWindow::on_Kaillera_ChatReceived);
        connect(&KailleraUIBridge::instance(), &KailleraUIBridge::p2pChatReceived,
                this, &MainWindow::on_Kaillera_ChatReceived);
        connect(&KailleraUIBridge::instance(), &KailleraUIBridge::recordingFileClosed,
                this, &MainWindow::on_Kaillera_RecordingFileClosed);
        connect(this->kailleraSessionManager, &KailleraSessionManager::playerDropped,
                this, &MainWindow::on_Kaillera_PlayerDropped);
        connect(this->kailleraSessionManager, &KailleraSessionManager::gameEnded,
                this, &MainWindow::on_Kaillera_GameEnded);
    }

    // Activate playback mode so the state machine uses the playback module
    n02::activateMode(2);

    auto* dialog = new KailleraPlaybackDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::destroyed, this, [this]() {
        if (this->kailleraSessionManager != nullptr)
        {
            disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::kailleraGameChatReceived,
                       this, &MainWindow::on_Kaillera_ChatReceived);
            disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::p2pChatReceived,
                       this, &MainWindow::on_Kaillera_ChatReceived);
            disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::recordingFileClosed,
                       this, &MainWindow::on_Kaillera_RecordingFileClosed);
            delete this->kailleraSessionManager;
            this->kailleraSessionManager = nullptr;
            CoreShutdownKaillera();
            this->updateUI(this->emulationThread->isRunning(), CoreIsEmulationPaused());
        }
    });
    dialog->show();
#endif // NETPLAY
}

void MainWindow::on_Action_Settings_Settings(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();
    const QString previousTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    const QString previousIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    Dialog::SettingsDialog dialog(this);
    const int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        const QString currentTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
        const QString currentIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));
        if (currentTheme != previousTheme || currentIconTheme != previousIconTheme)
        {
            this->reapplyTheme();
        }
    }

    // reload UI,
    // because we need to keep Settings -> {type}
    // up-to-date
    this->updateActions(emulationThread->isRunning(), isPaused);

    // update core callbacks settings
    this->coreCallBacks->LoadSettings();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }
}

void MainWindow::on_Action_Settings_Plugins(void)
{
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();
    const QString previousTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    const QString previousIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }

    Dialog::SettingsDialog dialog(this);
    dialog.ShowPluginsTab();
    const int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        const QString currentTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
        const QString currentIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));
        if (currentTheme != previousTheme || currentIconTheme != previousIconTheme)
        {
            this->reapplyTheme();
        }
    }

    // reload UI,
    // because we need to keep Settings -> {type}
    // up-to-date
    this->updateActions(emulationThread->isRunning(), isPaused);

    // update core callbacks settings
    this->coreCallBacks->LoadSettings();

    if (isRunning && !isPaused)
    {
        this->on_Action_System_Pause();
    }
}

void MainWindow::on_Action_View_Toolbar(bool checked)
{
    if (!this->ui_ShowUI)
    {
        return;
    }

    CoreSettingsSetValue(SettingsID::GUI_Toolbar, checked);
    this->toolBar->setVisible(checked);
    this->ui_ShowToolbar = checked;
}

void MainWindow::on_Action_View_StatusBar(bool checked)
{
    if (!this->ui_ShowUI)
    {
        return;
    }

    CoreSettingsSetValue(SettingsID::GUI_StatusBar, checked);
    this->statusBar()->setVisible(checked);
    this->ui_ShowStatusbar = checked;
}

void MainWindow::on_Action_View_GameList(bool checked)
{
    if (checked)
    {
        this->ui_Widget_RomBrowser->ShowList();
        CoreSettingsSetValue(SettingsID::RomBrowser_ViewMode, 0);
    }
}

void MainWindow::on_Action_View_GameGrid(bool checked)
{
    if (checked)
    {
        this->ui_Widget_RomBrowser->ShowGrid();
        CoreSettingsSetValue(SettingsID::RomBrowser_ViewMode, 1);
    }
}

void MainWindow::on_Action_View_UniformSize(bool checked)
{
    this->ui_Widget_RomBrowser->SetGridViewUniformSizes(checked);
    CoreSettingsSetValue(SettingsID::RomBrowser_GridViewUniformItemSizes, checked);
}

void MainWindow::on_Action_View_Fullscreen(void)
{
    if (!CoreToggleFullscreen())
    {
        this->showErrorMessage("CoreToggleFullscreen() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_View_RefreshRoms(void)
{
    if (!this->ui_Widget_RomBrowser->IsRefreshingRomList())
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_Action_View_ClearRomCache(void)
{
    if (!CoreClearRomHeaderAndSettingsCache())
    {
        this->showErrorMessage("CoreClearRomHeaderAndSettingsCache() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Action_View_Log(void)
{
    this->logDialog.show();
}

void MainWindow::on_Action_View_Search(void)
{
    this->ui_Widget_RomBrowser->SetToggleSearch();
}

void MainWindow::on_Action_Netplay_CreateSession(void)
{
#ifdef NETPLAY
    static QWebSocket webSocket;

    Dialog::CreateNetplaySessionDialog dialog(this, &webSocket, this->ui_Widget_RomBrowser->GetModelData());
    int ret = dialog.exec();
    if (ret == QDialog::Accepted)
    {
        this->showNetplaySessionDialog(&webSocket, dialog.GetSessionJson(), dialog.GetSessionFile());
    }
#endif // NETPLAY
}

void MainWindow::on_Action_Netplay_BrowseSessions(void)
{
#ifdef NETPLAY
    // Initialize Kaillera if not already initialized
    if (!CoreInitKaillera())
    {
        this->showErrorMessage("Kaillera Error", QString::fromStdString(CoreGetError()));
        return;
    }

    // Set Kaillera app info (app name and game list)
    std::string appName = "RMG-K " + CoreGetVersion();
    // Build game list from ROM browser (null-terminated strings with double-null at end)
    // Must use std::string directly to preserve embedded null characters
    std::string gameList;
    QMap<QString, CoreRomSettings> romData = this->ui_Widget_RomBrowser->GetModelData();

    // Collect and sort game names alphabetically for Kaillera.
    // Fallback to filename when GoodName is empty/invalid to avoid creating
    // an empty first entry in the null-delimited list.
    std::vector<std::string> goodNames;
    for (auto it = romData.begin(); it != romData.end(); ++it)
    {
        std::string goodName = it.value().GoodName;
        // Strip "(unknown rom)" suffix for better Kaillera compatibility
        const std::string suffix = " (unknown rom)";
        if (goodName.size() >= suffix.size() &&
            goodName.compare(goodName.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            goodName = goodName.substr(0, goodName.size() - suffix.size());
        }

        QString displayName = QString::fromStdString(goodName).trimmed();
        if (displayName.isEmpty())
        {
            QFileInfo fileInfo(it.key());
            displayName = fileInfo.completeBaseName().trimmed();
            if (displayName.isEmpty())
            {
                displayName = fileInfo.fileName().trimmed();
            }
        }

        if (!displayName.isEmpty())
        {
            goodNames.push_back(displayName.toStdString());
        }
    }
    std::sort(goodNames.begin(), goodNames.end(), [](const std::string& a, const std::string& b) {
        return QString::fromStdString(a).compare(QString::fromStdString(b), Qt::CaseInsensitive) < 0;
    });

    for (const auto& name : goodNames)
    {
        gameList += name;
        gameList += '\0';
    }
    gameList += '\0'; // Double null terminator
    CoreSetKailleraAppInfo(appName, gameList);

    // Create Kaillera session manager
    if (this->kailleraSessionManager != nullptr)
    {
        delete this->kailleraSessionManager;
    }
    this->kailleraSessionManager = new KailleraSessionManager(this);

    // Connect signals
    connect(this->kailleraSessionManager, &KailleraSessionManager::gameStarted,
            this, &MainWindow::on_Kaillera_GameStarted);
    connect(this->kailleraSessionManager, &KailleraSessionManager::chatReceived,
            this, &MainWindow::on_Kaillera_ChatReceived);
    connect(&KailleraUIBridge::instance(), &KailleraUIBridge::kailleraGameChatReceived,
            this, &MainWindow::on_Kaillera_ChatReceived);
    connect(&KailleraUIBridge::instance(), &KailleraUIBridge::p2pChatReceived,
            this, &MainWindow::on_Kaillera_ChatReceived);
    connect(&KailleraUIBridge::instance(), &KailleraUIBridge::recordingFileClosed,
            this, &MainWindow::on_Kaillera_RecordingFileClosed);
    connect(this->kailleraSessionManager, &KailleraSessionManager::playerDropped,
            this, &MainWindow::on_Kaillera_PlayerDropped);
    connect(this->kailleraSessionManager, &KailleraSessionManager::gameEnded,
            this, &MainWindow::on_Kaillera_GameEnded);

    // Disable buttons while Kaillera dialog is open
    this->action_Netplay_BrowseSessions->setEnabled(false);
    this->action_Netplay_Start->setEnabled(false);
    this->action_System_StartRom->setEnabled(false);

    // Show Kaillera's built-in server browser dialog
    // This is a blocking call - user will select server, join/create game
    // When they start a game, gameStarted signal will be emitted
    // Dialog stays open until user closes it
    this->kailleraSessionManager->showServerDialog();

    // Dialog closed - clean up Kaillera session
    // (emulation may still be running - user can manually stop it)
    // Guard: closeEvent may have already cleaned up if the main window was closed
    if (this->kailleraSessionManager != nullptr)
    {
        disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::kailleraGameChatReceived,
                   this, &MainWindow::on_Kaillera_ChatReceived);
        disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::p2pChatReceived,
                   this, &MainWindow::on_Kaillera_ChatReceived);
        disconnect(&KailleraUIBridge::instance(), &KailleraUIBridge::recordingFileClosed,
                   this, &MainWindow::on_Kaillera_RecordingFileClosed);
        delete this->kailleraSessionManager;
        this->kailleraSessionManager = nullptr;
        CoreShutdownKaillera();

        // Re-enable buttons and update UI
        this->action_Netplay_BrowseSessions->setEnabled(true);
        this->action_Netplay_Start->setEnabled(true);
        this->action_System_StartRom->setEnabled(true);
        this->updateUI(this->emulationThread->isRunning(), CoreIsEmulationPaused());
    }
#endif // NETPLAY
}

void MainWindow::on_Action_Netplay_ViewSession(void)
{
#ifdef NETPLAY
    if (this->netplaySessionDialog != nullptr &&
        this->netplaySessionDialog->isHidden())
    {
        this->netplaySessionDialog->show();
    }
#endif
}

#ifdef NETPLAY
void MainWindow::on_Kaillera_GameStarted(QString gameName, int playerNum, int totalPlayers)
{
    // Find ROM file by game name
    QString romFile = this->findRomByName(gameName);
    if (romFile.isEmpty())
    {
        this->showErrorMessage("ROM Not Found",
            "Could not find ROM: " + gameName + "\n\nPlease add it to your ROM directory and refresh the list.");
        // Just end the Kaillera game - full cleanup happens when dialog closes
        CoreEndKailleraGame();
        return;
    }

    // Wait for any previous emulation to finish
    // This is needed when restarting after Drop
    if (this->emulationThread->isRunning())
    {
        CoreStopEmulation();
        // Wait for thread to finish with timeout
        // Use a simple loop since we're in Kaillera's message pump context
        int timeout = 5000;
        while (this->emulationThread->isRunning() && timeout > 0)
        {
            QThread::msleep(10);
            timeout -= 10;
        }
    }

    // Launch emulation with Kaillera marker
    this->emulationThread->SetRomFile(romFile);
    this->emulationThread->SetDiskFile("");
    this->emulationThread->SetNetplay("KAILLERA", 0, playerNum); // "KAILLERA" is the marker
    this->emulationThread->start();
}

void MainWindow::on_Kaillera_ChatReceived(QString nickname, QString message)
{
    // Only show in-game Kaillera chat (not lobby chat).
    if (!CoreHasInitKaillera() || !this->emulationThread->isRunning())
    {
        return;
    }

    nickname = nickname.trimmed();
    message = NormalizeOsdKailleraChatMessage(message);

    const QString localNickname = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_Username)).trimmed();
    if (!localNickname.isEmpty() && nickname.compare(localNickname, Qt::CaseInsensitive) == 0)
    {
        const auto now = std::chrono::steady_clock::now();
        while (!this->ui_PendingLocalChatEchoes.empty())
        {
            const auto age = now - this->ui_PendingLocalChatEchoes.front().time;
            if (age <= kLocalEchoMaxAge)
            {
                break;
            }
            this->ui_PendingLocalChatEchoes.pop_front();
        }

        auto pendingEcho = std::find_if(this->ui_PendingLocalChatEchoes.begin(), this->ui_PendingLocalChatEchoes.end(),
            [&message](const PendingLocalChatEcho& pending) {
                return pending.message == message;
            });
        if (pendingEcho != this->ui_PendingLocalChatEchoes.end())
        {
            this->ui_PendingLocalChatEchoes.erase(pendingEcho);
            return;
        }

        const std::string chatLine = "<" + nickname.toStdString() + "> " + message.toStdString();
        OnScreenDisplaySetKailleraChatMessageImmediate(chatLine);
    }
    else
    {
        const std::string chatLine = "<" + nickname.toStdString() + "> " + message.toStdString();
        OnScreenDisplaySetKailleraChatMessage(chatLine);
    }
}

void MainWindow::on_Kaillera_PlayerDropped(QString nickname, int playerNum)
{
    // Show notification based on who dropped
    if (playerNum == 1)
    {
        OnScreenDisplaySetMessage("Host " + nickname.toStdString() + " dropped - game ending.");
    }
    else
    {
        OnScreenDisplaySetMessage(nickname.toStdString() + " (P" + std::to_string(playerNum) + ") dropped.");
    }

    // Note: Emulation stop is handled by gameEnded signal from KailleraSessionManager
    // Player 1 drop = everyone stops, Players 2-4 drop = only they stop
}

void MainWindow::on_Kaillera_GameEnded(void)
{
    // Mark game as inactive to re-enable UI buttons
    CoreMarkKailleraGameInactive();

    OnScreenDisplaySetKailleraChatMessage("");
#ifdef NETPLAY
    this->ui_PendingLocalChatEchoes.clear();
    this->closeNetplayChatPrompt();
#endif // NETPLAY

    // Stop emulation when game ends (user dropped or was dropped)
    if (this->emulationThread->isRunning())
    {
        CoreStopEmulation();
    }
}

#ifdef NETPLAY
void MainWindow::on_Kaillera_RecordingFileClosed(void)
{
    this->refreshKailleraRecordingStorageStatus(false);
}

bool MainWindow::handleNetplayChatKeyPress(QKeyEvent *event)
{
    if (!CoreIsEmulationRunning())
    {
        return false;
    }

    if (this->kailleraSessionManager == nullptr ||
        !this->kailleraSessionManager->isGameActive())
    {
        return false;
    }

    if (this->ui_NetplayChatInputActive)
    {
        const int key = event->key();
        if ((key == Qt::Key_Escape) && !event->isAutoRepeat())
        {
            this->closeNetplayChatPrompt();
            return true;
        }

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !event->isAutoRepeat())
        {
            QString message = this->ui_NetplayChatInput.trimmed();
            if (!message.isEmpty())
            {
                const QString normalizedMessage = NormalizeOsdKailleraChatMessage(message);
                this->kailleraSessionManager->sendChatMessage(normalizedMessage);

                const QString localNickname = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::Kaillera_Username)).trimmed();
                const bool useImmediateLocalEcho = (n02::getActiveMode() != 1);
                if (useImmediateLocalEcho && !localNickname.isEmpty())
                {
                    const auto now = std::chrono::steady_clock::now();
                    while (!this->ui_PendingLocalChatEchoes.empty())
                    {
                        const auto age = now - this->ui_PendingLocalChatEchoes.front().time;
                        if (age <= kLocalEchoMaxAge)
                        {
                            break;
                        }
                        this->ui_PendingLocalChatEchoes.pop_front();
                    }

                    this->ui_PendingLocalChatEchoes.push_back({normalizedMessage, std::chrono::steady_clock::now()});
                    while (this->ui_PendingLocalChatEchoes.size() > kLocalEchoMaxEntries)
                    {
                        this->ui_PendingLocalChatEchoes.pop_front();
                    }

                    const std::string chatLine = "<" + localNickname.toStdString() + "> " + normalizedMessage.toStdString();
                    OnScreenDisplaySetKailleraChatMessageImmediate(chatLine);
                }
            }
            this->closeNetplayChatPrompt();
            return true;
        }

        if (key == Qt::Key_Backspace)
        {
            if (!this->ui_NetplayChatInput.isEmpty())
            {
                this->ui_NetplayChatInput.chop(1);
                this->updateNetplayChatPrompt();
            }
            return true;
        }

        const QString text = event->text();
        if (!text.isEmpty())
        {
            const int maxLength = 127;
            if (this->ui_NetplayChatInput.size() < maxLength)
            {
                const QChar ch = text.at(0);
                if (!ch.isNull() && (ch.isPrint() || ch.isSpace()))
                {
                    this->ui_NetplayChatInput.append(ch);
                    this->updateNetplayChatPrompt();
                }
            }
            return true;
        }

        return true;
    }

    if (event->isAutoRepeat())
    {
        return false;
    }

    QString keyBinding = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::KeyBinding_NetplayChat));
    if (keyBinding.isEmpty())
    {
        return false;
    }

    const int key = event->key();
    QKeySequence expected(keyBinding);
    QKeySequence pressed(key | event->modifiers());
    if (expected.matches(pressed) == QKeySequence::ExactMatch)
    {
        this->ui_NetplayChatInputActive = true;
        this->ui_NetplayChatInput.clear();
        this->updateNetplayChatPrompt();
        return true;
    }

    if (key == Qt::Key_Enter)
    {
        QKeySequence altPressed(Qt::Key_Return | event->modifiers());
        if (expected.matches(altPressed) == QKeySequence::ExactMatch)
        {
            this->ui_NetplayChatInputActive = true;
            this->ui_NetplayChatInput.clear();
            this->updateNetplayChatPrompt();
            return true;
        }
    }
    else if (key == Qt::Key_Return)
    {
        QKeySequence altPressed(Qt::Key_Enter | event->modifiers());
        if (expected.matches(altPressed) == QKeySequence::ExactMatch)
        {
            this->ui_NetplayChatInputActive = true;
            this->ui_NetplayChatInput.clear();
            this->updateNetplayChatPrompt();
            return true;
        }
    }

    return false;
}

void MainWindow::updateNetplayChatPrompt(void)
{
    if (!this->ui_NetplayChatInputActive)
    {
        return;
    }

    const std::string prompt = "> " + this->ui_NetplayChatInput.toStdString();
    OnScreenDisplaySetInputPrompt(prompt);
}

void MainWindow::closeNetplayChatPrompt(void)
{
    this->ui_NetplayChatInputActive = false;
    this->ui_NetplayChatInput.clear();
    OnScreenDisplaySetInputPrompt("");
}
#endif // NETPLAY

QString MainWindow::findRomByName(QString gameName)
{
    // Search ROM browser data for matching game name
    QMap<QString, CoreRomSettings> romData = this->ui_Widget_RomBrowser->GetModelData();

    // Helper to normalize game names for comparison
    auto normalizeGameName = [](QString name) -> QString {
        // Remove "(unknown rom)" suffix
        if (name.endsWith(" (unknown rom)"))
            name = name.left(name.length() - 14);
        // Remove common suffixes like version numbers, regions, etc.
        name = name.remove(QRegularExpression("\\s*\\([^)]*\\)"));  // Remove (anything)
        name = name.remove(QRegularExpression("\\s*\\[[^\\]]*\\]")); // Remove [anything]
        name = name.remove(QRegularExpression("[^a-zA-Z0-9]")); // Keep only alphanumeric
        return name.toLower();
    };

    // Strip "(unknown rom)" from search term
    QString searchName = gameName;
    if (searchName.endsWith(" (unknown rom)"))
        searchName = searchName.left(searchName.length() - 14);

    // Try exact match first (with "(unknown rom)" stripped)
    for (auto it = romData.begin(); it != romData.end(); ++it)
    {
        QString localName = QString::fromStdString(it.value().GoodName);
        if (localName.endsWith(" (unknown rom)"))
            localName = localName.left(localName.length() - 14);

        if (localName == searchName)
        {
            return it.key();
        }
    }

    // Try case-insensitive match
    for (auto it = romData.begin(); it != romData.end(); ++it)
    {
        QString localName = QString::fromStdString(it.value().GoodName);
        if (localName.endsWith(" (unknown rom)"))
            localName = localName.left(localName.length() - 14);

        if (localName.compare(searchName, Qt::CaseInsensitive) == 0)
        {
            return it.key();
        }
    }

    // Try normalized comparison (removes special chars, version numbers, etc.)
    QString normalizedSearch = normalizeGameName(searchName);
    for (auto it = romData.begin(); it != romData.end(); ++it)
    {
        QString localName = QString::fromStdString(it.value().GoodName);
        QString normalizedLocal = normalizeGameName(localName);

        if (normalizedLocal == normalizedSearch)
        {
            return it.key();
        }
    }

    // Try substring match (if one contains the other)
    if (!normalizedSearch.isEmpty())
    {
        for (auto it = romData.begin(); it != romData.end(); ++it)
        {
            QString localName = QString::fromStdString(it.value().GoodName);
            QString normalizedLocal = normalizeGameName(localName);

            if (!normalizedLocal.isEmpty() &&
                (normalizedLocal.contains(normalizedSearch) || normalizedSearch.contains(normalizedLocal)))
            {
                return it.key();
            }
        }
    }

    return QString(); // Not found
}
#endif // NETPLAY

void MainWindow::on_Action_Help_Github(void)
{
    QDesktopServices::openUrl(QUrl("https://github.com/Jay-Day/RMG-K"));
}

void MainWindow::on_Action_Help_About(void)
{
    Dialog::AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_Action_Help_Update(void)
{
#ifdef UPDATER
    this->checkForUpdates(false, true);
#endif // UPDATER
}

void MainWindow::on_Action_Audio_IncreaseVolume(void)
{
    CoreIncreaseVolume();
}

void MainWindow::on_Action_Audio_DecreaseVolume(void)
{
    CoreDecreaseVolume();
}

void MainWindow::on_Action_Audio_ToggleVolumeMute(void)
{
    if (!CoreToggleMuteVolume())
    {
        this->showErrorMessage("CoreToggleMuteVolume() Failed", QString::fromStdString(CoreGetError()));
    }
}

void MainWindow::on_Emulation_Started(void)
{
    // only clear log dialog when we've gone over the limit
    if (this->logDialog.GetLineCount() >= 500000)
    {
        this->logDialog.Clear();
    }

    this->ui_MessageBoxList.clear();
    this->ui_DebugCallbackErrors.clear();
}

void MainWindow::on_Emulation_Finished(bool ret, QString error)
{
#ifdef _WIN32
    this->restoreDisplayMode();
#endif

    if (!ret)
    {
        // whatever we do on failure,
        // always return to the rombrowser
        this->ui_NoSwitchToRomBrowser = false;
    }

#ifdef NETPLAY
    if (this->netplaySessionDialog != nullptr)
    {
        this->netplaySessionDialog->deleteLater();
        this->netplaySessionDialog = nullptr;
    }
#endif // NETPLAY

    if (!this->ui_QuitAfterEmulation &&
        !this->ui_NoSwitchToRomBrowser &&
        this->ui_RefreshRomListAfterEmulation)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
        this->ui_RefreshRomListAfterEmulation = false;
    }

    if (this->ui_FullscreenTimerId != 0)
    {
        this->killTimer(this->ui_FullscreenTimerId);
        this->ui_FullscreenTimerId = 0;
    }

    if (this->ui_CheckVideoSizeTimerId != 0)
    {
        this->killTimer(this->ui_CheckVideoSizeTimerId);
        this->ui_CheckVideoSizeTimerId = 0;
    }

    if (this->ui_QuitAfterEmulation)
    {
        // show error message when
        // the user has requested we quit
        // after emulation
        if (!ret)
        {
            this->showErrorMessage("EmulationThread::run Failed", error);
        }

        this->ui_ForceClose = true;
        this->close();
        return;
    }

    // always refresh UI
    this->updateUI(false, false);

    // show error message to the user
    // after switching back to the ROM browser
    if (!ret)
    {
        this->showErrorMessage("EmulationThread::run Failed", error);
    }
}

void MainWindow::on_RomBrowser_PlayGame(QString file)
{
    this->launchEmulationThread(file);
}

void MainWindow::on_RomBrowser_PlayGameWith(CoreRomType type, QString file)
{
    QString mainRom;
    QString otherRom;

    if (type == CoreRomType::Cartridge)
    { // cartridge
        mainRom = file;
        otherRom = QFileDialog::getOpenFileName(this, tr("Open 64DD Disk"), "", "N64DD Disk Image (*.ndd *.d64 *.zip *.7z)");
    }
    else
    { // disk
        mainRom = QFileDialog::getOpenFileName(this, tr("Open N64 ROM"), "", "N64 ROMs (*.n64 *.z64 *.v64 *.zip *.7z)");
        otherRom = file;
    }

    if (mainRom.isEmpty() || otherRom.isEmpty())
    {
        return;
    }

    this->launchEmulationThread(mainRom, otherRom);
}

void MainWindow::on_RomBrowser_PlayGameWithDisk(QString cartridge, QString disk)
{
    this->launchEmulationThread(cartridge, disk);
}

void MainWindow::on_RomBrowser_PlayGameWithSlot(QString file, int slot)
{
    this->launchEmulationThread(file, "", false, slot);
}

void MainWindow::on_RomBrowser_ChangeRomDirectory(void)
{
    QString currentDir = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::RomBrowser_Directory));
    if (!QDir(currentDir).exists())
    {
        currentDir = "";
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("Select ROM Directory"), currentDir);
    if (!dir.isEmpty() && currentDir != dir)
    {
        CoreSettingsSetValue(SettingsID::RomBrowser_Directory, dir.toStdString());
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_RomBrowser_RomInformation(QString file)
{
    bool isRefreshingRomList = this->ui_Widget_RomBrowser->IsRefreshingRomList();
    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->StopRefreshRomList();
    }

    CoreRomType romType;
    CoreRomHeader romHeader;
    CoreRomSettings romSettings;

    if (!CoreGetCachedRomHeaderAndSettings(file.toStdU32String(), &romType, &romHeader, nullptr, &romSettings))
    {
        this->showErrorMessage("CoreGetCachedRomHeaderAndSettings() Failed", QString::fromStdString(CoreGetError()));
        return;
    }

    Dialog::RomInfoDialog dialog(this, file, romType, romHeader, romSettings);
    dialog.exec();

    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_RomBrowser_EditGameSettings(QString file)
{
    bool isRefreshingRomList = this->ui_Widget_RomBrowser->IsRefreshingRomList();
    const QString previousTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    const QString previousIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));
    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->StopRefreshRomList();
    }

    Dialog::SettingsDialog dialog(this, file);
    dialog.ShowGameTab();
    const int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        const QString currentTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
        const QString currentIconTheme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_IconTheme));
        if (currentTheme != previousTheme || currentIconTheme != previousIconTheme)
        {
            this->reapplyTheme();
        }
    }

    this->updateActions(false, false);
    this->coreCallBacks->LoadSettings();

    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_RomBrowser_EditGameInputSettings(QString file)
{
    bool isRefreshingRomList = this->ui_Widget_RomBrowser->IsRefreshingRomList();
    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->StopRefreshRomList();
    }

    if (!CorePluginsOpenROMConfig(CorePluginType::Input, this, file.toStdU32String()))
    {
        this->showErrorMessage("CorePluginsOpenROMConfig() Failed", QString::fromStdString(CoreGetError()));
        return;
    }

    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_RomBrowser_Cheats(QString file)
{
    bool isRefreshingRomList = this->ui_Widget_RomBrowser->IsRefreshingRomList();
    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->StopRefreshRomList();
    }

    Dialog::CheatsDialog dialog(this, file);
    if (!dialog.HasFailed())
    {
        dialog.exec();
    }

    if (isRefreshingRomList)
    {
        this->ui_Widget_RomBrowser->RefreshRomList();
    }
}

void MainWindow::on_Netplay_PlayGame(QString file, QString address, int port, int player)
{
    this->launchEmulationThread(file, address, port, player);
}

void MainWindow::on_NetplaySessionDialog_rejected()
{
#ifdef NETPLAY
    bool isRunning = CoreIsEmulationRunning();
    bool isPaused = CoreIsEmulationPaused();

    if (this->netplaySessionDialog != nullptr)
    {
        this->netplaySessionDialog->deleteLater();
        this->netplaySessionDialog = nullptr;
    }

    // force refresh of actions
    this->updateActions(isRunning, isPaused);
#endif // NETPLAY
}

void MainWindow::on_VidExt_Init(VidExtRenderMode renderMode)
{
    this->ui_VidExtRenderMode   = renderMode;
    this->ui_VidExtForceSetMode = true;

    if (CoreSettingsGetBoolValue(SettingsID::GUI_OpenGLES))
    {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSwapInterval(0);
        format.setMajorVersion(2);
        format.setMinorVersion(0);
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        QSurfaceFormat::setDefaultFormat(format);
    }
    else
    {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSwapInterval(0);
        format.setMajorVersion(3);
        format.setMinorVersion(3);
        format.setRenderableType(QSurfaceFormat::OpenGL);
        QSurfaceFormat::setDefaultFormat(format);
    }

    if (renderMode == VidExtRenderMode::OpenGL)
    {
        this->ui_Widget_OpenGL = new Widget::OGLWidget(this);
        this->ui_Widget_OpenGL->installEventFilter(this->ui_EventFilter);
        this->ui_Widget_OpenGL->SetHideCursor(this->ui_HideCursorInEmulation);

        this->ui_Widgets->addWidget(this->ui_Widget_OpenGL->GetWidget());
    }
    else if (renderMode == VidExtRenderMode::Vulkan)
    {
        this->ui_Widget_Vulkan = new Widget::VKWidget(this);
        this->ui_Widget_Vulkan->installEventFilter(this->ui_EventFilter);
        this->ui_Widget_Vulkan->SetHideCursor(this->ui_HideCursorInEmulation);

        this->ui_Widgets->addWidget(this->ui_Widget_Vulkan->GetWidget());
    }

    this->updateUI(true, false);
}

void MainWindow::on_VidExt_SetupOGL(QSurfaceFormat format, QThread* thread)
{
    this->ui_Widget_OpenGL->MoveContextToThread(thread);
    // on wayland setting the surface format
    // fails for some reason, and if we set it anyways
    // ->makeCurrent() will fail in VidExt.cpp,
    // so to resolve that I've set OpenGL 3.3 as
    // default surface format in main.cpp and we
    // skip it here only on when on wayland
    if (QGuiApplication::platformName() != "wayland")
    {
        this->ui_Widget_OpenGL->setFormat(format);
    }
}

void MainWindow::on_VidExt_SetWindowedMode(int width, int height, int bps, int flags)
{
    bool returnedFromFullscreen = false;

    if (this->isFullScreen())
    {
#ifdef _WIN32
        this->restoreDisplayMode();
#endif
        returnedFromFullscreen = true;
        this->showNormal();
    }

    if (this->ui_ShowMenubar && this->menuBar()->isHidden())
    {
        this->menuBar()->show();
    }

    if (this->ui_ShowToolbar && this->toolBar->isHidden())
    {
        this->toolBar->show();
    }

    if (this->ui_ShowStatusbar && this->statusBar()->isHidden())
    {
        this->statusBar()->show();
    }

    if (!this->ui_HideCursorInEmulation && this->ui_HideCursorInFullscreenEmulation)
    {
        if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
        {
            this->ui_Widget_OpenGL->SetHideCursor(false);
        }
        else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
        {
            this->ui_Widget_Vulkan->SetHideCursor(false);
        }
    }

    if (this->ui_ShowUI)
    {
        this->removeActions();
    }

    // only resize window when we're
    // not returning from fullscreen
    if (!returnedFromFullscreen)
    {
        this->on_VidExt_ResizeWindow(width, height);
    }
}

void MainWindow::on_VidExt_SetFullscreenMode(int width, int height, int bps, int flags)
{
    if (!this->isFullScreen())
    {
#ifdef _WIN32
        if (this->ui_ExclusiveFullscreen)
        {
            this->applyExclusiveFullscreen();
        }
#endif
        this->showFullScreen();
    }

    if (!this->menuBar()->isHidden())
    {
        this->menuBar()->hide();
    }

    if (this->ui_ShowToolbar && !this->toolBar->isHidden())
    {
        this->toolBar->hide();
    }

    if (this->ui_ShowStatusbar && !this->statusBar()->isHidden())
    {
        this->statusBar()->hide();
    }

    if (!this->ui_HideCursorInEmulation && this->ui_HideCursorInFullscreenEmulation)
    {
        if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
        {
            this->ui_Widget_OpenGL->SetHideCursor(true);
        }
        else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
        {
            this->ui_Widget_Vulkan->SetHideCursor(true);
        }
    }

    if (this->ui_ShowUI)
    {
        this->addActions();
    }
}

void MainWindow::on_VidExt_ResizeWindow(int width, int height)
{
    // account for HiDPI scaling
    if (this->devicePixelRatio() != 1)
    {
        width  = static_cast<int>(std::ceil(static_cast<double>(static_cast<double>(width)  / this->devicePixelRatio())));
        height = static_cast<int>(std::ceil(static_cast<double>(static_cast<double>(height) / this->devicePixelRatio())));
    }

    if (!this->menuBar()->isHidden())
    {
        height += this->menuBar()->height();
    }

    if (!this->toolBar->isHidden())
    {
        Qt::ToolBarArea area = this->toolBarArea(this->toolBar);

        // dont resize when toolbar is floating
        if (this->toolBar->isFloating())
        {
            return;
        }

        if (area == Qt::ToolBarArea::TopToolBarArea ||
            area == Qt::ToolBarArea::BottomToolBarArea)
        {
            height += this->toolBar->height();
        }
        else if (area == Qt::ToolBarArea::LeftToolBarArea ||
                 area == Qt::ToolBarArea::RightToolBarArea)
        {
            width += this->toolBar->width();
        }
    }

    if (!this->statusBar()->isHidden())
    {
        height += this->statusBar()->height();
    }

    if (!this->ui_VidExtForceSetMode)
    {
        if (this->size() == QSize(width, height))
        {
            return;
        }
    }

    if (this->isMaximized() || this->isMinimized())
    {
        this->showNormal();
    }

    this->resize(width, height);

    // we've force set the size once,
    // we can safely disable it now
    this->ui_VidExtForceSetMode = false;
}

void MainWindow::on_VidExt_ToggleFS(bool fullscreen)
{
    if (fullscreen)
    {
        if (!this->isFullScreen())
        {
#ifdef _WIN32
            if (this->ui_ExclusiveFullscreen)
            {
                this->applyExclusiveFullscreen();
            }
#endif
            this->showFullScreen();
        }

        if (this->ui_ShowMenubar && !this->menuBar()->isHidden())
        {
            this->menuBar()->hide();
        }

        if (this->ui_ShowToolbar && !this->toolBar->isHidden())
        {
            this->toolBar->hide();
        }

        if (this->ui_ShowStatusbar && !this->statusBar()->isHidden())
        {
            this->statusBar()->hide();
        }

        if (!this->ui_HideCursorInEmulation && this->ui_HideCursorInFullscreenEmulation)
        {
            if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
            {
                this->ui_Widget_OpenGL->SetHideCursor(true);
            }
            else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
            {
                this->ui_Widget_Vulkan->SetHideCursor(true);
            }
        }

        if (this->ui_ShowUI)
        {
            this->addActions();
        }
    }
    else
    {
        if (this->isFullScreen())
        {
#ifdef _WIN32
            this->restoreDisplayMode();
#endif
            this->showNormal();
        }

        if (this->ui_ShowMenubar && this->menuBar()->isHidden())
        {
            this->menuBar()->show();
        }

        if (this->ui_ShowToolbar && this->toolBar->isHidden())
        {
            this->toolBar->show();
        }

        if (this->ui_ShowStatusbar && this->statusBar()->isHidden())
        {
            this->statusBar()->show();
        }

        if (!this->ui_HideCursorInEmulation && this->ui_HideCursorInFullscreenEmulation)
        {
            if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
            {
                this->ui_Widget_OpenGL->SetHideCursor(false);
            }
            else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
            {
                this->ui_Widget_Vulkan->SetHideCursor(false);
            }
        }

        if (this->ui_ShowUI)
        {
            this->removeActions();
        }
    }
}

void MainWindow::on_Core_DebugCallback(QList<CoreCallbackMessage> messages)
{
    // pass callback messages to the log window
    this->logDialog.AddMessages(messages);

    // only display in statusbar when emulation is running
    if (!this->emulationThread->isRunning())
    {
        return;
    }

    // attempt to find last core message
    CoreCallbackMessage statusbarMessage = {};
    qsizetype i = messages.size() - 1;
    for (; i >= 0; i--)
    {
        if (messages[i].Context.startsWith("[CORE]") &&
            messages[i].Type != CoreDebugMessageType::Verbose &&
            !messages[i].Message.startsWith("IS64:"))
        {
            statusbarMessage = messages[i];
            break;
        }
    }
    if (i < 0)
    { // no wanted core message found
        return;
    }

    if (statusbarMessage.Type == CoreDebugMessageType::Error)
    {
        // when we've reached 50 of the same error in the same
        // emulation run, we'll stop displaying it
        if (this->ui_DebugCallbackErrors.count(statusbarMessage.Message) < 50)
        {
            this->showErrorMessage("Core Error", statusbarMessage.Message, false);
        }
        this->ui_DebugCallbackErrors.append(statusbarMessage.Message);
        return;
    }

    if (!this->ui_ShowStatusbar)
    {
        return;
    }

    this->ui_StatusBar_Label->setText(statusbarMessage.Message);

    // reset label deletion timer
    if (this->ui_ResetStatusBarTimerId != 0)
    {
        this->killTimer(this->ui_ResetStatusBarTimerId);
    }
    this->ui_ResetStatusBarTimerId = this->startTimer(this->ui_StatusBarTimerTimeout * 1000);
}

void MainWindow::on_Core_StateCallback(CoreStateCallbackType type, int value)
{
    switch (type)
    {
        default:
            break;
        case CoreStateCallbackType::EmulationState:
        {
            // update Pause button
            this->action_System_Pause->setChecked(value == static_cast<int>(CoreEmulationState::Paused));
            // update OSD state
            if (value == (int)CoreEmulationState::Paused)
            {
                OnScreenDisplayPause();
            }
            else if (value == (int)CoreEmulationState::Running)
            {
                OnScreenDisplayResume();
            }
        } break;
        case CoreStateCallbackType::SaveStateSlot:
        {
            QAction* slotAction  = this->ui_SlotActions[value];
            QString dateTimeText = this->getSaveStateSlotDateTimeText(slotAction);
            std::string message  = "Selected save slot: " + std::to_string(value);

            if (this->ui_LoadSaveStateSlotTimerId != -1)
            {
                return;
            }

            // add date and time when available
            if (!dateTimeText.isEmpty())
            {
                message += " [";
                message += dateTimeText.toStdString();
                message += "]";
            }
            else
            {
                message += " [N/A]";
            }

            // display message
            OnScreenDisplaySetMessage(message);

            // update UI
            this->ui_SlotActions[value]->setChecked(true);
        } break;
        case CoreStateCallbackType::SpeedFactor:
        {
            OnScreenDisplaySetMessage("Playback speed: " + std::to_string(value) + "%");
        } break;
        case CoreStateCallbackType::AudioVolume:
        {
            OnScreenDisplaySetMessage("Volume: " + std::to_string(value) + "%");
        } break;
        case CoreStateCallbackType::AudioMute:
        {
            if (value == 0)
            {
                OnScreenDisplaySetMessage("Volume unmuted");
            }
            else
            {
                OnScreenDisplaySetMessage("Volume muted");
            }
        } break;
        case CoreStateCallbackType::SaveStateLoaded:
        {
            if (this->ui_LoadSaveStateSlotTimerId != -1 && value == 0)
            {
                this->ui_LoadSaveStateSlotCounter++;
                if (this->ui_LoadSaveStateSlotCounter >= 5)
                { // give up after 5 attempts
                    this->showErrorMessage("Failed to load save state");
                    this->ui_LoadSaveStateSlotCounter = 0;
                    this->ui_LoadSaveStateSlotTimerId = -1;
                    this->ui_LoadSaveStateSlot        = -1;
                }
                else
                {
                    this->ui_LoadSaveStateSlotTimerId = this->startTimer(500);   
                }
            }
            else if (this->ui_LoadSaveStateSlotTimerId != -1 && value != 0)
            {
                this->ui_LoadSaveStateSlotCounter = 0;
                this->ui_LoadSaveStateSlotTimerId = -1;
                this->ui_LoadSaveStateSlot        = -1;
            }
            else if (value == 0)
            {
                OnScreenDisplaySetMessage("Failed to load save state.");
            }
            else if (!this->ui_ManuallyLoadedState)
            {
                OnScreenDisplaySetMessage("Loaded save state.");
            }

            this->ui_ManuallyLoadedState = false;
        } break;
        case CoreStateCallbackType::SaveStateSaved:
        {
            if (value == 0)
            {
                OnScreenDisplaySetMessage("Failed to save state.");
            }
            else if (!this->ui_ManuallySavedState)
            {
                OnScreenDisplaySetMessage("Saved state.");
            }

            // refresh savestate slot times in 1 second,
            // kill any existing timers
            if (this->ui_UpdateSaveStateSlotTimerId != 0)
            {
                this->killTimer(this->ui_UpdateSaveStateSlotTimerId);
                this->ui_UpdateSaveStateSlotTimerId = 0;
            }
            this->ui_UpdateSaveStateSlotTimerId = this->startTimer(1000);

            this->ui_ManuallySavedState = false;
        } break;
        case CoreStateCallbackType::ScreenshotCaptured:
        {
            if (value == 0)
            {
                OnScreenDisplaySetMessage("Failed to capture screenshot.");
            }
            else
            {
                OnScreenDisplaySetMessage("Captured screenshot.");
            }
        } break;
    }
}

void MainWindow::on_VidExt_Quit(void)
{
    if (this->ui_VidExtRenderMode == VidExtRenderMode::OpenGL)
    {
        this->ui_Widgets->removeWidget(this->ui_Widget_OpenGL->GetWidget());
        this->ui_Widget_OpenGL->GetWidget()->deleteLater();
        this->ui_Widget_OpenGL = nullptr;
    }
    else if (this->ui_VidExtRenderMode == VidExtRenderMode::Vulkan)
    {
        this->ui_Widgets->removeWidget(this->ui_Widget_Vulkan->GetWidget());
        this->ui_Widget_Vulkan->GetWidget()->deleteLater();
        this->ui_Widget_Vulkan = nullptr;
    }

    this->ui_VidExtRenderMode = VidExtRenderMode::Invalid;
}
