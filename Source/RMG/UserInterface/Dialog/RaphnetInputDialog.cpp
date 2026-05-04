/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "RaphnetInputDialog.hpp"

#include <hidapi.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <cstring>

// Raphnet adapter USB vendor ID
#define RAPHNET_VID 0x289b

// raphnet HID protocol request for raw SI command
#define RQ_GCN64_RAW_SI_COMMAND 0x80

// N64 controller command to read button/axis state
#define N64_GET_STATUS 0x01

// N64 button bit masks (byte 0, MSB first)
#define N64_BTN_A       0x8000
#define N64_BTN_B       0x4000
#define N64_BTN_Z       0x2000
#define N64_BTN_START   0x1000
#define N64_BTN_DUP     0x0800
#define N64_BTN_DDOWN   0x0400
#define N64_BTN_DLEFT   0x0200
#define N64_BTN_DRIGHT  0x0100
// byte 1
#define N64_BTN_L       0x0020
#define N64_BTN_R       0x0010
#define N64_BTN_CUP     0x0008
#define N64_BTN_CDOWN   0x0004
#define N64_BTN_CLEFT   0x0002
#define N64_BTN_CRIGHT  0x0001

using namespace UserInterface;

RaphnetInputDialog::RaphnetInputDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Raphnet Adapter - Input Test");
    setMinimumSize(420, 400);

    setupUi();

    m_PollTimer = new QTimer(this);
    connect(m_PollTimer, &QTimer::timeout, this, &RaphnetInputDialog::onPollTimer);

    if (openAdapter())
    {
        m_StatusLabel->setText("Adapter connected. Press buttons on the N64 controller.");
        m_PollTimer->start(16); // ~60Hz
    }
    else
    {
        m_StatusLabel->setText("No raphnet adapter found. Connect one and reopen this dialog.");
    }
}

RaphnetInputDialog::~RaphnetInputDialog()
{
    if (m_PollTimer->isActive())
    {
        m_PollTimer->stop();
    }
    closeAdapter();
}

void RaphnetInputDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_StatusLabel = new QLabel("Searching for adapter...", this);
    mainLayout->addWidget(m_StatusLabel);

    // Buttons group
    QGroupBox* buttonsGroup = new QGroupBox("Buttons", this);
    QGridLayout* buttonsGrid = new QGridLayout(buttonsGroup);

    // Define all N64 buttons with their masks
    struct ButtonDef { QString name; uint16_t mask; };
    ButtonDef buttonDefs[] = {
        { "A",       N64_BTN_A },
        { "B",       N64_BTN_B },
        { "Z",       N64_BTN_Z },
        { "Start",   N64_BTN_START },
        { "L",       N64_BTN_L },
        { "R",       N64_BTN_R },
        { "D-Up",    N64_BTN_DUP },
        { "D-Down",  N64_BTN_DDOWN },
        { "D-Left",  N64_BTN_DLEFT },
        { "D-Right", N64_BTN_DRIGHT },
        { "C-Up",    N64_BTN_CUP },
        { "C-Down",  N64_BTN_CDOWN },
        { "C-Left",  N64_BTN_CLEFT },
        { "C-Right", N64_BTN_CRIGHT },
    };

    int col = 0, row = 0;
    for (auto& def : buttonDefs)
    {
        QLabel* nameLabel = new QLabel(def.name + ":", buttonsGroup);
        QLabel* stateLabel = new QLabel(buttonsGroup);
        stateLabel->setFixedSize(16, 16);
        stateLabel->setAlignment(Qt::AlignCenter);
        stateLabel->setStyleSheet("QLabel { background-color: gray; border-radius: 3px; }");

        buttonsGrid->addWidget(nameLabel, row, col * 3);
        buttonsGrid->addWidget(stateLabel, row, col * 3 + 1);

        // Add spacer between columns
        if (col == 0)
        {
            QWidget* spacer = new QWidget(buttonsGroup);
            spacer->setFixedWidth(20);
            buttonsGrid->addWidget(spacer, row, 2);
        }

        m_ButtonIndicators.push_back({ def.name, def.mask, stateLabel });

        col++;
        if (col >= 2)
        {
            col = 0;
            row++;
        }
    }
    mainLayout->addWidget(buttonsGroup);

    // Axes group
    QGroupBox* axesGroup = new QGroupBox("Analog Stick", this);
    QGridLayout* axesGrid = new QGridLayout(axesGroup);

    axesGrid->addWidget(new QLabel("X Axis:", axesGroup), 0, 0);
    m_XAxisValue = new QLabel("0", axesGroup);
    m_XAxisValue->setFixedWidth(40);
    m_XAxisValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    axesGrid->addWidget(m_XAxisValue, 0, 1);
    m_XAxisBar = new QProgressBar(axesGroup);
    m_XAxisBar->setRange(-128, 127);
    m_XAxisBar->setValue(0);
    m_XAxisBar->setTextVisible(false);
    axesGrid->addWidget(m_XAxisBar, 0, 2);

    axesGrid->addWidget(new QLabel("Y Axis:", axesGroup), 1, 0);
    m_YAxisValue = new QLabel("0", axesGroup);
    m_YAxisValue->setFixedWidth(40);
    m_YAxisValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    axesGrid->addWidget(m_YAxisValue, 1, 1);
    m_YAxisBar = new QProgressBar(axesGroup);
    m_YAxisBar->setRange(-128, 127);
    m_YAxisBar->setValue(0);
    m_YAxisBar->setTextVisible(false);
    axesGrid->addWidget(m_YAxisBar, 1, 2);

    mainLayout->addWidget(axesGroup);

    // Close button
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

