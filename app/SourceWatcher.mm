#include "SourceWatcher.h"
#include <QPointer>
#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>

struct SourceWatcher::Native {
    __strong id launched = nil, terminated = nil, screens = nil;
    AudioObjectPropertyListenerBlock audio = nil;
};
SourceWatcher::SourceWatcher(QObject *parent) : QObject(parent), native_(std::make_unique<Native>()) {}
SourceWatcher::~SourceWatcher() { stop(); }
void SourceWatcher::start() {
    if (native_->launched) return;
    QPointer<SourceWatcher> owner(this);
    auto notify = ^(NSNotification *) { if (owner) emit owner->changed(); };
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    native_->launched = [center addObserverForName:NSWorkspaceDidLaunchApplicationNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:notify];
    native_->terminated = [center addObserverForName:NSWorkspaceDidTerminateApplicationNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:notify];
    native_->screens = [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidChangeScreenParametersNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:notify];
    native_->audio = ^(UInt32, const AudioObjectPropertyAddress *) { if (owner) emit owner->changed(); };
    for (const auto selector : {kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultInputDevice, kAudioHardwarePropertyDefaultOutputDevice}) {
        AudioObjectPropertyAddress property{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &property, dispatch_get_main_queue(), native_->audio);
    }
}
void SourceWatcher::stop() {
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    if (native_->launched) [center removeObserver:native_->launched];
    if (native_->terminated) [center removeObserver:native_->terminated];
    if (native_->screens) [NSNotificationCenter.defaultCenter removeObserver:native_->screens];
    native_->launched = nil; native_->terminated = nil; native_->screens = nil;
    if (native_->audio) {
        for (const auto selector : {kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultInputDevice, kAudioHardwarePropertyDefaultOutputDevice}) {
            AudioObjectPropertyAddress property{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &property, dispatch_get_main_queue(), native_->audio);
        }
        native_->audio = nil;
    }
}
bool SourceWatcher::nativeEventFilter(const QByteArray &, void *, qintptr *) { return false; }
