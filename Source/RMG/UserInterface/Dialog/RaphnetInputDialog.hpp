/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef RAPHNETINPUTDIALOG_HPP
#define RAPHNETINPUTDIALOG_HPP

#include <QDialog>
#include <QLabel>
#include <QTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QProgressBar>
#include <QComboBox>
#include <array>
#include <vector>

struct hid_device_;
typedef struct hid_device_ hid_device;

namespace UserInterface
{

class RaphnetInputDialog : public QDialog
{
    Q_OBJECT

public:
    RaphnetInputDialog(QWidget* parent);
    ~RaphnetInputDialog();

private slots:
    void onPollTimer();

private:
    bool openAdapter();
    void closeAdapter();
    bool exchangeCommand(const unsigned char* command, int commandLength, unsigned char* response, int& responseLength);
    bool setAdapterPollingSuspended(bool suspended);
    bool pollController(uint16_t& buttons, int8_t& xAxis, int8_t& yAxis);

    void setupUi();
    void updateButtonIndicators(uint16_t buttons);
    void updateAxisDisplay(int8_t x, int8_t y);

    hid_device* m_HidDevice = nullptr;
    int m_ReportSize = 63;
    int m_ChannelCount = 1;
    int m_FailedPollCount = 0;
    QTimer* m_PollTimer = nullptr;

    // Button indicators (label + state label pairs)
    struct ButtonIndicator
    {
        QString name;
        uint16_t mask;
        QLabel* stateLabel;
    };
    std::vector<ButtonIndicator> m_ButtonIndicators;

    // Axis displays
    QLabel* m_XAxisValue = nullptr;
    QLabel* m_YAxisValue = nullptr;
    QProgressBar* m_XAxisBar = nullptr;
    QProgressBar* m_YAxisBar = nullptr;

    QLabel* m_StatusLabel = nullptr;
    QComboBox* m_PortComboBox = nullptr;
};

} // namespace UserInterface

#endif // RAPHNETINPUTDIALOG_HPP
