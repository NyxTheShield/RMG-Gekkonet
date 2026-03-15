/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "KailleraNetplayDialog.hpp"
#include "KailleraServerBrowserDialog.hpp"
#include "KailleraP2PDialog.hpp"
#include "KailleraWaitingGamesDialog.hpp"

#ifdef _WIN32

#include "../../KailleraUIBridge.hpp"

#include <RMG-Core/Settings.hpp>
#include <RMG-Core/Kaillera.hpp>
#include <RMG-Core/Emulation.hpp>

#include "n02_client.h"
#include "kailleraclient.h"
#include "kcore/kaillera_core.h"
#include "core/p2p_core.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QTabBar>
#include <QEventLoop>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QUrl>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QProcess>
#include <QSettings>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStylePainter>
#include <QStyledItemDelegate>
#include <QStringList>

#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <future>
#include <algorithm>

#include <windows.h>

static constexpr int kMaxP2PRecentEntries = 12;

static QString getKailleraRecordsDirectory()
{
    return QString::fromStdString(CoreGetKailleraRecordsDirectory());
}

static QIcon themedLineIcon(const QString& iconName)
{
    const bool darkTheme = QApplication::palette().window().color().value() < 128;
    const QString iconPath = QString(":/icons/%1/svg/%2.svg")
        .arg(darkTheme ? "white" : "black", iconName);
    return QIcon(iconPath);
}

static QIcon themedFavoriteIcon(const QString& iconName)
{
    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    if (theme == "Fusion Dark")
    {
        return QIcon(QString(":/icons/white/svg/%1.svg").arg(iconName));
    }

    return themedLineIcon(iconName);
}

static bool isPrivateHostPort(const QString& hostPort)
{
    if (hostPort.startsWith("10.") || hostPort.startsWith("192.168.") || hostPort.startsWith("127."))
    {
        return true;
    }

    if (hostPort.startsWith("172."))
    {
        QStringList octets = hostPort.split('.');
        if (octets.size() >= 2)
        {
            bool ok = false;
            const int second = octets[1].toInt(&ok);
            if (ok && second >= 16 && second <= 31)
            {
                return true;
            }
        }
    }

    return false;
}

class SearchableComboBox final : public QComboBox
{
public:
    explicit SearchableComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
        setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    }

    void setDisplayTextInset(int inset)
    {
        m_displayTextInset = inset;
        update();
    }

    void showPopup() override
    {
        if (count() == 0)
        {
            return;
        }

        if (m_popup == nullptr)
        {
            m_popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
            m_popup->setObjectName("KailleraSearchPopup");
            m_popup->installEventFilter(this);
            auto* popupLayout = new QVBoxLayout(m_popup);
            popupLayout->setContentsMargins(8, 8, 8, 8);
            popupLayout->setSpacing(6);

            m_searchEdit = new QLineEdit(m_popup);
            m_searchEdit->setPlaceholderText("Search ROMs...");
            popupLayout->addWidget(m_searchEdit);

            m_listWidget = new QListWidget(m_popup);
            m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
            m_listWidget->setUniformItemSizes(true);
            popupLayout->addWidget(m_listWidget);

            connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
                refreshPopupItems(text);
            });
            connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
                if (m_listWidget == nullptr || m_listWidget->count() == 0)
                {
                    return;
                }

                QListWidgetItem* item = m_listWidget->currentItem();
                if (item == nullptr)
                {
                    item = m_listWidget->item(0);
                }
                activatePopupItem(item);
            });
            connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
                activatePopupItem(item);
            });
            connect(m_listWidget, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
                activatePopupItem(item);
            });
        }

        refreshPopupItems(QString());
        m_searchEdit->clear();

        const int popupWidth = qMax(width(), 340);
        const int visibleItems = qMin(10, qMax(1, m_listWidget->count()));
        const int rowHeight = m_listWidget->sizeHintForRow(0) > 0 ? m_listWidget->sizeHintForRow(0) : 22;
        const int popupHeight = 16 + m_searchEdit->sizeHint().height() + 6 + (visibleItems * rowHeight) + 16;

        const QPoint below = mapToGlobal(QPoint(0, height()));
        m_popup->resize(popupWidth, popupHeight);
        m_popup->move(below);
        m_popup->show();
        m_searchEdit->setFocus();
    }

    void hidePopup() override
    {
        if (m_popup != nullptr)
        {
            m_popup->hide();
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_popup && event->type() == QEvent::Hide && QApplication::mouseButtons() != Qt::NoButton)
        {
            m_suppressPopupUntilMouseRelease = true;
        }

        return QComboBox::eventFilter(watched, event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (m_suppressPopupUntilMouseRelease)
        {
            event->accept();
            return;
        }

        QComboBox::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_suppressPopupUntilMouseRelease)
        {
            m_suppressPopupUntilMouseRelease = false;
            event->accept();
            return;
        }

        QComboBox::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        if (m_displayTextInset <= 0)
        {
            QComboBox::paintEvent(event);
            return;
        }

        Q_UNUSED(event);

        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        painter.drawComplexControl(QStyle::CC_ComboBox, option);

        QStyleOptionComboBox labelOption(option);
        labelOption.rect = style()->subControlRect(
            QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
        labelOption.rect.adjust(m_displayTextInset, 0, 0, 0);
        painter.drawControl(QStyle::CE_ComboBoxLabel, labelOption);
    }

private:
    void refreshPopupItems(const QString& filter)
    {
        if (m_listWidget == nullptr)
        {
            return;
        }

        const QString normalizedFilter = filter.trimmed();
        const QString currentValue = currentText();

        m_listWidget->clear();
        int selectedRow = -1;
        for (int i = 0; i < count(); ++i)
        {
            const QString itemText = itemTextAt(i);
            if (!normalizedFilter.isEmpty()
                && !itemText.contains(normalizedFilter, Qt::CaseInsensitive))
            {
                continue;
            }

            auto* item = new QListWidgetItem(itemText, m_listWidget);
            item->setData(Qt::UserRole, i);
            if (selectedRow < 0 && itemText == currentValue)
            {
                selectedRow = m_listWidget->count() - 1;
            }
        }

        if (m_listWidget->count() > 0)
        {
            m_listWidget->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
        }
    }

    QString itemTextAt(int index) const
    {
        return QComboBox::itemText(index);
    }

    void activatePopupItem(QListWidgetItem* item)
    {
        if (item == nullptr)
        {
            return;
        }

        const int sourceIndex = item->data(Qt::UserRole).toInt();
        if (sourceIndex >= 0 && sourceIndex < count())
        {
            setCurrentIndex(sourceIndex);
        }
        hidePopup();
    }

    QFrame* m_popup = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
    bool m_suppressPopupUntilMouseRelease = false;
    int m_displayTextInset = 0;
};

class CenteredIconDelegate final : public QStyledItemDelegate
{
public:
    explicit CenteredIconDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem viewOption(option);
        initStyleOption(&viewOption, index);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::UserRole));
        const bool hasText = !viewOption.text.isEmpty();
        if (icon.isNull() || hasText)
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        viewOption.icon = QIcon();
        viewOption.text.clear();
        viewOption.features &= ~QStyleOptionViewItem::HasDecoration;
        viewOption.features &= ~QStyleOptionViewItem::HasDisplay;
        QStyledItemDelegate::paint(painter, viewOption, index);

        const QSize iconSize = viewOption.decorationSize.isValid()
            ? viewOption.decorationSize
            : QSize(16, 16);
        const QRect iconRect(
            viewOption.rect.x() + (viewOption.rect.width() - iconSize.width()) / 2,
            viewOption.rect.y() + (viewOption.rect.height() - iconSize.height()) / 2,
            iconSize.width(),
            iconSize.height());
        icon.paint(painter, iconRect, Qt::AlignCenter,
            option.state & QStyle::State_Enabled ? QIcon::Normal : QIcon::Disabled);
    }
};

