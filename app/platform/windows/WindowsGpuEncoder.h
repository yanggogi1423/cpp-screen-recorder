#pragma once
#include "core/NativeVideoEncoder.h"
#include <windows.h>
#include <d3d11.h>
#include <wrl.h>
namespace luma {
struct WindowsGpuFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    QRect crop;
    QSize content;
};
using WindowsFrameSource =
    std::function<void(const std::function<void(const WindowsGpuFrame &)> &)>;
std::unique_ptr<NativeVideoEncoder> createWindowsGpuEncoder(ID3D11Device *, WindowsFrameSource,
                                                            const Settings &, const QString &);
} // namespace luma
