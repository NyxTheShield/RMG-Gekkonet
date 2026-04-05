#include "FrameCapture.hpp"

#include "PifReplay.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace KailleraExport
{

namespace
{

enum class EncodeSlotState
{
    Free,
    Filling,
    Queued,
    Encoding,
};

static constexpr int kEncodeSlotCount = 2;

static EmulatorProxy* s_Emulator = nullptr;
static FfmpegEncoder* s_Encoder = nullptr;
static FfmpegEncoderConfig s_EncoderConfig;
static bool s_EncoderOpened = false;
static std::atomic<int> s_CapturedFrames = 0;
static int s_SubmittedFrames = 0;
static int s_ExpectedFrameCount = 0;
static bool s_SpeedConfigured = false;
static int s_CurrentSpeedFactor = 0;
static int s_MinSpeedFactor = 0;
static int s_MaxSpeedFactor = 0;
static int s_SpeedStep = 0;
static int s_GovernorWindowFrames = 0;
static int s_GovernorBackpressureEvents = 0;
static int s_GovernorPeakQueueDepth = 0;
static bool s_EncodeThreadStarted = false;
static bool s_EncodeShutdown = false;
static std::atomic<bool> s_EncodeFailed = false;
static std::string s_EncodeErrorMessage;
static int s_FrameWidth = 0;
static int s_FrameHeight = 0;
static EncodeSlotState s_SlotStates[kEncodeSlotCount] = {
    EncodeSlotState::Free,
    EncodeSlotState::Free,
};
static std::vector<uint8_t> s_RawBuffers[kEncodeSlotCount];
static std::vector<uint8_t> s_FlippedBuffer;
static int s_QueuedSlotIndices[kEncodeSlotCount] = { 0, 0 };
static int s_QueueHead = 0;
static int s_QueueTail = 0;
static int s_QueueCount = 0;
static std::thread s_EncodeThread;
static std::mutex s_EncodeMutex;
static std::condition_variable s_EncodeWorkCv;
static std::condition_variable s_EncodeSpaceCv;

static bool allSlotsIdleLocked()
{
    if (s_QueueCount != 0)
    {
        return false;
    }

    for (int i = 0; i < kEncodeSlotCount; ++i)
    {
        if (s_SlotStates[i] != EncodeSlotState::Free)
        {
            return false;
        }
    }

    return true;
}

static void applySpeedFactor(int speedFactor, const char* reason)
{
    if (s_Emulator == nullptr)
    {
        return;
    }

    if (speedFactor <= 0 || speedFactor == s_CurrentSpeedFactor)
    {
        return;
    }

    int value = speedFactor;
    s_Emulator->coreDoCommand(M64CMD_CORE_STATE_SET, M64CORE_SPEED_FACTOR, &value);
    s_CurrentSpeedFactor = speedFactor;

    if (reason != nullptr && reason[0] != '\0')
    {
        fprintf(stderr, "Using replay export speed target: %d%% (%s)\n", speedFactor, reason);
    }
    else
    {
        fprintf(stderr, "Using replay export speed target: %d%%\n", speedFactor);
    }
}

static void releaseSlot(int slotIndex)
{
    {
        std::lock_guard<std::mutex> lock(s_EncodeMutex);
        s_SlotStates[slotIndex] = EncodeSlotState::Free;
    }
    s_EncodeSpaceCv.notify_all();
}

static void notifyEncodeFailure(const std::string& message)
{
    bool shouldStopEmulator = false;
    {
        std::lock_guard<std::mutex> lock(s_EncodeMutex);
        if (!s_EncodeFailed.load())
        {
            s_EncodeFailed.store(true);
            s_EncodeShutdown = true;
            s_EncodeErrorMessage = message;
            s_QueueHead = 0;
            s_QueueTail = 0;
            s_QueueCount = 0;
            for (int i = 0; i < kEncodeSlotCount; ++i)
            {
                if (s_SlotStates[i] == EncodeSlotState::Queued)
                {
                    s_SlotStates[i] = EncodeSlotState::Free;
                }
            }
            shouldStopEmulator = true;
        }
    }

    s_EncodeWorkCv.notify_all();
    s_EncodeSpaceCv.notify_all();

    if (shouldStopEmulator && s_Emulator != nullptr)
    {
        s_Emulator->stop();
    }
}

static int reserveFreeSlot()
{
    std::unique_lock<std::mutex> lock(s_EncodeMutex);
    bool hadFreeSlot = false;
    for (int i = 0; i < kEncodeSlotCount; ++i)
    {
        if (s_SlotStates[i] == EncodeSlotState::Free)
        {
            hadFreeSlot = true;
            break;
        }
    }
    if (!hadFreeSlot)
    {
        s_GovernorBackpressureEvents++;
    }

    s_EncodeSpaceCv.wait(lock, []()
    {
        if (s_EncodeFailed.load())
        {
            return true;
        }

        for (int i = 0; i < kEncodeSlotCount; ++i)
        {
            if (s_SlotStates[i] == EncodeSlotState::Free)
            {
                return true;
            }
        }

        return false;
    });

    if (s_EncodeFailed.load())
    {
        return -1;
    }

    for (int i = 0; i < kEncodeSlotCount; ++i)
    {
        if (s_SlotStates[i] == EncodeSlotState::Free)
        {
            s_SlotStates[i] = EncodeSlotState::Filling;
            return i;
        }
    }

    return -1;
}

static bool queueFilledSlot(int slotIndex)
{
    {
        std::lock_guard<std::mutex> lock(s_EncodeMutex);
        if (s_EncodeFailed.load())
        {
            s_SlotStates[slotIndex] = EncodeSlotState::Free;
            return false;
        }

        s_SlotStates[slotIndex] = EncodeSlotState::Queued;
        s_QueuedSlotIndices[s_QueueTail] = slotIndex;
        s_QueueTail = (s_QueueTail + 1) % kEncodeSlotCount;
        s_QueueCount++;
        s_GovernorPeakQueueDepth = std::max(s_GovernorPeakQueueDepth, s_QueueCount);
    }

    s_EncodeWorkCv.notify_one();
    return true;
}

static void maybeAdjustSpeedFactor()
{
    if (!s_SpeedConfigured)
    {
        return;
    }

    s_GovernorWindowFrames++;
    if (s_GovernorWindowFrames < 240)
    {
        return;
    }

    int nextSpeedFactor = s_CurrentSpeedFactor;
    const char* reason = nullptr;

    if (s_GovernorBackpressureEvents > 0 || s_GovernorPeakQueueDepth >= kEncodeSlotCount)
    {
        nextSpeedFactor = std::max(s_MinSpeedFactor, s_CurrentSpeedFactor - s_SpeedStep);
        reason = "auto-tuned down";
    }
    else if (s_CurrentSpeedFactor < s_MaxSpeedFactor)
    {
        nextSpeedFactor = std::min(s_MaxSpeedFactor, s_CurrentSpeedFactor + s_SpeedStep);
        reason = "auto-tuned up";
    }

    s_GovernorWindowFrames = 0;
    s_GovernorBackpressureEvents = 0;
    s_GovernorPeakQueueDepth = 0;

    if (nextSpeedFactor != s_CurrentSpeedFactor)
    {
        applySpeedFactor(nextSpeedFactor, reason);
    }
}

static void EncodeWorkerMain()
{
    for (;;)
    {
        int slotIndex = -1;
        {
            std::unique_lock<std::mutex> lock(s_EncodeMutex);
            s_EncodeWorkCv.wait(lock, []()
            {
                return s_EncodeShutdown || s_QueueCount > 0;
            });

            if (s_QueueCount == 0)
            {
                if (s_EncodeShutdown)
                {
                    return;
                }
                continue;
            }

            slotIndex = s_QueuedSlotIndices[s_QueueHead];
            s_QueueHead = (s_QueueHead + 1) % kEncodeSlotCount;
            s_QueueCount--;
            s_SlotStates[slotIndex] = EncodeSlotState::Encoding;
        }

        const int width = s_FrameWidth;
        const int height = s_FrameHeight;
        if (width <= 0 || height <= 0)
        {
            notifyEncodeFailure("Replay export encountered an invalid frame size");
            releaseSlot(slotIndex);
            return;
        }

        const int stride = width * 3;
        const size_t frameSize = static_cast<size_t>(stride) * static_cast<size_t>(height);
        if (s_FlippedBuffer.size() < frameSize)
        {
            s_FlippedBuffer.resize(frameSize);
        }

        const std::vector<uint8_t>& rawBuffer = s_RawBuffers[slotIndex];
        for (int y = 0; y < height; ++y)
        {
            memcpy(s_FlippedBuffer.data() + y * stride,
                   rawBuffer.data() + (height - 1 - y) * stride,
                   static_cast<size_t>(stride));
        }

        std::string errorMessage;
        if (!s_Encoder->writeFrame(s_FlippedBuffer.data(), width, height, &errorMessage))
        {
            notifyEncodeFailure(errorMessage);
            releaseSlot(slotIndex);
            return;
        }

        const int capturedFrames = s_CapturedFrames.fetch_add(1) + 1;
        if ((capturedFrames % 60) == 0)
        {
            if (s_ExpectedFrameCount > 0)
            {
                fprintf(stderr,
                        "Captured %d / %d frames...\n",
                        capturedFrames,
                        s_ExpectedFrameCount);
            }
            else
            {
                fprintf(stderr, "Captured %d frames...\n", capturedFrames);
            }
        }

        releaseSlot(slotIndex);
    }
}

static void startEncodeThread()
{
    if (s_EncodeThreadStarted)
    {
        return;
    }

    s_EncodeShutdown = false;
    s_EncodeThread = std::thread(EncodeWorkerMain);
    s_EncodeThreadStarted = true;
}

static void waitForEncodeThread()
{
    std::unique_lock<std::mutex> lock(s_EncodeMutex);
    s_EncodeSpaceCv.wait(lock, []()
    {
        return s_EncodeFailed.load() || allSlotsIdleLocked();
    });
}

static void stopEncodeThread()
{
    if (!s_EncodeThreadStarted)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_EncodeMutex);
        s_EncodeShutdown = true;
    }
    s_EncodeWorkCv.notify_all();

    if (s_EncodeThread.joinable())
    {
        s_EncodeThread.join();
    }

    s_EncodeThreadStarted = false;
}

} // namespace