class FloatingCornerButtonFilter final : public QObject
{
public:
    FloatingCornerButtonFilter(QWidget* container, QWidget* button, int rightMargin, int bottomMargin)
        : QObject(container)
        , m_container(container)
        , m_button(button)
        , m_rightMargin(rightMargin)
        , m_bottomMargin(bottomMargin)
    {
        reposition();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_container && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        {
            reposition();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void reposition()
    {
        if (m_container == nullptr || m_button == nullptr)
        {
            return;
        }

        const int x = qMax(0, m_container->width() - m_button->width() - m_rightMargin);
        const int y = qMax(0, m_container->height() - m_button->height() - m_bottomMargin);
        m_button->move(x, y);
        m_button->raise();
    }

    QWidget* m_container = nullptr;
    QWidget* m_button = nullptr;
    int m_rightMargin = 0;
    int m_bottomMargin = 0;
};

static void attachFloatingCornerButton(QWidget* container, QWidget* button, int rightMargin, int bottomMargin)
{
    if (container == nullptr || button == nullptr)
    {
        return;
    }

    container->installEventFilter(
        new FloatingCornerButtonFilter(container, button, rightMargin, bottomMargin));
}

static void configureLauncherButtonMetrics(QPushButton* button)
{
    if (button == nullptr)
    {
        return;
    }

    button->setMinimumHeight(32);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

static void configureLauncherTallButtonMetrics(QPushButton* button)
{
    if (button == nullptr)
    {
        return;
    }

    button->setMinimumHeight(56);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

static void configureLauncherComboMetrics(QComboBox* combo)
{
    if (combo == nullptr)
    {
        return;
    }

    combo->setMinimumHeight(32);
    combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

static bool isFusionFamilyTheme(const QString& theme)
{
    return theme == "Fusion" || theme == "Fusion Warm" || theme == "Fusion Dark";
}

static void configureLauncherLineEditMetrics(QLineEdit* edit, const QString& theme)
{
    if (edit == nullptr)
    {
        return;
    }

    if (isFusionFamilyTheme(theme))
    {
        edit->setFixedHeight(32);
    }
}

static QWidget* createLauncherSectionPane(
    const QString& theme, QWidget* parent, const QString& title, QVBoxLayout** outLayout)
{
    const bool useTitledGroup = (theme == "Modern");
    QWidget* pane = nullptr;
    QVBoxLayout* layout = nullptr;

    if (useTitledGroup)
    {
        auto* group = new QGroupBox(title, parent);
        group->setObjectName("KailleraPane");
        layout = new QVBoxLayout(group);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(0);
        pane = group;
    }
    else
    {
        auto* widget = new QWidget(parent);
        layout = new QVBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        auto* titleLabel = new QLabel(title, widget);
        titleLabel->setObjectName("KailleraSectionCaption");
        layout->addWidget(titleLabel, 0, Qt::AlignLeft);
        pane = widget;
    }

    if (outLayout != nullptr)
    {
        *outLayout = layout;
    }
    return pane;
}

static void setPaletteRoleForAllGroups(QPalette& palette, QPalette::ColorRole role, const QColor& color)
{
    palette.setColor(QPalette::Active, role, color);
    palette.setColor(QPalette::Inactive, role, color);
    palette.setColor(QPalette::Disabled, role, color);
}

static QColor blendColors(const QColor& from, const QColor& to, qreal amount)
{
    const qreal clamped = std::clamp(amount, 0.0, 1.0);
    return QColor(
        static_cast<int>(from.red() + ((to.red() - from.red()) * clamped)),
        static_cast<int>(from.green() + ((to.green() - from.green()) * clamped)),
        static_cast<int>(from.blue() + ((to.blue() - from.blue()) * clamped)));
}

static QString cssColor(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

static void configureLauncherAccentPalette(QPushButton* button)
{
    if (button == nullptr)
    {
        return;
    }

    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    if (!isFusionFamilyTheme(theme))
    {
        return;
    }

    const QPalette appPalette = QApplication::palette();
    const QColor accent = appPalette.color(QPalette::Highlight);
    const QColor accentText = appPalette.color(QPalette::HighlightedText);
    const QColor disabledText = appPalette.color(QPalette::Disabled, QPalette::ButtonText);

    QPalette palette = button->palette();
    setPaletteRoleForAllGroups(palette, QPalette::Button, accent);
    setPaletteRoleForAllGroups(palette, QPalette::Window, accent);
    setPaletteRoleForAllGroups(palette, QPalette::Base, accent);
    setPaletteRoleForAllGroups(palette, QPalette::Light, accent.lighter(130));
    setPaletteRoleForAllGroups(palette, QPalette::Midlight, accent.lighter(115));
    setPaletteRoleForAllGroups(palette, QPalette::Mid, accent.darker(115));
    setPaletteRoleForAllGroups(palette, QPalette::Dark, accent.darker(140));
    setPaletteRoleForAllGroups(palette, QPalette::Shadow, accent.darker(180));
    setPaletteRoleForAllGroups(palette, QPalette::ButtonText, accentText);
    setPaletteRoleForAllGroups(palette, QPalette::WindowText, accentText);
    setPaletteRoleForAllGroups(palette, QPalette::Text, accentText);
    setPaletteRoleForAllGroups(palette, QPalette::BrightText, accentText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    button->setForegroundRole(QPalette::ButtonText);
    button->setPalette(palette);
}

class LauncherTabBar final : public QTabBar
{
public:
    explicit LauncherTabBar(QWidget* parent = nullptr)
        : QTabBar(parent)
    {
        setDrawBase(false);
        setElideMode(Qt::ElideNone);
    }

    QSize tabSizeHint(int index) const override
    {
        QSize hint = QTabBar::tabSizeHint(index);
        if (count() <= 0)
        {
            return hint;
        }

        const auto* tabWidget = qobject_cast<const QTabWidget*>(parentWidget());
        int availableWidth = tabWidget != nullptr ? tabWidget->width() : width();
        if (availableWidth <= 0)
        {
            return hint;
        }

        // Reserve a little room at the edges so the outer tab borders don't clip.
        availableWidth -= 24;
        hint.setWidth(qMax(hint.width(), availableWidth / count()));
        return hint;
    }
};

class LauncherTabWidget final : public QTabWidget
{
public:
    explicit LauncherTabWidget(QWidget* parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new LauncherTabBar(this));
        setUsesScrollButtons(false);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QTabWidget::resizeEvent(event);
        if (tabBar() != nullptr)
        {
            const bool expanding = tabBar()->expanding();
            tabBar()->setExpanding(!expanding);
            tabBar()->setExpanding(expanding);
            tabBar()->updateGeometry();
            tabBar()->update();
        }
    }
};

static QString buildLauncherStyleSheet(const QString& theme)
{
    const bool modern = (theme == "Modern");
    const bool fusionLike = (theme == "Fusion" || theme == "Fusion Warm" || theme == "Fusion Dark");
    if (!modern && !fusionLike)
    {
        return QString();
    }

    const QString paneRadius = modern ? "10px" : "2px";
    const QString tabRadius = modern ? "8px" : "0px";
    const QString controlRadius = modern ? "7px" : "2px";
    const QString fabRadius = modern ? "23px" : "6px";
    const QString dividerColor = modern ? "palette(mid)" : "palette(midlight)";
    const QString tabMargin = modern ? "0px" : "2px";
    const QString tabWeight = modern ? "600" : "500";
    const QString tabBarLeftOffset = modern ? "8px" : "12px";
    const QString comboDropWidth = modern ? "24px" : "32px";
    const QString comboDropRadius = modern ? controlRadius : "0px";
    const QString comboDropBackground = modern ? "transparent" : "palette(button)";
    const QString lineEditVerticalPadding = modern ? "5px" : "1px";
    const QPalette appPalette = QApplication::palette();
    const QColor windowColor = appPalette.window().color();
    const bool darkTheme = windowColor.value() < 128;
    const QColor inactiveTabColor = darkTheme
        ? blendColors(windowColor, QColor(Qt::white), modern ? 0.18 : 0.15)
        : blendColors(windowColor, QColor(Qt::white), modern ? 0.48 : 0.38);
    const QString inactiveTabBackground = cssColor(inactiveTabColor);
    const QString comboArrowIcon = QString(":/icons/%1/svg/arrow-down-s-line.svg")
        .arg(darkTheme ? "white" : "black");

    QString style = QString(
        "QDialog#KailleraLauncherDialog {"
        "  background-color: palette(window);"
        "}"
        "QWidget#KailleraPane, QGroupBox#KailleraPane, QWidget#KailleraPaneGameList {"
        "  border: 1px solid palette(mid);"
        "  border-radius: %1;"
        "  background-color: palette(base);"
        "}"
        "QLabel#KailleraFieldLabel {"
        "  color: palette(text);"
        "  font-weight: 600;"
        "}"
        "QLabel#KailleraSectionCaption {"
        "  color: palette(mid);"
        "  font-weight: 600;"
        "  font-size: 15px;"
        "}"
        "QTabWidget#KailleraLauncherTabs::pane {"
        "  border-top: 1px solid palette(mid);"
        "  border-left: none;"
        "  border-right: none;"
        "  border-bottom: none;"
        "  border-radius: 0px;"
        "  background-color: palette(base);"
        "  top: -1px;"
        "}"
        "QTabWidget#KailleraLauncherTabs::tab-bar {"
        "  left: %2;"
        "}"
        "QTabWidget#KailleraLauncherTabs QTabBar::tab {"
        "  border: 1px solid palette(mid);"
        "  border-top-left-radius: %3;"
        "  border-top-right-radius: %3;"
        "  padding: 7px 14px;"
        "  margin-right: %4;"
        "}"
        "QTabWidget#KailleraLauncherTabs QTabBar::tab:selected {"
        "  background-color: palette(base);"
        "  border-bottom: none;"
        "  font-weight: %5;"
        "}"
        "QLineEdit#KailleraInput {"
        "  border: 1px solid palette(mid);"
        "  border-radius: %6;"
        "  background-color: palette(base);"
        "  padding: %13 8px;"
        "}"
        "QLineEdit#KailleraInput:focus {"
        "  border-color: palette(highlight);"
        "}"
        "QFrame#KailleraSearchPopup {"
        "  border: 1px solid palette(mid);"
        "  border-radius: %1;"
        "  background-color: palette(base);"
        "}"
        "QTextBrowser#KailleraSurface, QTableWidget#KailleraSurface {"
        "  border: none;"
        "  background: transparent;"
        "}"
        "QTableWidget#KailleraSurface {"
        "  gridline-color: transparent;"
        "  alternate-background-color: palette(alternate-base);"
        "  selection-background-color: transparent;"
        "  selection-color: palette(text);"
        "}"
        "QTableWidget#KailleraSurface::item:selected {"
        "  background: transparent;"
        "  color: palette(text);"
        "}"
        "QFrame#KailleraDivider {"
        "  background-color: %12;"
        "  max-height: 1px;"
        "}").arg(
            paneRadius,
            tabBarLeftOffset,
            tabRadius,
            tabMargin,
            tabWeight,
            controlRadius,
            comboDropWidth,
            comboDropRadius,
            comboDropBackground,
            comboArrowIcon,
            fabRadius,
            dividerColor,
            lineEditVerticalPadding);

    style += QString(
        "QTabWidget#KailleraLauncherTabs QTabBar::tab:!selected {"
        "  background-color: %1;"
        "}").arg(inactiveTabBackground);

    style += QString(
        "QTableWidget#KailleraSurface[launcherServerTable=\"true\"] {"
        "  border-top: 1px solid palette(mid);"
        "  border-left: 1px solid palette(mid);"
        "}"
        "QTableWidget#KailleraSurface[launcherP2PTable=\"true\"] {"
        "  border: 1px solid palette(mid);"
        "}");

    if (modern)
    {
        style += QString(
            "QWidget#KailleraPaneGameList {"
            "  border-radius: 0px;"
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
            "QGroupBox#KailleraPane {"
            "  margin-top: 10px;"
            "  padding-top: 6px;"
            "}"
            "QGroupBox#KailleraPane::title {"
            "  subcontrol-origin: margin;"
            "  left: 10px;"
            "  padding: 0 4px;"
            "  font-weight: 600;"
            "}"
            "QComboBox#KailleraInputCombo {"
            "  border: 1px solid palette(mid);"
            "  border-radius: %1;"
            "  background-color: palette(base);"
            "  padding: 5px 8px;"
            "  min-height: 24px;"
            "}"
            "QComboBox#KailleraInputCombo:focus {"
            "  border-color: palette(highlight);"
            "}"
            "QComboBox#KailleraInputCombo::drop-down {"
            "  subcontrol-origin: padding;"
            "  subcontrol-position: top right;"
            "  width: %2;"
            "  border: none;"
            "  border-left: 1px solid palette(mid);"
            "  border-top-right-radius: %1;"
            "  border-bottom-right-radius: %1;"
            "  background-color: transparent;"
            "  margin: 1px;"
            "}"
            "QComboBox#KailleraInputCombo::down-arrow {"
            "  image: url(%3);"
            "  width: 10px;"
            "  height: 10px;"
            "}"
            "QComboBox#KailleraInputCombo QAbstractItemView {"
            "  border: 1px solid palette(mid);"
            "  background-color: palette(base);"
            "  selection-background-color: palette(highlight);"
            "  selection-color: palette(highlighted-text);"
            "  outline: none;"
            "  padding: 0px;"
            "  margin: 0px;"
            "}"
            "QComboBox#KailleraInputCombo QAbstractItemView::item {"
            "  padding: 0px 8px;"
            "  min-height: 18px;"
            "  margin: 0px;"
            "}"
            "QPushButton#KailleraPrimaryButton {"
            "  border: 1px solid #0066b4;"
            "  border-radius: %1;"
            "  padding: 4px 12px;"
            "  font-weight: 700;"
            "  color: white;"
            "  background-color: #0078D7;"
            "}"
            "QPushButton#KailleraPrimaryButton:hover {"
            "  background-color: #1584dd;"
            "}"
            "QPushButton#KailleraPrimaryButton:pressed {"
            "  background-color: #0063b1;"
            "}"
            "QPushButton#KailleraSecondaryButton {"
            "  border: 1px solid palette(mid);"
            "  border-radius: %1;"
            "  padding: 4px 12px;"
            "  background-color: palette(button);"
            "}"
            "QPushButton#KailleraSecondaryButton:hover {"
            "  background-color: palette(light);"
            "}"
            "QPushButton#KailleraSecondaryButton:pressed {"
            "  background-color: palette(midlight);"
            "}"
            "QPushButton#KailleraFabButton {"
            "  border: 1px solid #005a9e;"
            "  border-radius: %2;"
            "  padding: 0px;"
            "  background-color: #0078D7;"
            "  color: white;"
            "}"
            "QPushButton#KailleraFabButton:hover {"
            "  background-color: #1c88dc;"
            "}"
            "QPushButton#KailleraFabButton:pressed {"
            "  border-color: #004f8b;"
            "  background-color: #005a9e;"
            "}").arg(controlRadius, fabRadius, comboArrowIcon);
    }

    return style;
}

KailleraNetplayDialog::KailleraNetplayDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowIcon(QIcon(":Resource/Kaillera.svg"));
    m_netManager = new QNetworkAccessManager(this);
    m_serverPingPollTimer = new QTimer(this);
    m_serverPingPollTimer->setInterval(50);
    connect(m_serverPingPollTimer, &QTimer::timeout, this, &KailleraNetplayDialog::pollServerPing);

    setupUI();
    loadSettings();
    loadServerList();
    loadP2PStoredUsers();

    // Start the KSSDFA state machine timer
    // This replaces the blocking while-loop in n02::selectServerDialog()
    n02::setStateInput(0);
    m_stateMachineTimer = new QTimer(this);
    connect(m_stateMachineTimer, &QTimer::timeout, this, &KailleraNetplayDialog::onStateMachineTimer);
    m_stateMachineTimer->start(1);

    schedulePingAllServers();
    fetchLiveServerList();

    // Restore saved geometry
    std::string geom = CoreSettingsGetStringValue(SettingsID::Kaillera_NetplayGeometry);
    if (!geom.empty())
    {
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(geom)));
    }

}

KailleraNetplayDialog::~KailleraNetplayDialog()
{
    if (m_stateMachineTimer)
    {
        m_stateMachineTimer->stop();
    }
    if (m_serverPingPollTimer)
    {
        m_serverPingPollTimer->stop();
    }
    saveSettings();
    saveServerList();
    saveP2PStoredUsers();

    // Save geometry
    CoreSettingsSetValue(SettingsID::Kaillera_NetplayGeometry,
        saveGeometry().toBase64().toStdString());
    CoreSettingsSave();
}

void KailleraNetplayDialog::setupUI()
{
    setObjectName("KailleraLauncherDialog");
    setWindowTitle("RMG-K Netplay");
    setMinimumSize(520, 480);
    resize(580, 530);

    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));
    setStyleSheet(buildLauncherStyleSheet(theme));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 12, 0, 0);
    mainLayout->setSpacing(12);

