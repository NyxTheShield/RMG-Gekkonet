/*
 * Shared table styling helpers for Kaillera dialogs.
 * Suppresses native Windows accent rendering on selected/focused items.
 */
#pragma once

#include <QItemDelegate>
#include <QHeaderView>
#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>

class NoAccentTableStyle final : public QProxyStyle
{
public:
    NoAccentTableStyle(QStyle* style) : QProxyStyle(style) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == PE_PanelItemViewItem || element == PE_FrameFocusRect
            || element == PE_PanelItemViewRow)
        {
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element, const QStyleOption* option,
        QPainter* painter, const QWidget* widget) const override
    {
        if (element == CE_ItemViewItem)
        {
            QStyleOptionViewItem opt(*qstyleoption_cast<const QStyleOptionViewItem*>(option));
            opt.state &= ~(State_HasFocus | State_Selected | State_MouseOver);
            QProxyStyle::drawControl(element, &opt, painter, widget);
            return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

};

class CompactHeaderStyle final : public QProxyStyle
{
public:
    CompactHeaderStyle(QStyle* style) : QProxyStyle(style) {}

    int pixelMetric(PixelMetric metric, const QStyleOption* option,
        const QWidget* widget) const override
    {
        switch (metric)
        {
        case PM_HeaderMargin:
            return 3;
        case PM_HeaderMarkSize:
            return 10;
        default:
            break;
        }

        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

class NoAccentDelegate final : public QItemDelegate
{
public:
    using QItemDelegate::QItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        opt.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected | QStyle::State_MouseOver);

        if (option.state & QStyle::State_Selected)
        {
            painter->fillRect(option.rect, QColor(100, 149, 237, 60));
        }
        else if (option.state & QStyle::State_MouseOver)
        {
            painter->fillRect(option.rect, QColor(100, 149, 237, 30));
        }

        QItemDelegate::paint(painter, opt, index);
    }
};

inline void applyNoAccentStyle(QTableWidget* table)
{
    table->setStyle(new NoAccentTableStyle(table->style()));
    table->viewport()->setStyle(table->style());

    if (table->horizontalHeader() != nullptr)
    {
        table->horizontalHeader()->setStyle(new CompactHeaderStyle(QApplication::style()));
    }
    table->setItemDelegate(new NoAccentDelegate(table));
    table->setFocusPolicy(Qt::NoFocus);
}

inline void installHeaderDoubleClickSortToggle(QTableWidget* table)
{
    if (table == nullptr || table->horizontalHeader() == nullptr)
    {
        return;
    }

    QObject::connect(table->horizontalHeader(), &QHeaderView::sectionDoubleClicked, table,
        [table](int logicalIndex)
    {
        if (table == nullptr || table->horizontalHeader() == nullptr
            || logicalIndex < 0 || logicalIndex >= table->columnCount())
        {
            return;
        }

        const int currentSection = table->horizontalHeader()->sortIndicatorSection();
        const Qt::SortOrder currentOrder = table->horizontalHeader()->sortIndicatorOrder();
        const Qt::SortOrder nextOrder =
            (currentSection == logicalIndex && currentOrder == Qt::AscendingOrder)
                ? Qt::DescendingOrder
                : Qt::AscendingOrder;

        table->sortByColumn(logicalIndex, nextOrder);
    });
}
