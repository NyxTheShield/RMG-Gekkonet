#pragma once

#include "EmulatorProxy.hpp"
#include "FfmpegEncoder.hpp"

#include <string>

namespace KailleraExport
{

void InitializeFrameCapture(EmulatorProxy* emulator,
                            FfmpegEncoder* encoder,
                            const FfmpegEncoderConfig& encoderConfig,
                            int expectedFrameCount);
void FrameCaptureCallback(unsigned int frameIndex);
void FlushFrameCapture();
bool GetFrameCaptureError(std::string* errorMessage);
int GetCapturedFrameCount();

} // namespace KailleraExport
