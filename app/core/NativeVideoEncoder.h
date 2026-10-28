#pragma once
#include "Types.h"
namespace luma {
class NativeVideoEncoder {
  public:
    virtual ~NativeVideoEncoder() = default;
    virtual void append(qint64 frame) = 0;
    virtual void finish() = 0;
};
// Synthetic GPU frames only: no screen, microphone or speaker is opened.
void probeNativeVideoEncoder(const Settings &, const QString &file);
} // namespace luma
