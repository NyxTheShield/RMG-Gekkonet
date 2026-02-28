/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef KAILLERAWAITINGGAMESDIALOG_HPP
#define KAILLERAWAITINGGAMESDIALOG_HPP

#ifdef _WIN32

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class KailleraWaitingGamesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KailleraWaitingGamesDialog(QWidget* parent = nullptr);

    // After exec(), these return the selected game's connection info
    QString selectedCode() const { return m_selectedCode; }
    QString selectedHost() const { return m_selectedHost; }

private slots:
    void onFetchFinished(QNetworkReply* reply);
    void onConnect();
    void onRefresh();
    void onRowDoubleClicked(int row, int column);

private:
    void fetchList();
    void parseResponse(const QByteArray& data);

    QTableWidget* m_table = nullptr;
    QPushButton* m_btnConnect = nullptr;
    QPushButton* m_btnRefresh = nullptr;
    QPushButton* m_btnClose = nullptr;
    QLabel* m_statusLabel = nullptr;
    QNetworkAccessManager* m_netManager = nullptr;

    QString m_selectedCode;
    QString m_selectedHost;
};

#endif // _WIN32
#endif // KAILLERAWAITINGGAMESDIALOG_HPP