    auto* profilePane = new QWidget(this);
    auto* profileWrapper = new QWidget(this);
    auto* profileWrapperLayout = new QVBoxLayout(profileWrapper);
    profileWrapperLayout->setContentsMargins(12, 0, 12, 0);
    profileWrapperLayout->setSpacing(0);
    auto* settingsLayout = new QHBoxLayout(profilePane);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(10);
    auto* usernameLabel = new QLabel("Username:", profilePane);
    usernameLabel->setObjectName("KailleraFieldLabel");
    settingsLayout->addWidget(usernameLabel);
    m_usernameEdit = new QLineEdit(profilePane);
    m_usernameEdit->setObjectName("KailleraInput");
    m_usernameEdit->setMaxLength(31);
    configureLauncherLineEditMetrics(m_usernameEdit, theme);
    settingsLayout->addWidget(m_usernameEdit);
    profileWrapperLayout->addWidget(profilePane);
    mainLayout->addWidget(profileWrapper);

    // Mode tabs
    m_tabWidget = new LauncherTabWidget(this);
    m_tabWidget->setObjectName("KailleraLauncherTabs");
    m_tabWidget->addTab(createServerTab(), "Server");
    m_tabWidget->addTab(createP2PTab(), "Peer to Peer");
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &KailleraNetplayDialog::onTabChanged);
    mainLayout->addWidget(m_tabWidget, 1);
}

QWidget* KailleraNetplayDialog::createServerTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));

    // Server list table
    auto* tablePane = new QWidget(tab);
    tablePane->setObjectName("KailleraPaneGameList");
    auto* tablePaneLayout = new QVBoxLayout(tablePane);
    tablePaneLayout->setContentsMargins(0, 0, 0, 0);
    tablePaneLayout->setSpacing(0);

    m_serverTable = new QTableWidget(0, 5, tablePane);
    m_serverTable->setObjectName("KailleraSurface");
    m_serverTable->setProperty("launcherServerTable", true);
    m_serverTable->setHorizontalHeaderLabels({"*", "Name", "Players", "Ping", "IP"});
    m_serverTable->verticalHeader()->setVisible(false);
    m_serverTable->setShowGrid(false);
    m_serverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serverTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_serverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_serverTable->setSortingEnabled(false);
    m_serverTable->horizontalHeader()->setMinimumSectionSize(16);
    // Stretch the IP column so IPs aren't truncated.
    m_serverTable->horizontalHeader()->setStretchLastSection(false);
    m_serverTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_serverTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_serverTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_serverTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_serverTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    QFont serverHeaderFont = m_serverTable->horizontalHeader()->font();
    serverHeaderFont.setBold(false);
    m_serverTable->horizontalHeader()->setFont(serverHeaderFont);
    m_serverTable->setColumnWidth(0, 28);
    m_serverTable->setColumnWidth(1, 170);
    m_serverTable->setColumnWidth(2, 58);
    m_serverTable->setColumnWidth(3, 60);
    m_serverTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_serverTable->setItemDelegateForColumn(0, new CenteredIconDelegate(m_serverTable));
    if (theme != "Modern" && !isFusionFamilyTheme(theme))
    {
        m_serverTable->setStyleSheet(
            "QTableWidget { border-top: 1px solid palette(mid); border-left: 1px solid palette(mid); }");
    }
    connect(m_serverTable, &QTableWidget::cellDoubleClicked, this, &KailleraNetplayDialog::onServerDoubleClicked);
    connect(m_serverTable, &QWidget::customContextMenuRequested, this, &KailleraNetplayDialog::onServerRightClicked);
    connect(m_serverTable, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != 0 || row < 0 || row >= m_displayServers.size())
        {
            updateServerButtons();
            return;
        }

        const ServerEntry& entry = m_displayServers[row];
        toggleFavoriteServer(entry.host, entry.name);
    });
    connect(m_serverTable, &QTableWidget::itemSelectionChanged, this, &KailleraNetplayDialog::updateServerButtons);
    tablePaneLayout->addWidget(m_serverTable);

    m_btnAdd = new QPushButton(tablePane);
    m_btnAdd->setObjectName("KailleraFabButton");
    m_btnAdd->setToolTip("Add a custom server");
    const bool useLauncherSkin =
        (theme == "Modern" || theme == "Fusion" || theme == "Fusion Warm" || theme == "Fusion Dark");
    if (useLauncherSkin)
    {
        m_btnAdd->setText("");
        m_btnAdd->setIcon(QIcon(":/icons/white/svg/add-line.svg"));
    }
    else
    {
        QFont addFont = m_btnAdd->font();
        addFont.setPointSize(addFont.pointSize() + 6);
        addFont.setBold(true);
        m_btnAdd->setFont(addFont);
        m_btnAdd->setText("+");
        m_btnAdd->setIcon(QIcon());
    }
    m_btnAdd->setIconSize(QSize(22, 22));
    m_btnAdd->setCursor(Qt::PointingHandCursor);
    m_btnAdd->setFixedSize(46, 46);
    configureLauncherAccentPalette(m_btnAdd);
    connect(m_btnAdd, &QPushButton::clicked, this, &KailleraNetplayDialog::onAddServer);
    attachFloatingCornerButton(tablePane, m_btnAdd, 20, 14);

    layout->addWidget(tablePane, 1);


    // Bottom action row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(10);
    m_btnWaitingGames = new QPushButton("Waiting Games", tab);
    m_btnWaitingGames->setObjectName("KailleraSecondaryButton");
    configureLauncherButtonMetrics(m_btnWaitingGames);
    m_btnConnect = new QPushButton("Connect", tab);
    m_btnConnect->setObjectName("KailleraPrimaryButton");
    configureLauncherButtonMetrics(m_btnConnect);
    configureLauncherAccentPalette(m_btnConnect);
    auto* frameDelayLabel = new QLabel("Frame Delay:", tab);
    frameDelayLabel->setObjectName("KailleraFieldLabel");
    m_frameDelayCombo = new QComboBox(tab);
    m_frameDelayCombo->setObjectName("KailleraInputCombo");
    configureLauncherComboMetrics(m_frameDelayCombo);
    m_frameDelayCombo->addItem("Auto");
    m_frameDelayCombo->addItem("1 frame (8ms)");
    m_frameDelayCombo->addItem("2 frames (24ms)");
    m_frameDelayCombo->addItem("3 frames (40ms)");
    m_frameDelayCombo->addItem("4 frames (56ms)");
    m_frameDelayCombo->addItem("5 frames (72ms)");
    m_frameDelayCombo->addItem("6 frames (88ms)");
    m_frameDelayCombo->addItem("7 frames (104ms)");
    m_frameDelayCombo->addItem("8 frames (120ms)");
    m_frameDelayCombo->addItem("9 frames (136ms)");

    connect(m_btnWaitingGames, &QPushButton::clicked, this, &KailleraNetplayDialog::onWaitingGames);
    connect(m_btnConnect, &QPushButton::clicked, this, &KailleraNetplayDialog::onConnectServer);

    btnLayout->addWidget(m_btnWaitingGames);
    btnLayout->addStretch();
    btnLayout->addWidget(frameDelayLabel);
    btnLayout->addWidget(m_frameDelayCombo);
    btnLayout->addWidget(m_btnConnect);
    layout->addLayout(btnLayout);

    updateServerButtons();


    return tab;
}