bool RaphnetInputDialog::openAdapter()
{
    hid_init();

    // Enumerate all raphnet devices
    struct hid_device_info* devs = hid_enumerate(RAPHNET_VID, 0);
    struct hid_device_info* cur = devs;

    while (cur)
    {
        // Open the first raphnet device found on the management interface (1 or 2)
        if (cur->interface_number >= 1)
        {
            m_HidDevice = hid_open_path(cur->path);
            if (m_HidDevice)
            {
                // Pre-3.4 adapters use 40-byte reports, newer use 63
                // We'll default to 63 and handle errors gracefully
                m_ReportSize = 63;
                hid_set_nonblocking(m_HidDevice, 1);
                break;
            }
        }
        cur = cur->next;
    }

    hid_free_enumeration(devs);
    return m_HidDevice != nullptr;
}

void RaphnetInputDialog::closeAdapter()
{
    if (m_HidDevice)
    {
        hid_close(m_HidDevice);
        m_HidDevice = nullptr;
    }
    hid_exit();
}

bool RaphnetInputDialog::pollController(uint16_t& buttons, int8_t& xAxis, int8_t& yAxis)
{
    if (!m_HidDevice)
        return false;

    // Build raw SI command: request N64 controller status
    // Protocol: [RQ_GCN64_RAW_SI_COMMAND, channel, tx_len, N64_GET_STATUS]
    // Max report size is 63 bytes + 1 for report ID
    unsigned char cmd[64];
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0x00; // HID report ID
    cmd[1] = RQ_GCN64_RAW_SI_COMMAND;
    cmd[2] = 0x00; // channel 0
    cmd[3] = 0x01; // tx_len = 1 byte
    cmd[4] = N64_GET_STATUS;

    int n = hid_send_feature_report(m_HidDevice, cmd, m_ReportSize + 1);
    if (n < 0)
        return false;

    // Read response
    unsigned char response[64];
    memset(response, 0, sizeof(response));
    response[0] = 0x00; // report ID

    n = hid_get_feature_report(m_HidDevice, response, m_ReportSize + 1);
    if (n < 0)
        return false;

    // Response format: [report_type, ..., rx_len, data...]
    // The raw SI response starts at offset 3 in the report payload (offset 4 with report ID)
    // response[1] = echo of request type
    // response[2] = channel
    // response[3] = rx_len
    // response[4..] = rx_data

    unsigned char rxLen = response[3];
    if (rxLen < 4)
        return false;

    // Parse N64 controller status (4 bytes)
    buttons = (static_cast<uint16_t>(response[4]) << 8) | response[5];
    xAxis = static_cast<int8_t>(response[6]);
    yAxis = static_cast<int8_t>(response[7]);

    return true;
}

void RaphnetInputDialog::onPollTimer()
{
    uint16_t buttons = 0;
    int8_t xAxis = 0, yAxis = 0;

    if (pollController(buttons, xAxis, yAxis))
    {
        updateButtonIndicators(buttons);
        updateAxisDisplay(xAxis, yAxis);
    }
}

void RaphnetInputDialog::updateButtonIndicators(uint16_t buttons)
{
    for (auto& indicator : m_ButtonIndicators)
    {
        bool pressed = (buttons & indicator.mask) != 0;
        indicator.stateLabel->setStyleSheet(pressed
            ? "QLabel { background-color: #4CAF50; border-radius: 3px; }"
            : "QLabel { background-color: gray; border-radius: 3px; }");
    }
}

void RaphnetInputDialog::updateAxisDisplay(int8_t x, int8_t y)
{
    m_XAxisValue->setText(QString::number(x));
    m_YAxisValue->setText(QString::number(y));
    m_XAxisBar->setValue(x);
    m_YAxisBar->setValue(y);
}
