#pragma once
#include "core/NativeVideoEncoder.h"
#include <CoreVideo/CoreVideo.h>
namespace luma {
using MacFrameSource = std::function<void(const std::function<void(CVPixelBufferRef, QRect)> &)>;
std::unique_ptr<NativeVideoEncoder> createMacGpuEncoder(MacFrameSource, const Settings &,
                                                        const QString &);
} // namespace luma