QWidget* KailleraNetplayDialog::createP2PTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    const QString theme = QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Theme));

    // Host area
    QVBoxLayout* hostLayout = nullptr;
    auto* hostPane = createLauncherSectionPane(theme, tab, "Host", &hostLayout);

    auto* hostBody = new QWidget(hostPane);
    auto* hostBodyLayout = new QVBoxLayout(hostBody);
    hostBodyLayout->setContentsMargins(0, 0, 0, 0);
    hostBodyLayout->setSpacing(10);

    // Game picker
    auto* gameLayout = new QHBoxLayout();
    auto* gameLabel = new QLabel("ROM:", hostBody);
    gameLabel->setObjectName("KailleraFieldLabel");
    gameLayout->addWidget(gameLabel);
    m_p2pGameCombo = new SearchableComboBox(hostBody);
    m_p2pGameCombo->setObjectName("KailleraInputCombo");
    configureLauncherComboMetrics(m_p2pGameCombo);
    if (theme != "Modern")
    {
        static_cast<SearchableComboBox*>(m_p2pGameCombo)->setDisplayTextInset(4);
    }
    m_p2pGameCombo->setToolTip("Choose the ROM to host");
    gameLayout->addWidget(m_p2pGameCombo, 1);
    hostBodyLayout->addLayout(gameLayout);

    QStringList gameNames;
    if (infos.gameList)
    {
        const char* p = infos.gameList;
        while (*p)
        {
            gameNames.append(QString::fromUtf8(p));
            p += strlen(p) + 1;
        }
    }
    std::sort(gameNames.begin(), gameNames.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    m_p2pGameCombo->addItems(gameNames);

    if (m_p2pGameCombo->count() > 0)
    {
        m_p2pGameCombo->setCurrentIndex(0);
    }

    // Host port + Host button
    auto* hostBtnLayout = new QHBoxLayout();
    auto* hostPortLabel = new QLabel("Host port:", hostBody);
    hostPortLabel->setObjectName("KailleraFieldLabel");
    hostBtnLayout->addWidget(hostPortLabel);
    m_p2pPortEdit = new QLineEdit(hostBody);
    m_p2pPortEdit->setObjectName("KailleraInput");
    m_p2pPortEdit->setText("27886");
    configureLauncherLineEditMetrics(m_p2pPortEdit, theme);
    m_p2pPortEdit->setMaximumWidth(80);
    hostBtnLayout->addWidget(m_p2pPortEdit);
    hostBtnLayout->addStretch();
    m_btnP2PHost = new QPushButton("Host", hostBody);
    m_btnP2PHost->setObjectName("KailleraPrimaryButton");
    configureLauncherButtonMetrics(m_btnP2PHost);
    configureLauncherAccentPalette(m_btnP2PHost);
    connect(m_btnP2PHost, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PHost);
    hostBtnLayout->addWidget(m_btnP2PHost);
    hostBodyLayout->addLayout(hostBtnLayout);
    hostLayout->addWidget(hostBody);

    layout->addWidget(hostPane);

    if (theme != "Modern")
    {
        auto* dividerRow = new QWidget(tab);
        auto* dividerRowLayout = new QVBoxLayout(dividerRow);
        dividerRowLayout->setContentsMargins(0, 6, 0, 6);
        dividerRowLayout->setSpacing(0);

        auto* divider = new QFrame(dividerRow);
        divider->setObjectName("KailleraDivider");
        divider->setFrameShape(QFrame::HLine);
        divider->setFrameShadow(QFrame::Plain);
        dividerRowLayout->addWidget(divider);
        layout->addWidget(dividerRow);
    }

    // Connect area
    QVBoxLayout* connectLayout = nullptr;
    auto* connectPane = createLauncherSectionPane(theme, tab, "Connect", &connectLayout);

    auto* connectBody = new QWidget(connectPane);
    auto* connectBodyLayout = new QVBoxLayout(connectBody);
    connectBodyLayout->setContentsMargins(0, 0, 0, 0);
    connectBodyLayout->setSpacing(10);

    // Top row: IP/Code field + Connect + Paste & Go
    auto* addrLayout = new QHBoxLayout();
    auto* addrLabel = new QLabel("IP/Code:", connectBody);
    addrLabel->setObjectName("KailleraFieldLabel");
    addrLayout->addWidget(addrLabel);
    const int p2pLabelWidth = qMax(gameLabel->sizeHint().width(),
        qMax(hostPortLabel->sizeHint().width(), addrLabel->sizeHint().width()));
    gameLabel->setFixedWidth(p2pLabelWidth);
    hostPortLabel->setFixedWidth(p2pLabelWidth);
    addrLabel->setFixedWidth(p2pLabelWidth);
    m_p2pHostEdit = new QLineEdit(connectBody);
    m_p2pHostEdit->setObjectName("KailleraInput");
    m_p2pHostEdit->setPlaceholderText("Connect code or ip:port");
    configureLauncherLineEditMetrics(m_p2pHostEdit, theme);
    addrLayout->addWidget(m_p2pHostEdit, 1);
    m_btnP2PJoin = new QPushButton("Connect", connectBody);
    m_btnP2PJoin->setObjectName("KailleraPrimaryButton");
    configureLauncherButtonMetrics(m_btnP2PJoin);
    configureLauncherAccentPalette(m_btnP2PJoin);
    connect(m_btnP2PJoin, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PJoin);
    addrLayout->addWidget(m_btnP2PJoin);
    m_btnP2PPasteGo = new QPushButton("Paste && Go", connectBody);
    m_btnP2PPasteGo->setObjectName("KailleraSecondaryButton");
    configureLauncherButtonMetrics(m_btnP2PPasteGo);
    connect(m_btnP2PPasteGo, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PPasteAndGo);
    addrLayout->addWidget(m_btnP2PPasteGo);
    connectBodyLayout->addLayout(addrLayout);

    // Stored list + waiting games button
    auto* storedAreaLayout = new QVBoxLayout();
    storedAreaLayout->setSpacing(8);

    m_p2pStoredTable = new QTableWidget(0, 3, connectBody);
    m_p2pStoredTable->setObjectName("KailleraSurface");
    m_p2pStoredTable->setProperty("launcherP2PTable", true);
    m_p2pStoredTable->setHorizontalHeaderLabels({"*", "Name", "IP / Code"});
    m_p2pStoredTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_p2pStoredTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_p2pStoredTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_p2pStoredTable->setColumnWidth(0, 28);
    m_p2pStoredTable->setColumnWidth(1, 140);
    m_p2pStoredTable->setShowGrid(false);
    m_p2pStoredTable->verticalHeader()->setVisible(false);
    m_p2pStoredTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_p2pStoredTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_p2pStoredTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_p2pStoredTable->setItemDelegateForColumn(0, new CenteredIconDelegate(m_p2pStoredTable));
    if (theme != "Modern" && !isFusionFamilyTheme(theme))
    {
        m_p2pStoredTable->setStyleSheet(
            "QTableWidget { border: 1px solid palette(mid); }");
    }
    connect(m_p2pStoredTable, &QTableWidget::cellClicked, this, &KailleraNetplayDialog::onP2PStoredClicked);
    storedAreaLayout->addWidget(m_p2pStoredTable, 1);

    m_btnP2PWaitingGames = new QPushButton("Show waiting games", connectBody);
    m_btnP2PWaitingGames->setObjectName("KailleraSecondaryButton");
    configureLauncherButtonMetrics(m_btnP2PWaitingGames);
    connect(m_btnP2PWaitingGames, &QPushButton::clicked, this, &KailleraNetplayDialog::onP2PWaitingGames);
    storedAreaLayout->addWidget(m_btnP2PWaitingGames, 0, Qt::AlignLeft);

    connectBodyLayout->addLayout(storedAreaLayout, 1);
    connectLayout->addWidget(connectBody);
    layout->addWidget(connectPane, 1);

    return tab;
}

void KailleraNetplayDialog::loadSettings()
{
    // Load username
    std::string username = CoreSettingsGetStringValue(SettingsID::Kaillera_Username);
    if (username.empty())
    {
        // Fallback to Windows username
        char winUser[32];
        DWORD size = sizeof(winUser);
        if (GetUserNameA(winUser, &size))
        {
            username = winUser;
        }
        else
        {
            username = "Player";
        }
    }
    m_usernameEdit->setText(QString::fromStdString(username));

    // Load frame delay
    int frameDelay = CoreSettingsGetIntValue(SettingsID::Kaillera_SpoofPing);
    if (frameDelay < 0 || frameDelay > 9) frameDelay = 0;
    m_frameDelayCombo->setCurrentIndex(frameDelay);

    // Load active mode and select the corresponding tab
    int mode = CoreSettingsGetIntValue(SettingsID::Kaillera_ActiveMode);
    if (mode < 0 || mode > 1) mode = 1;
    // Tab order: 0=Server, 1=P2P
    // Mode order: 0=P2P, 1=Server
    int tabIndex = (mode == 0) ? 1 : 0;
    m_tabWidget->setCurrentIndex(tabIndex);
    // Always explicitly activate the mode. setCurrentIndex does not emit
    // currentChanged when the index is unchanged (e.g., default 0 → 0),
    // so onTabChanged never fires and the n02 module stays on whatever
    // CoreInitKaillera loaded (which could be playback mode 2).
    n02::activateMode(mode);
}

void KailleraNetplayDialog::saveSettings()
{
    CoreSettingsSetValue(SettingsID::Kaillera_Username,
                         m_usernameEdit->text().toStdString());
    CoreSettingsSetValue(SettingsID::Kaillera_SpoofPing,
                         m_frameDelayCombo->currentIndex());

    // Tab order: 0=Server, 1=P2P
    // Mode order: 0=P2P, 1=Server
    int mode = (m_tabWidget->currentIndex() == 1) ? 0 : 1;
    CoreSettingsSetValue(SettingsID::Kaillera_ActiveMode, mode);
}

void KailleraNetplayDialog::loadServerList()
{
    m_favoriteServers.clear();
    m_cachedLiveServers.clear();
    m_displayServers.clear();

    const std::vector<std::string> favoriteNames =
        CoreSettingsGetStringListValue(SettingsID::Kaillera_ServerListNames);
    const std::vector<std::string> favoriteHosts =
        CoreSettingsGetStringListValue(SettingsID::Kaillera_ServerListHosts);
    for (size_t i = 0; i < favoriteNames.size() && i < favoriteHosts.size(); ++i)
    {
        m_favoriteServers.append({
            QString::fromStdString(favoriteNames[i]),
            QString::fromStdString(favoriteHosts[i]),
            "-",
            -1,
            "-",
            999999
        });
    }

    const std::vector<std::string> cachedNames =
        CoreSettingsGetStringListValue(SettingsID::Kaillera_LiveServerCacheNames);
    const std::vector<std::string> cachedHosts =
        CoreSettingsGetStringListValue(SettingsID::Kaillera_LiveServerCacheHosts);
    for (size_t i = 0; i < cachedNames.size() && i < cachedHosts.size(); ++i)
    {
        const QString host = QString::fromStdString(cachedHosts[i]);
        if (host.isEmpty() || favoriteServerIndexByHost(host) >= 0)
        {
            continue;
        }

        m_cachedLiveServers.append({
            QString::fromStdString(cachedNames[i]),
            host,
            "-",
            -1,
            "-",
            999999
        });
    }

    refreshServerListDisplay();
}

