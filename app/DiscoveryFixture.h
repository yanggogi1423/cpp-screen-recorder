#pragma once
#include "core/Platform.h"
#include <atomic>

class DiscoveryFixture final : public luma::Platform {
  public:
    std::atomic<int> phase{0}, calls{0};
    QList<luma::Source> sources() override {
        ++calls;
        return {{"display", "Fixture display", luma::SourceKind::Display, QRect(0, 0, 1920, 1080)}};
    }
    QList<luma::AudioSource> audioSources() override {
        QList<luma::AudioSource> result;
        if (phase.load() < 2)
            result.append({"mic", "USB Microphone", "", luma::AudioKind::Microphone});
        result.append({"system", "Speakers", "", luma::AudioKind::System});
        if (phase.load() > 0)
            result.append({"app", "Discord", "", luma::AudioKind::Application});
        return result;
    }
    std::unique_ptr<luma::VideoCapture> createVideoCapture() override {
        return {};
    }
    std::unique_ptr<luma::AudioCapture> createAudioCapture() override {
        return {};
    }
    QString name() const override {
        return "Discovery fixture";
    }
};