void InitializeFrameCapture(EmulatorProxy* emulator,
                            FfmpegEncoder* encoder,
                            const FfmpegEncoderConfig& encoderConfig,
                            int expectedFrameCount)
{
    s_Emulator = emulator;
    s_Encoder = encoder;
    s_EncoderConfig = encoderConfig;
    s_EncoderOpened = false;
    s_CapturedFrames.store(0);
    s_SubmittedFrames = 0;
    s_ExpectedFrameCount = expectedFrameCount;
    s_SpeedConfigured = false;
    s_CurrentSpeedFactor = 0;
    s_MinSpeedFactor = 0;
    s_MaxSpeedFactor = 0;
    s_SpeedStep = 0;
    s_GovernorWindowFrames = 0;
    s_GovernorBackpressureEvents = 0;
    s_GovernorPeakQueueDepth = 0;
    s_EncodeThreadStarted = false;
    s_EncodeShutdown = false;
    s_EncodeFailed.store(false);
    s_EncodeErrorMessage.clear();
    s_FrameWidth = 0;
    s_FrameHeight = 0;
    s_QueueHead = 0;
    s_QueueTail = 0;
    s_QueueCount = 0;
    for (int i = 0; i < kEncodeSlotCount; ++i)
    {
        s_SlotStates[i] = EncodeSlotState::Free;
        s_RawBuffers[i].clear();
    }
    s_FlippedBuffer.clear();
}