void KailleraNetplayDialog::saveServerList()
{
    std::vector<std::string> favoriteNames;
    std::vector<std::string> favoriteHosts;
    favoriteNames.reserve(m_favoriteServers.size());
    favoriteHosts.reserve(m_favoriteServers.size());
    for (const auto& server : m_favoriteServers)
    {
        favoriteNames.push_back(server.name.toStdString());
        favoriteHosts.push_back(server.host.toStdString());
    }

    std::vector<std::string> cachedNames;
    std::vector<std::string> cachedHosts;
    cachedNames.reserve(m_cachedLiveServers.size());
    cachedHosts.reserve(m_cachedLiveServers.size());
    for (const auto& server : m_cachedLiveServers)
    {
        cachedNames.push_back(server.name.toStdString());
        cachedHosts.push_back(server.host.toStdString());
    }

    CoreSettingsSetValue(SettingsID::Kaillera_ServerListNames, favoriteNames);
    CoreSettingsSetValue(SettingsID::Kaillera_ServerListHosts, favoriteHosts);
    CoreSettingsSetValue(SettingsID::Kaillera_LiveServerCacheNames, cachedNames);
    CoreSettingsSetValue(SettingsID::Kaillera_LiveServerCacheHosts, cachedHosts);
    CoreSettingsSave();
}

// QTableWidgetItem subclass that sorts numerically by Qt::UserRole data
class NumericSortItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
    }
};

void KailleraNetplayDialog::refreshServerListDisplay()
{
    QString selectedHost;
    const int currentRow = m_serverTable->currentRow();
    if (currentRow >= 0 && currentRow < m_displayServers.size())
    {
        selectedHost = m_displayServers[currentRow].host;
    }

    const int verticalScroll = m_serverTable->verticalScrollBar() != nullptr
        ? m_serverTable->verticalScrollBar()->value()
        : 0;
    const int horizontalScroll = m_serverTable->horizontalScrollBar() != nullptr
        ? m_serverTable->horizontalScrollBar()->value()
        : 0;

    m_displayServers = m_favoriteServers;

    QVector<ServerEntry> nonFavorites;
    nonFavorites.reserve(m_cachedLiveServers.size());
    for (const auto& server : m_cachedLiveServers)
    {
        if (favoriteServerIndexByHost(server.host) < 0)
        {
            nonFavorites.append(server);
        }
    }

    std::stable_sort(nonFavorites.begin(), nonFavorites.end(), [](const ServerEntry& a, const ServerEntry& b) {
        return a.pingValue < b.pingValue;
    });

    for (const auto& server : nonFavorites)
    {
        m_displayServers.append(server);
    }

    QSignalBlocker blocker(m_serverTable);
    m_serverTable->setUpdatesEnabled(false);
    m_serverTable->setRowCount(m_displayServers.size());
    for (int i = 0; i < m_displayServers.size(); ++i)
    {
        const ServerEntry& server = m_displayServers[i];
        const bool favorite = (favoriteServerIndexByHost(server.host) >= 0);

        auto* favoriteItem = new QTableWidgetItem();
        favoriteItem->setData(Qt::UserRole, themedFavoriteIcon(favorite ? "star-fill" : "star"));
        favoriteItem->setTextAlignment(Qt::AlignCenter);
        favoriteItem->setFlags((favoriteItem->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
            & ~Qt::ItemIsEditable);
        favoriteItem->setToolTip(favorite ? "Favorited server" : "Favorite server");
        m_serverTable->setItem(i, 0, favoriteItem);

        auto* nameItem = new QTableWidgetItem(server.name);
        m_serverTable->setItem(i, 1, nameItem);

        auto* playersItem = new QTableWidgetItem(server.players);
        playersItem->setTextAlignment(Qt::AlignCenter);
        m_serverTable->setItem(i, 2, playersItem);

        auto* pingItem = new QTableWidgetItem(server.ping);
        pingItem->setTextAlignment(Qt::AlignCenter);
        m_serverTable->setItem(i, 3, pingItem);

        m_serverTable->setItem(i, 4, new QTableWidgetItem(server.host));
    }

    bool restoredSelection = false;
    if (!selectedHost.isEmpty())
    {
        for (int i = 0; i < m_displayServers.size(); ++i)
        {
            if (m_displayServers[i].host == selectedHost)
            {
                m_serverTable->selectRow(i);
                restoredSelection = true;
                break;
            }
        }
    }
    if (!restoredSelection)
    {
        m_serverTable->clearSelection();
    }

    m_serverTable->setUpdatesEnabled(true);
    if (m_serverTable->verticalScrollBar() != nullptr)
    {
        m_serverTable->verticalScrollBar()->setValue(verticalScroll);
    }
    if (m_serverTable->horizontalScrollBar() != nullptr)
    {
        m_serverTable->horizontalScrollBar()->setValue(horizontalScroll);
    }

    updateServerButtons();
}

void KailleraNetplayDialog::fetchLiveServerList()
{
    QNetworkRequest request(QUrl("http://kaillerareborn.2manygames.fr/server_list.php"));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            return;
        }

        const QVector<ServerEntry> liveServers = parseLiveServerList(reply->readAll());
        if (liveServers.isEmpty())
        {
            return;
        }

        QVector<ServerEntry> mergedLiveServers;
        mergedLiveServers.reserve(liveServers.size());

        for (const auto& cachedServer : m_cachedLiveServers)
        {
            for (const auto& liveServer : liveServers)
            {
                if (liveServer.host == cachedServer.host)
                {
                    ServerEntry merged = liveServer;
                    merged.ping = cachedServer.ping;
                    merged.pingValue = cachedServer.pingValue;
                    mergedLiveServers.append(merged);
                    break;
                }
            }
        }

        for (const auto& liveServer : liveServers)
        {
            if (cachedServerIndexByHost(liveServer.host) < 0)
            {
                mergedLiveServers.append(liveServer);
            }
        }

        m_cachedLiveServers = mergedLiveServers;
        for (auto& favoriteServer : m_favoriteServers)
        {
            for (const auto& liveServer : liveServers)
            {
                if (liveServer.host == favoriteServer.host)
                {
                    favoriteServer.players = liveServer.players;
                    favoriteServer.playerCount = liveServer.playerCount;
                    break;
                }
            }
        }
        refreshServerListDisplay();
        saveServerList();
        schedulePingAllServers();
    });
}

void KailleraNetplayDialog::schedulePingAllServers()
{
    if (m_pingAllInProgress)
    {
        m_pingAllQueued = true;
        return;
    }

    if (m_pingAllQueued)
    {
        return;
    }

    m_pingAllQueued = true;
    QTimer::singleShot(100, this, [this]() {
        if (!m_pingAllQueued || m_pingAllInProgress)
        {
            return;
        }

        m_pingAllQueued = false;
        pingAllServers();
    });
}

void KailleraNetplayDialog::pingAllServers()
{
    m_pingAllInProgress = true;
    m_serverListNeedsRefresh = false;
    m_pendingPingHosts.clear();
    m_pendingPingHosts.reserve(m_displayServers.size());
    for (const auto& server : m_displayServers)
    {
        m_pendingPingHosts.append(server.host);
    }

    startNextServerPing();
}

void KailleraNetplayDialog::startNextServerPing()
{
    if (m_pendingPingHosts.isEmpty())
    {
        m_pingAllInProgress = false;
        if (m_serverListNeedsRefresh)
        {
            m_serverListNeedsRefresh = false;
            refreshServerListDisplay();
            cacheVisibleLiveServerOrder();
            saveServerList();
        }
        if (m_pingAllQueued)
        {
            m_pingAllQueued = false;
            schedulePingAllServers();
        }
        return;
    }

    m_activePingHost = m_pendingPingHosts.takeFirst();
    updateServerPing(m_activePingHost, -2);

    QByteArray ipBytes;
    int port = 27888;
    const int colonIdx = m_activePingHost.lastIndexOf(':');
    if (colonIdx >= 0)
    {
        ipBytes = m_activePingHost.left(colonIdx).toUtf8();
        port = m_activePingHost.mid(colonIdx + 1).toInt();
        if (port == 0)
        {
            port = 27888;
        }
    }
    else
    {
        ipBytes = m_activePingHost.toUtf8();
    }

    m_activePingFuture = std::async(std::launch::async, [ipBytes, port]() {
        return kaillera_ping_server(const_cast<char*>(ipBytes.constData()), port, 2000);
    });

    if (m_serverPingPollTimer != nullptr)
    {
        m_serverPingPollTimer->start();
    }
}

void KailleraNetplayDialog::pollServerPing()
{
    if (!m_activePingFuture.valid())
    {
        if (m_serverPingPollTimer != nullptr)
        {
            m_serverPingPollTimer->stop();
        }
        return;
    }

    if (m_activePingFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    {
        return;
    }

    if (m_serverPingPollTimer != nullptr)
    {
        m_serverPingPollTimer->stop();
    }

    updateServerPing(m_activePingHost, m_activePingFuture.get());
    m_activePingHost.clear();
    startNextServerPing();
}

QVector<ServerEntry> KailleraNetplayDialog::parseLiveServerList(const QByteArray& data) const
{
    QVector<ServerEntry> parsedServers;
    if (data.size() < 32)
    {
        return parsedServers;
    }

    const char* ptr = data.constData();
    const char* end = ptr + data.size();

    while (ptr < end - 10)
    {
        const char* nameStart = ptr;
        while (ptr < end && *ptr != '\n') ++ptr;
        if (ptr >= end)
        {
            break;
        }
        const QString name = QString::fromUtf8(nameStart, ptr - nameStart).trimmed();
        ++ptr;

        const char* lineStart = ptr;
        while (ptr < end && *ptr != '\n') ++ptr;
        const QString line = QString::fromUtf8(lineStart, ptr - lineStart).trimmed();
        if (ptr < end)
        {
            ++ptr;
        }

        if (name.isEmpty() || line.isEmpty())
        {
            continue;
        }

        const QStringList parts = line.split(';');
        if (parts.isEmpty())
        {
            continue;
        }

        const QString hostPort = parts[0].trimmed();
        if (hostPort.isEmpty() || isPrivateHostPort(hostPort))
        {
            continue;
        }

        bool duplicate = false;
        for (const auto& server : parsedServers)
        {
            if (server.host == hostPort)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        QString players = "-";
        int playerCount = -1;
        if (parts.size() > 1)
        {
            players = parts[1].trimmed();
            if (players.isEmpty())
            {
                players = "-";
            }
            else
            {
                bool ok = false;
                const int parsedCount = players.toInt(&ok);
                if (ok)
                {
                    playerCount = parsedCount;
                }
            }
        }

        parsedServers.append({name, hostPort, players, playerCount, "-", 999999});
    }

    return parsedServers;
}

int KailleraNetplayDialog::favoriteServerIndexByHost(const QString& host) const
{
    for (int i = 0; i < m_favoriteServers.size(); ++i)
    {
        if (m_favoriteServers[i].host == host)
        {
            return i;
        }
    }
    return -1;
}

int KailleraNetplayDialog::cachedServerIndexByHost(const QString& host) const
{
    for (int i = 0; i < m_cachedLiveServers.size(); ++i)
    {
        if (m_cachedLiveServers[i].host == host)
        {
            return i;
        }
    }
    return -1;
}

void KailleraNetplayDialog::toggleFavoriteServer(const QString& host, const QString& name)
{
    const int favoriteIndex = favoriteServerIndexByHost(host);
    if (favoriteIndex >= 0)
    {
        m_favoriteServers.removeAt(favoriteIndex);
    }
    else
    {
        ServerEntry entry{name, host, "-", -1, "-", 999999};
        const int cachedIndex = cachedServerIndexByHost(host);
        if (cachedIndex >= 0)
        {
            entry = m_cachedLiveServers[cachedIndex];
        }
        m_favoriteServers.append(entry);
    }

    refreshServerListDisplay();
    saveServerList();
}

void KailleraNetplayDialog::moveFavoriteServer(int favoriteIndex, int delta)
{
    const int targetIndex = favoriteIndex + delta;
    if (favoriteIndex < 0 || favoriteIndex >= m_favoriteServers.size()
        || targetIndex < 0 || targetIndex >= m_favoriteServers.size())
    {
        return;
    }

    m_favoriteServers.move(favoriteIndex, targetIndex);
    refreshServerListDisplay();
    if (targetIndex >= 0 && targetIndex < m_displayServers.size())
    {
        m_serverTable->selectRow(targetIndex);
    }
    saveServerList();
}

void KailleraNetplayDialog::updateServerPing(const QString& host, int pingMs)
{
    QString pingText;
    auto applyPing = [pingMs](ServerEntry& server) {
        if (pingMs == -2)
        {
            server.ping = "...";
            server.pingValue = 999998;
        }
        else if (pingMs >= 0)
        {
            server.ping = QString::number(pingMs) + "ms";
            server.pingValue = pingMs;
        }
        else
        {
            server.ping = "timeout";
            server.pingValue = 999999;
        }
    };

    if (pingMs == -2)
    {
        pingText = "...";
    }
    else if (pingMs >= 0)
    {
        pingText = QString::number(pingMs) + "ms";
    }
    else
    {
        pingText = "timeout";
    }

    const int favoriteIndex = favoriteServerIndexByHost(host);
    if (favoriteIndex >= 0)
    {
        applyPing(m_favoriteServers[favoriteIndex]);
    }

    const int cachedIndex = cachedServerIndexByHost(host);
    if (cachedIndex >= 0)
    {
        applyPing(m_cachedLiveServers[cachedIndex]);
    }

    for (auto& server : m_displayServers)
    {
        if (server.host == host)
        {
            applyPing(server);
            break;
        }
    }

    if (m_pingAllInProgress)
    {
        updateVisibleServerPing(host, pingText);
        if (pingMs != -2)
        {
            m_serverListNeedsRefresh = true;
        }
        return;
    }

    refreshServerListDisplay();
}

void KailleraNetplayDialog::updateVisibleServerPing(const QString& host, const QString& pingText)
{
    for (int row = 0; row < m_displayServers.size(); ++row)
    {
        if (m_displayServers[row].host != host)
        {
            continue;
        }

        QTableWidgetItem* pingItem = m_serverTable->item(row, 3);
        if (pingItem != nullptr)
        {
            pingItem->setText(pingText);
        }
        break;
    }
}

void KailleraNetplayDialog::cacheVisibleLiveServerOrder()
{
    QVector<ServerEntry> sortedNonFavorites;
    sortedNonFavorites.reserve(m_displayServers.size());
    for (const auto& server : m_displayServers)
    {
        if (favoriteServerIndexByHost(server.host) < 0)
        {
            sortedNonFavorites.append(server);
        }
    }

    int nextNonFavorite = 0;
    for (int i = 0; i < m_cachedLiveServers.size() && nextNonFavorite < sortedNonFavorites.size(); ++i)
    {
        if (favoriteServerIndexByHost(m_cachedLiveServers[i].host) >= 0)
        {
            continue;
        }

        m_cachedLiveServers[i] = sortedNonFavorites[nextNonFavorite];
        ++nextNonFavorite;
    }
}

void KailleraNetplayDialog::updateServerButtons()
{
    const int row = m_serverTable ? m_serverTable->currentRow() : -1;
    const bool hasSelection = (row >= 0 && row < m_displayServers.size());
    if (m_btnConnect != nullptr)
    {
        m_btnConnect->setEnabled(hasSelection);
    }
}

void KailleraNetplayDialog::onStateMachineTimer()
{
    // Drive the KSSDFA state machine one step (non-blocking)
    bool active = n02::processStateMachineStep();
    if (!active)
    {
        // State 3 = shutdown, close dialog
        m_stateMachineTimer->stop();
        accept();
    }
}

void KailleraNetplayDialog::onTabChanged(int index)
{
    // Tab order: 0=Server, 1=P2P
    // n02 mode: 0=P2P, 1=Server
    int mode = (index == 1) ? 0 : 1;
    n02::activateMode(mode);
}

void KailleraNetplayDialog::onAddServer()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Add Server");
    auto* layout = new QVBoxLayout(&dlg);
    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText("Server Name");
    auto* hostEdit = new QLineEdit(&dlg);
    hostEdit->setPlaceholderText("Host (ip:port)");
    hostEdit->setText("127.0.0.1:27888");
    auto* btnLayout = new QHBoxLayout();
    auto* btnOk = new QPushButton("OK", &dlg);
    auto* btnCancel = new QPushButton("Cancel", &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    layout->addWidget(nameEdit);
    layout->addWidget(hostEdit);
    layout->addLayout(btnLayout);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    nameEdit->setFocus();

    if (dlg.exec() != QDialog::Accepted) return;
    QString name = nameEdit->text().trimmed();
    QString host = hostEdit->text().trimmed();
    if (name.isEmpty() || host.isEmpty()) return;

    const int existingFavorite = favoriteServerIndexByHost(host);
    if (existingFavorite >= 0)
    {
        m_favoriteServers[existingFavorite].name = name;
    }
    else
    {
        ServerEntry entry{name, host, "-", -1, "-", 999999};
        const int cachedIndex = cachedServerIndexByHost(host);
        if (cachedIndex >= 0)
        {
            entry.ping = m_cachedLiveServers[cachedIndex].ping;
            entry.pingValue = m_cachedLiveServers[cachedIndex].pingValue;
        }
        m_favoriteServers.append(entry);
    }
    refreshServerListDisplay();
    saveServerList();
    schedulePingAllServers();
}

void KailleraNetplayDialog::onEditServer()
{
    int row = m_serverTable->currentRow();
    if (row < 0 || row >= m_displayServers.size()) return;

    const QString selectedHost = m_displayServers[row].host;
    const int favoriteIndex = favoriteServerIndexByHost(selectedHost);
    if (favoriteIndex < 0 || favoriteIndex >= m_favoriteServers.size()) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Edit Server");
    auto* layout = new QVBoxLayout(&dlg);
    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText("Server Name");
    nameEdit->setText(m_favoriteServers[favoriteIndex].name);
    auto* hostEdit = new QLineEdit(&dlg);
    hostEdit->setPlaceholderText("Host (ip:port)");
    hostEdit->setText(m_favoriteServers[favoriteIndex].host);
    auto* btnLayout = new QHBoxLayout();
    auto* btnOk = new QPushButton("OK", &dlg);
    auto* btnCancel = new QPushButton("Cancel", &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    layout->addWidget(nameEdit);
    layout->addWidget(hostEdit);
    layout->addLayout(btnLayout);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    nameEdit->setFocus();

    if (dlg.exec() != QDialog::Accepted) return;
    QString name = nameEdit->text().trimmed();
    QString host = hostEdit->text().trimmed();
    if (name.isEmpty() || host.isEmpty()) return;
    const int duplicateFavorite = favoriteServerIndexByHost(host);
    if (duplicateFavorite >= 0 && duplicateFavorite != favoriteIndex)
    {
        QMessageBox::information(this, "Edit Server",
            "That host is already in your favorites.");
        return;
    }

    m_favoriteServers[favoriteIndex].name = name;
    m_favoriteServers[favoriteIndex].host = host;
    m_favoriteServers[favoriteIndex].ping = "-";
    m_favoriteServers[favoriteIndex].pingValue = 999999;
    refreshServerListDisplay();
    saveServerList();
    schedulePingAllServers();
}

void KailleraNetplayDialog::onServerRightClicked(QPoint pos)
{
    int row = m_serverTable->rowAt(pos.y());
    if (row < 0 || row >= m_displayServers.size()) return;

    m_serverTable->selectRow(row);
    const ServerEntry& entry = m_displayServers[row];
    const int favoriteIndex = favoriteServerIndexByHost(entry.host);
    const bool favorite = favoriteIndex >= 0;

    QMenu menu(this);
    QAction* actFavorite = menu.addAction(favorite ? "Unfavorite" : "Favorite");
    QAction* actEdit = nullptr;
    QAction* actMoveUp = nullptr;
    QAction* actMoveDown = nullptr;
    if (favorite)
    {
        actEdit = menu.addAction("Edit");
        actMoveUp = menu.addAction("Move Favorite Up");
        actMoveDown = menu.addAction("Move Favorite Down");
        actMoveUp->setEnabled(favoriteIndex > 0);
        actMoveDown->setEnabled(favoriteIndex + 1 < m_favoriteServers.size());
        menu.addSeparator();
    }
    QAction* actPing = menu.addAction("Ping");
    QAction* actTraceroute = menu.addAction("Traceroute");

    QAction* chosen = menu.exec(m_serverTable->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actFavorite)
    {
        toggleFavoriteServer(entry.host, entry.name);
    }
    else if (chosen == actEdit)
    {
        onEditServer();
    }
    else if (chosen == actMoveUp)
    {
        moveFavoriteServer(favoriteIndex, -1);
    }
    else if (chosen == actMoveDown)
    {
        moveFavoriteServer(favoriteIndex, 1);
    }
    else if (chosen == actPing)
    {
        const QString hostStr = entry.host;
        QByteArray ipBytes;
        int port = 27888;
        const int colonIdx = hostStr.lastIndexOf(':');
        if (colonIdx >= 0)
        {
            ipBytes = hostStr.left(colonIdx).toUtf8();
            port = hostStr.mid(colonIdx + 1).toInt();
            if (port == 0)
            {
                port = 27888;
            }
        }
        else
        {
            ipBytes = hostStr.toUtf8();
        }

        updateServerPing(hostStr, -2);
        QApplication::processEvents();
        updateServerPing(hostStr, kaillera_ping_server(ipBytes.data(), port, 2000));
    }
    else if (chosen == actTraceroute)
    {
        // Extract IP (strip port)
        const QString ip = entry.host.split(':').first();

        // Launch tracert in a new console window
        QString cmd = "cmd.exe /c \"tracert " + ip + " & pause\"";
        QByteArray cmdBytes = cmd.toLocal8Bit();
        WinExec(cmdBytes.constData(), SW_SHOW);
    }
}


void KailleraNetplayDialog::onWaitingGames()
{
    m_btnWaitingGames->setEnabled(false);

    QNetworkRequest request(QUrl("http://kaillerareborn.2manygames.fr/game_list.php"));
    QNetworkReply* reply = m_netManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onWaitingGamesReply(reply);
    });
}

void KailleraNetplayDialog::onWaitingGamesReply(QNetworkReply* reply)
{
    m_btnWaitingGames->setEnabled(true);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::warning(this, "Waiting Games", "Error: " + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    if (data.size() < 50)
    {
        QMessageBox::information(this, "Waiting Games", "No waiting games found.");
        return;
    }

    // Build popup dialog
    QDialog* wgDialog = new QDialog(this);
    wgDialog->setWindowTitle("Waiting Games");
    wgDialog->setMinimumSize(600, 400);
    wgDialog->resize(700, 450);

    auto* wgLayout = new QVBoxLayout(wgDialog);

    auto* wgTable = new QTableWidget(0, 5, wgDialog);
    wgTable->setHorizontalHeaderLabels({"Game", "Emulator", "User", "Server", "IP"});
    wgTable->horizontalHeader()->setStretchLastSection(true);
    wgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    wgTable->setSelectionMode(QAbstractItemView::SingleSelection);
    wgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    wgTable->setSortingEnabled(true);
    wgTable->horizontalHeader()->setMinimumSectionSize(16);
    wgLayout->addWidget(wgTable);

    auto* wgBtnLayout = new QHBoxLayout();
    auto* btnAddToList = new QPushButton("Favorite Server", wgDialog);
    auto* btnWgClose = new QPushButton("Close", wgDialog);
    wgBtnLayout->addWidget(btnAddToList);
    wgBtnLayout->addStretch();
    wgBtnLayout->addWidget(btnWgClose);
    wgLayout->addLayout(wgBtnLayout);

    connect(btnWgClose, &QPushButton::clicked, wgDialog, &QDialog::accept);

    // Parse: pipe-delimited fields, 7 per entry
    // GameName|IP:Port|Username|EmuName|WaitingPlayers|ServerName|ServerLocation|...
    QStringList fields = QString::fromUtf8(data).split('|', Qt::SkipEmptyParts);

    int total = 0;
    wgTable->setSortingEnabled(false);
    for (int i = 0; i + 6 < fields.size(); i += 7)
    {
        QString gameName = fields[i].trimmed();
        QString hostPort = fields[i + 1].trimmed();
        QString username = fields[i + 2].trimmed();
        QString emulator = fields[i + 3].trimmed();
        QString serverName = fields[i + 5].trimmed();

        if (isPrivateHostPort(hostPort)) continue;

        int row = wgTable->rowCount();
        wgTable->insertRow(row);
        wgTable->setItem(row, 0, new QTableWidgetItem(gameName));
        wgTable->setItem(row, 1, new QTableWidgetItem(emulator));
        wgTable->setItem(row, 2, new QTableWidgetItem(username));
        wgTable->setItem(row, 3, new QTableWidgetItem(serverName));
        wgTable->setItem(row, 4, new QTableWidgetItem(hostPort));
        total++;
    }

    wgTable->setSortingEnabled(true);

    connect(btnAddToList, &QPushButton::clicked, this, [this, wgTable, wgDialog]() {
        int row = wgTable->currentRow();
        if (row < 0) return;

        QString serverName = wgTable->item(row, 3)->text();
        QString hostPort = wgTable->item(row, 4)->text();

        if (favoriteServerIndexByHost(hostPort) >= 0)
        {
            QMessageBox::information(wgDialog, "Already Favorited",
                "This server is already in your favorites.");
            return;
        }

        toggleFavoriteServer(hostPort, serverName);
    });

    wgDialog->exec();
    delete wgDialog;
}

void KailleraNetplayDialog::onConnectServer()
{
    int row = m_serverTable->currentRow();
    if (row < 0 || row >= m_displayServers.size()) return;
    const ServerEntry& entry = m_displayServers[row];

    // Parse host:port
    QString hostStr = entry.host;
    QByteArray ipBytes;
    int port = 27888;
    int colonIdx = hostStr.lastIndexOf(':');
    if (colonIdx >= 0)
    {
        ipBytes = hostStr.left(colonIdx).toUtf8();
        port = hostStr.mid(colonIdx + 1).toInt();
        if (port == 0) port = 27888;
    }
    else
    {
        ipBytes = hostStr.toUtf8();
    }

    // Set spoof ping from frame delay combo
    int fdlyIndex = m_frameDelayCombo->currentIndex();
    if (fdlyIndex > 0 && fdlyIndex <= 9)
    {
        kaillera_set_spoof_ping(fdlyIndex * 16 - 8);
    }
    else
    {
        kaillera_set_spoof_ping(0);
    }

    // Get username
    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    // Initialize kaillera core for server mode
    if (kaillera_core_initialize(0, APP, usernameBytes.data(), 1))
    {
        const bool stateTimerWasRunning =
            (m_stateMachineTimer != nullptr && m_stateMachineTimer->isActive());

        // IMPORTANT: pause the n02 state-machine timer while connect runs on a
        // worker thread. Both paths touch shared socket globals inside n02, and
        // running them concurrently can corrupt state and crash on teardown.
        if (stateTimerWasRunning)
        {
            m_stateMachineTimer->stop();
        }

        // Run connect on a background thread so the UI stays responsive
        // (kaillera_core_connect blocks for up to 15 seconds on timeout)
        auto connectFuture = std::async(std::launch::async, [&]() {
            return kaillera_core_connect(ipBytes.data(), port);
        });

        // Show a progress dialog while connecting
        QProgressDialog progress("Connecting to " + entry.name + "...",
                                 "Cancel", 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.show();

        // Poll until the future completes or user cancels
        while (connectFuture.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
        {
            QApplication::processEvents();
            if (progress.wasCanceled())
            {
                // Can't cancel the blocking socket wait, so keep processing events until it finishes
                while (connectFuture.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
                    QApplication::processEvents();
                kaillera_core_cleanup();
                if (stateTimerWasRunning && m_stateMachineTimer != nullptr)
                {
                    m_stateMachineTimer->start(1);
                }
                return;
            }
        }
        progress.close();

        const bool connected = connectFuture.get();
        if (stateTimerWasRunning && m_stateMachineTimer != nullptr)
        {
            m_stateMachineTimer->start(1);
        }

        if (connected)
        {
            // Hide the netplay dialog while the server browser is open
            hide();

            // Open the server browser dialog as a standalone top-level window
            // so it doesn't stay on top of the emulator frame
            KailleraServerBrowserDialog browser(entry.name, nullptr);
            browser.show();

            QEventLoop loop;
            connect(&browser, &QDialog::finished, &loop, &QEventLoop::quit);
            loop.exec();
            // Browser dialog handles disconnect/cleanup on close

            // Re-show the netplay dialog, unless the main window
            // is closing (user clicked X on the emulator window)
            if (parentWidget() && parentWidget()->isVisible())
                show();
            else
                accept();
        }
        else
        {
            QString errorMsg = QString::fromUtf8(kaillera_core_get_last_error());
            kaillera_core_cleanup();
            if (errorMsg.isEmpty())
            {
                errorMsg = "Failed to connect to server";
            }
            QMessageBox::warning(this, "Connection Error",
                                 errorMsg + "\n\nServer: " + entry.name);
        }
    }
    else
    {
        QMessageBox::warning(this, "Connection Error", "Failed to initialize Kaillera core.");
    }
}

void KailleraNetplayDialog::onServerDoubleClicked(int row, int column)
{
    (void)column;
    if (row >= 0 && row < m_displayServers.size())
    {
        m_serverTable->selectRow(row);
        onConnectServer();
    }
}

void KailleraNetplayDialog::onP2PHost()
{
    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    // Use selected game from the host picker
    QString gameName = (m_p2pGameCombo != nullptr) ? m_p2pGameCombo->currentText().trimmed() : QString();
    if (gameName.isEmpty())
    {
        QMessageBox::warning(this, "P2P Host", "No game selected. Choose a ROM to host.");
        return;
    }
    QByteArray gameBytes = gameName.toUtf8();

    int port = m_p2pPortEdit->text().toInt();
    if (port <= 0 || port > 65535) port = 27886;

    if (p2p_core_initialize(true, port, APP, gameBytes.data(), usernameBytes.data()))
    {
        hide();

        QString username = QString::fromUtf8(usernameBytes);
        KailleraP2PDialog p2pDialog(true, gameName, username, QString(), nullptr);
        p2pDialog.show();

        QEventLoop loop;
        connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
        loop.exec();

        show();
    }
    else
    {
        QMessageBox::warning(this, "P2P Host", "Failed to initialize P2P core.");
    }
}

// Check if a string looks like a NAT traversal code rather than an IP address.
static bool looksLikeTraversalCode(const QString& s)
{
    if (s.isEmpty()) return false;
    for (const QChar& ch : s)
    {
        if (ch == '.' || ch == ':' || ch == '/') return false;
    }
    int alnumCount = 0;
    for (const QChar& ch : s)
    {
        if (ch.isLetterOrNumber()) { alnumCount++; continue; }
        if (ch == '-' || ch == '_') continue;
        return false;
    }
    return (alnumCount >= 6 && alnumCount <= 16);
}

void KailleraNetplayDialog::onP2PJoin()
{
    QString addrText = m_p2pHostEdit->text().trimmed();
    addrText.remove(' ');
    if (addrText.isEmpty())
    {
        QMessageBox::warning(this, "P2P Join", "Please enter a connect code or host address (ip:port).");
        return;
    }

    QByteArray usernameBytes = m_usernameEdit->text().toUtf8();
    if (usernameBytes.isEmpty()) usernameBytes = "Player";

    bool isCode = looksLikeTraversalCode(addrText);

    if (p2p_core_initialize(false, 0, APP, (char*)"", usernameBytes.data()))
    {
        if (isCode)
        {
            // Join by traversal code — the dialog handles connecting via NAT traversal
            rememberP2PStoredEntry(addrText);
            hide();

            QString username = QString::fromUtf8(usernameBytes);
            KailleraP2PDialog p2pDialog(false, QString(), username, addrText, nullptr);
            connect(&p2pDialog, &KailleraP2PDialog::peerNicknameResolved, this,
                [this, addrText](const QString& nickname) {
                    updateP2PStoredNickname(addrText, nickname);
                });
            p2pDialog.show();

            QEventLoop loop;
            connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
            loop.exec();

            show();
        }
        else
        {
            // Join by direct IP:port
            QByteArray ipBytes;
            int port = 27886;
            int colonIdx = addrText.lastIndexOf(':');
            if (colonIdx >= 0)
            {
                ipBytes = addrText.left(colonIdx).toUtf8();
                port = addrText.mid(colonIdx + 1).toInt();
                if (port == 0) port = 27886;
            }
            else
            {
                ipBytes = addrText.toUtf8();
            }

            if (p2p_core_connect(ipBytes.data(), port))
            {
                rememberP2PStoredEntry(addrText);
                hide();

                QString username = QString::fromUtf8(usernameBytes);
                KailleraP2PDialog p2pDialog(false, QString(), username, QString(), nullptr);
                connect(&p2pDialog, &KailleraP2PDialog::peerNicknameResolved, this,
                    [this, addrText](const QString& nickname) {
                        updateP2PStoredNickname(addrText, nickname);
                    });
                p2pDialog.show();

                QEventLoop loop;
                connect(&p2pDialog, &QDialog::finished, &loop, &QEventLoop::quit);
                loop.exec();

                show();
            }
            else
            {
                p2p_core_cleanup();
                QMessageBox::warning(this, "P2P Join", "Failed to connect to host: " + addrText);
            }
        }
    }
    else
    {
        QMessageBox::warning(this, "P2P Join", "Failed to initialize P2P core.");
    }
}

// ---- P2P recent/favorite peers ----

void KailleraNetplayDialog::loadP2PStoredUsers()
{
    QSettings settings("RMG-K", "n02");
    m_p2pStoredUsers.clear();

    int count = settings.value("P2P_HistoryCount", -1).toInt();
    if (count >= 0)
    {
        for (int i = 0; i < count; i++)
        {
            P2PStoredEntry entry;
            entry.name = settings.value(QString("P2P_HistoryName_%1").arg(i)).toString().trimmed();
            entry.host = settings.value(QString("P2P_HistoryHost_%1").arg(i)).toString().trimmed();
            entry.favorite = settings.value(QString("P2P_HistoryFavorite_%1").arg(i), false).toBool();
            if (!entry.host.isEmpty())
            {
                m_p2pStoredUsers.append(entry);
            }
        }
    }
    else
    {
        count = settings.value("P2P_StoredCount", 0).toInt();
        for (int i = 0; i < count; i++)
        {
            P2PStoredEntry entry;
            entry.name = settings.value(QString("P2P_StoredName_%1").arg(i)).toString().trimmed();
            entry.host = settings.value(QString("P2P_StoredHost_%1").arg(i)).toString().trimmed();
            if (!entry.host.isEmpty())
            {
                m_p2pStoredUsers.append(entry);
            }
        }
    }

    QVector<P2PStoredEntry> favorites;
    QVector<P2PStoredEntry> recents;
    favorites.reserve(m_p2pStoredUsers.size());
    recents.reserve(m_p2pStoredUsers.size());
    for (const auto& entry : m_p2pStoredUsers)
    {
        if (entry.favorite)
        {
            favorites.append(entry);
        }
        else
        {
            recents.append(entry);
        }
    }
    m_p2pStoredUsers = favorites;
    m_p2pStoredUsers += recents;

    while (m_p2pStoredUsers.size() - p2pFavoriteCount() > kMaxP2PRecentEntries)
    {
        m_p2pStoredUsers.removeLast();
    }

    refreshP2PStoredDisplay();
}

void KailleraNetplayDialog::saveP2PStoredUsers()
{
    QSettings settings("RMG-K", "n02");
    const int cleanupCount = std::max(
        settings.value("P2P_HistoryCount", 0).toInt(),
        settings.value("P2P_StoredCount", 0).toInt());
    settings.setValue("P2P_HistoryCount", m_p2pStoredUsers.size());
    for (int i = 0; i < cleanupCount; i++)
    {
        settings.remove(QString("P2P_HistoryName_%1").arg(i));
        settings.remove(QString("P2P_HistoryHost_%1").arg(i));
        settings.remove(QString("P2P_HistoryFavorite_%1").arg(i));
        settings.remove(QString("P2P_StoredName_%1").arg(i));
        settings.remove(QString("P2P_StoredHost_%1").arg(i));
    }
    for (int i = 0; i < m_p2pStoredUsers.size(); i++)
    {
        settings.setValue(QString("P2P_HistoryName_%1").arg(i), m_p2pStoredUsers[i].name);
        settings.setValue(QString("P2P_HistoryHost_%1").arg(i), m_p2pStoredUsers[i].host);
        settings.setValue(QString("P2P_HistoryFavorite_%1").arg(i), m_p2pStoredUsers[i].favorite);
    }
    settings.setValue("P2P_StoredCount", 0);
}

void KailleraNetplayDialog::refreshP2PStoredDisplay()
{
    if (!m_p2pStoredTable) return;
    m_p2pStoredTable->setRowCount(m_p2pStoredUsers.size());
    for (int i = 0; i < m_p2pStoredUsers.size(); i++)
    {
        auto* favoriteItem = new QTableWidgetItem();
        favoriteItem->setData(Qt::UserRole, themedFavoriteIcon(m_p2pStoredUsers[i].favorite ? "star-fill" : "star"));
        favoriteItem->setTextAlignment(Qt::AlignCenter);
        favoriteItem->setFlags((favoriteItem->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
            & ~Qt::ItemIsEditable);
        m_p2pStoredTable->setItem(i, 0, favoriteItem);
        m_p2pStoredTable->setItem(i, 1, new QTableWidgetItem(
            m_p2pStoredUsers[i].name.isEmpty() ? "-" : m_p2pStoredUsers[i].name));
        m_p2pStoredTable->setItem(i, 2, new QTableWidgetItem(m_p2pStoredUsers[i].host));
    }
}

void KailleraNetplayDialog::onP2PStoredClicked(int row, int column)
{
    if (row >= 0 && row < m_p2pStoredUsers.size())
    {
        if (column == 0)
        {
            toggleP2PStoredFavorite(row);
            return;
        }

        m_p2pStoredTable->selectRow(row);
        m_p2pHostEdit->setText(m_p2pStoredUsers[row].host);
    }
}

int KailleraNetplayDialog::p2pStoredIndexByHost(const QString& host) const
{
    for (int i = 0; i < m_p2pStoredUsers.size(); ++i)
    {
        if (m_p2pStoredUsers[i].host == host)
        {
            return i;
        }
    }
    return -1;
}

int KailleraNetplayDialog::p2pFavoriteCount() const
{
    int count = 0;
    while (count < m_p2pStoredUsers.size() && m_p2pStoredUsers[count].favorite)
    {
        ++count;
    }
    return count;
}

void KailleraNetplayDialog::toggleP2PStoredFavorite(int row)
{
    if (row < 0 || row >= m_p2pStoredUsers.size())
    {
        return;
    }

    P2PStoredEntry entry = m_p2pStoredUsers[row];
    m_p2pStoredUsers.removeAt(row);
    entry.favorite = !entry.favorite;

    const int insertIndex = p2pFavoriteCount();
    m_p2pStoredUsers.insert(insertIndex, entry);
    refreshP2PStoredDisplay();
    m_p2pStoredTable->selectRow(insertIndex);
    saveP2PStoredUsers();
}

void KailleraNetplayDialog::rememberP2PStoredEntry(const QString& host, const QString& nickname)
{
    QString normalizedHost = host.trimmed();
    normalizedHost.remove(' ');
    if (normalizedHost.isEmpty())
    {
        return;
    }

    const int existingIndex = p2pStoredIndexByHost(normalizedHost);
    if (existingIndex >= 0)
    {
        P2PStoredEntry entry = m_p2pStoredUsers[existingIndex];
        if (!nickname.trimmed().isEmpty())
        {
            entry.name = nickname.trimmed();
        }
        if (entry.favorite)
        {
            m_p2pStoredUsers[existingIndex] = entry;
            refreshP2PStoredDisplay();
            saveP2PStoredUsers();
            return;
        }

        m_p2pStoredUsers.removeAt(existingIndex);
        m_p2pStoredUsers.insert(p2pFavoriteCount(), entry);
        while (m_p2pStoredUsers.size() - p2pFavoriteCount() > kMaxP2PRecentEntries)
        {
            m_p2pStoredUsers.removeLast();
        }
        refreshP2PStoredDisplay();
        saveP2PStoredUsers();
        return;
    }

    m_p2pStoredUsers.insert(p2pFavoriteCount(), {nickname.trimmed(), normalizedHost, false});
    while (m_p2pStoredUsers.size() - p2pFavoriteCount() > kMaxP2PRecentEntries)
    {
        m_p2pStoredUsers.removeLast();
    }

    refreshP2PStoredDisplay();
    saveP2PStoredUsers();
}

void KailleraNetplayDialog::updateP2PStoredNickname(const QString& host, const QString& nickname)
{
    const QString trimmedNickname = nickname.trimmed();
    if (trimmedNickname.isEmpty())
    {
        return;
    }

    rememberP2PStoredEntry(host, trimmedNickname);
}

void KailleraNetplayDialog::onP2PPasteAndGo()
{
    QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.isEmpty()) return;

    // Remove spaces (connect codes shouldn't have spaces)
    clip.remove(' ');

    m_p2pHostEdit->setText(clip);
    onP2PJoin();
}

void KailleraNetplayDialog::onP2PWaitingGames()
{
    KailleraWaitingGamesDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString code = dlg.selectedCode();
    QString host = dlg.selectedHost();
    if (code.isEmpty() && host.isEmpty()) return;

    // Fill the address field and connect
    if (!code.isEmpty())
        m_p2pHostEdit->setText(code);
    else
        m_p2pHostEdit->setText(host);

    onP2PJoin();
}

#endif // _WIN32