int GetCapturedFrameCount()
{
    return s_CapturedFrames.load();
}

void FlushFrameCapture()
{
    waitForEncodeThread();
    stopEncodeThread();
    for (int i = 0; i < kEncodeSlotCount; ++i)
    {
        s_RawBuffers[i].clear();
        s_SlotStates[i] = EncodeSlotState::Free;
    }
    s_FlippedBuffer.clear();
}

bool GetFrameCaptureError(std::string* errorMessage)
{
    std::lock_guard<std::mutex> lock(s_EncodeMutex);
    if (!s_EncodeFailed.load())
    {
        return false;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = s_EncodeErrorMessage;
    }
    return true;
}

void FrameCaptureCallback(unsigned int)
{
    if (s_Emulator == nullptr || s_Encoder == nullptr)
    {
        return;
    }

    if (s_EncodeFailed.load())
    {
        s_Emulator->stop();
        return;
    }

    ResetPifReplayFrameSync();
    if (IsPifReplayFinished())
    {
        s_Emulator->stop();
        return;
    }

    if (s_ExpectedFrameCount > 0 &&
        s_SubmittedFrames >= (s_ExpectedFrameCount + 120))
    {
        fprintf(stderr,
                "Replay export reached safety frame limit (%d captured, %d expected), stopping.\n",
                s_SubmittedFrames,
                s_ExpectedFrameCount);
        s_Emulator->stop();
        return;
    }

    int width = s_FrameWidth;
    int height = s_FrameHeight;
    if (width <= 0 || height <= 0)
    {
        s_Emulator->readScreen(nullptr, &width, &height);
        if (width <= 0 || height <= 0)
        {
            return;
        }
    }

    if (!s_EncoderOpened)
    {
        s_EncoderConfig.width = width;
        s_EncoderConfig.height = height;
        s_FrameWidth = width;
        s_FrameHeight = height;

        std::string errorMessage;
        if (!s_Encoder->open(s_EncoderConfig, &errorMessage))
        {
            fprintf(stderr, "%s\n", errorMessage.c_str());
            s_Emulator->stop();
            return;
        }
        startEncodeThread();
        s_EncoderOpened = true;
    }

    if (!s_SpeedConfigured)
    {
        if (s_Encoder->isHardwareAccelerated())
        {
            s_MinSpeedFactor = 500;
            s_MaxSpeedFactor = 1500;
            s_SpeedStep = 100;
            applySpeedFactor(s_MinSpeedFactor, "auto start");
        }
        else
        {
            s_MinSpeedFactor = 300;
            s_MaxSpeedFactor = 500;
            s_SpeedStep = 50;
            applySpeedFactor(s_MinSpeedFactor, "cpu fallback");
        }
        s_SpeedConfigured = true;
    }

    const int slotIndex = reserveFreeSlot();
    if (slotIndex < 0)
    {
        s_Emulator->stop();
        return;
    }

    const size_t frameSize = static_cast<size_t>(s_FrameWidth) * static_cast<size_t>(s_FrameHeight) * 3U;
    if (s_RawBuffers[slotIndex].size() < frameSize)
    {
        s_RawBuffers[slotIndex].resize(frameSize);
    }

    width = s_FrameWidth;
    height = s_FrameHeight;
    s_Emulator->readScreen(s_RawBuffers[slotIndex].data(), &width, &height);
    if (width <= 0 || height <= 0)
    {
        releaseSlot(slotIndex);
        return;
    }

    if (width != s_FrameWidth || height != s_FrameHeight)
    {
        releaseSlot(slotIndex);
        notifyEncodeFailure("Replay export render size changed unexpectedly");
        s_Emulator->stop();
        return;
    }

    if (!queueFilledSlot(slotIndex))
    {
        s_Emulator->stop();
        return;
    }

    s_SubmittedFrames++;
    maybeAdjustSpeedFactor();
}

} // namespace KailleraExport
