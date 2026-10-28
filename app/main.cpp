#include "RecorderController.h"
#include "AppInfo.h"
#include "RegionFixture.h"
#include "DiscoveryFixture.h"
#include "core/RegionGeometry.h"
#include "updates/UpdateManager.h"
#include "QuitGuard.h"
#include <QMouseEvent>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QIcon>
#include <QSettings>
#include <QUuid>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QShortcut>
#include <QTimer>
#include <cmath>
#include <array>
#ifdef Q_OS_WIN
#include <windows.h>
#include <winrt/base.h>
#endif

int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    winrt::init_apartment(winrt::apartment_type::single_threaded);
#endif
    QGuiApplication app(argc, argv);
    app.setOrganizationName(appinfo::name);
    app.setApplicationName(appinfo::name);
    app.setApplicationDisplayName(appinfo::name);
    app.setApplicationVersion(appinfo::version);
    app.setWindowIcon(QIcon("qrc:/assets/app-icon.png"));
    QQuickWindow::setDefaultAlphaBuffer(true);
    // Embedded faces keep the UI consistent without installing a system font.
    for (const auto *file : {":/fonts/D2Coding-Regular.ttf", ":/fonts/D2Coding-Bold.ttf"}) {
        if (QFontDatabase::addApplicationFont(file) < 0) {
            qCritical("Cannot load embedded D2Coding font: %s", file);
            return 1;
        }
    }
    app.setFont(QFont("D2Coding"));
    QQuickStyle::setStyle("Basic");
    const auto arguments = app.arguments();
    if (arguments.contains("--native-probe")) {
        try {
            luma::Settings settings;
            settings.width = arguments.value(2).toInt();
            settings.height = arguments.value(3).toInt();
            settings.fps = arguments.value(4).toInt();
            settings.outputFolder = "probe";
            settings.encoder = "native_gpu";
            settings.validate();
            QTemporaryDir directory;
            if (!directory.isValid())
                throw std::runtime_error("임시 폴더를 만들 수 없습니다.");
            luma::probeNativeVideoEncoder(settings, directory.filePath("probe.mp4"));
            return 0;
        } catch (const std::exception &error) {
            qWarning("%s", error.what());
            return 47;
        }
    }
    const bool testing =
        arguments.contains("--ui-smoke") || arguments.contains("--capture-test") ||
        arguments.contains("--edit-test") || arguments.contains("--workflow-test") ||
        arguments.contains("--encoder-test") || arguments.contains("--settings-test") ||
        arguments.contains("--region-test") || arguments.contains("--discovery-test") ||
        arguments.contains("--window-test") || arguments.contains("--update-check-test");
    if (testing) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope,
            QDir::current().absoluteFilePath("artifacts/test-settings/" +
                                             QUuid::createUuid().toString(QUuid::Id128)));
    }
    if (arguments.contains("--settings-test")) {
        const auto oldFolder = QDir::current().absoluteFilePath("artifacts/이전 저장 위치");
        const auto newFolder = QDir::current().absoluteFilePath("artifacts/새 저장 위치");
        QSettings legacy(QSettings::IniFormat, QSettings::UserScope, "Luma", "Luma Recorder");
        legacy.setValue("container", "mkv");
        legacy.setValue("output", oldFolder);
        legacy.setValue("fps", 55);
        legacy.setValue("expanded", true);
        legacy.sync();
        {
            PreviewProvider preview;
            RecorderController controller(&preview);
            if (controller.settings().value("container") != "mp4" ||
                controller.outputFolder().toLocalFile() != oldFolder ||
                controller.settings().value("fps").toInt() != 55 || !controller.expanded())
                return 19;
            controller.setOutputFolder(QUrl::fromLocalFile(newFolder));
            controller.setSetting("container", "mkv");
            controller.setExpanded(false);
        }
        {
            PreviewProvider preview;
            RecorderController controller(&preview);
            if (controller.settings().value("container") != "mkv" ||
                controller.outputFolder().toLocalFile() != newFolder || controller.expanded() ||
                legacy.value("output") != oldFolder)
                return 20;
        }
        return 0;
    }
    if (arguments.contains("--discovery-test")) {
        PreviewProvider preview;
        auto fixture = std::make_unique<DiscoveryFixture>();
        auto *feed = fixture.get();
        RecorderController controller(&preview, nullptr, std::move(fixture));
        int stage = 0, events = 0, stableEvents = 0;
        QObject::connect(&controller, &RecorderController::sourcesChanged, &app, [&] {
            ++events;
            const auto routes = controller.audio();
            if (stage == 0 && routes.size() == 2) {
                stage = 1;
                controller.selectAudio("mic:mic", true);
                controller.setAudioGain("mic:mic", 0.45);
                stableEvents = events;
                QTimer::singleShot(100, &controller, &RecorderController::refresh);
                QTimer::singleShot(650, &app, [&] {
                    if (events != stableEvents) {
                        app.exit(30);
                        return;
                    }
                    feed->phase = 1;
                });
            } else if (stage == 1 && routes.size() == 3) {
                bool preserved = false;
                for (const auto &route : routes) {
                    const auto item = route.toMap();
                    if (item.value("id") == "mic:mic")
                        preserved = item.value("selected").toBool() &&
                                    qAbs(item.value("gain").toDouble() - 0.45) < 0.01;
                }
                if (!preserved) {
                    app.exit(31);
                    return;
                }
                stage = 2;
                feed->phase = 2;
            } else if (stage == 2 && routes.size() == 2) {
                for (const auto &route : routes)
                    if (route.toMap().value("id") == "mic:mic") {
                        app.exit(32);
                        return;
                    }
                app.exit(feed->calls >= 4 ? 0 : 33);
            }
        });
        QTimer::singleShot(10000, &app, [&] { app.exit(34); });
        return app.exec();
    }
    if (arguments.contains("--tone")) {
        const int index = arguments.indexOf("--tone");
        const double hz = arguments.value(index + 1, "440").toDouble();
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        QByteArray samples(48000 * 4 * 30, 0);
        for (int frame = 0; frame < 48000 * 30; frame++) {
            const auto sample =
                qint16(500 * std::sin(2.0 * 3.141592653589793 * hz * frame / 48000));
            qToLittleEndian(sample, samples.data() + frame * 4);
            qToLittleEndian(sample, samples.data() + frame * 4 + 2);
        }
        QBuffer buffer(&samples);
        buffer.open(QIODevice::ReadOnly);
        QAudioSink sink(format);
        QTimer::singleShot(1500, &app, [&] { sink.start(&buffer); });
        QTimer::singleShot(31500, &app, &QCoreApplication::quit);
        return app.exec();
    }
    auto *preview = new PreviewProvider;
    RecorderController controller(preview);
    UpdateManager updater(!testing);
    if (arguments.contains("--update-check-test")) {
        QObject::connect(&updater, &UpdateManager::changed, &app, [&] {
            if (QStringList{"current", "available", "error"}.contains(updater.state())) {
                QDir().mkpath("artifacts");
                QFile result("artifacts/update-check-result.json");
                result.open(QIODevice::WriteOnly);
                result.write(QJsonDocument(QJsonObject{{"state", updater.state()},
                                                       {"message", updater.message()},
                                                       {"version", updater.version()}})
                                 .toJson());
                app.exit(updater.state() == "error" ? 48 : 0);
            }
        });
        QTimer::singleShot(0, &updater, [&] { updater.check(); });
        QTimer::singleShot(35000, &app, [&] { app.exit(49); });
        return app.exec();
    }
    auto updateAllowed = [&] {
        updater.setAllowed(controller.state() == "idle" && !controller.hasUnsavedRecording() &&
                           !controller.audioTestActive());
    };
    QObject::connect(&controller, &RecorderController::stateChanged, &updater, updateAllowed);
    QObject::connect(&controller, &RecorderController::unsavedChanged, &updater, updateAllowed);
    QObject::connect(&controller, &RecorderController::audioTestChanged, &updater, updateAllowed);
    updateAllowed();
    QObject::connect(&updater, &UpdateManager::changed, &controller, [&] {
        controller.setUpdateBlocked(updater.state() == "preparing" ||
                                    updater.state() == "installing");
    });
    if (arguments.contains("--populated"))
        controller.configureUiFixture();
    if (arguments.contains("--expanded"))
        controller.setExpanded(true);
    if (arguments.contains("--custom")) {
        controller.setSetting("width", 1600);
        controller.setSetting("height", 900);
        controller.setSetting("fps", 55);
        controller.setSetting("delay", 7);
    }
    // QML must be destroyed before its context object to avoid teardown bindings
    // evaluating against a deleted controller.
    QQmlApplicationEngine engine;
    engine.addImageProvider("preview", preview);
    engine.rootContext()->setContextProperty("recorder", &controller);
    engine.rootContext()->setContextProperty("updater", &updater);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Luma", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    QuitGuard quitGuard(qobject_cast<QWindow *>(engine.rootObjects().first()), [&] {
        return controller.state() != "idle" || controller.hasUnsavedRecording() ||
               updater.state() == "preparing";
    });
    app.installEventFilter(&quitGuard);
    if (arguments.contains("--window-test")) {
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        auto fail = [&app](const char *error) {
            qWarning("%s", error);
            app.exit(45);
        };
        root->setProperty("previewScreenWidth", 1600);
        root->setProperty("previewScreenHeight", 1000);
        QTimer::singleShot(150, &app, [&, root, window] {
            window->setGeometry(120, 80, 760, 680);
            QTimer::singleShot(150, &app,
                               [root] { QMetaObject::invokeMethod(root, "toggleMaximized"); });
        });
        QTimer::singleShot(600, &app,
                           [root] { QMetaObject::invokeMethod(root, "toggleMaximized"); });
        QTimer::singleShot(850, &app, [&, root, window, fail] {
            if (window->visibility() != QWindow::Windowed || window->size() != QSize(760, 680)) {
                fail("Maximize button did not restore the previous window size");
                return;
            }
            QMetaObject::invokeMethod(root, "toggleMaximized");
            QTimer::singleShot(200, &app, [root] {
                QMetaObject::invokeMethod(root, "moveWindow", Q_ARG(QVariant, QPointF(300, 20)));
            });
        });
        QTimer::singleShot(1300, &app, [&, root, window, fail] {
            if (window->visibility() != QWindow::Windowed || window->size() != QSize(760, 680)) {
                fail("Dragging a maximized titlebar did not restore the previous size");
                return;
            }
            root->setProperty("optimized", true);
        });
        QTimer::singleShot(1600, &app, [&, root, window, fail] {
            if (window->size() != QSize(400, 104)) {
                fail("Compact mode has wrong bounds");
                return;
            }
            int moves = 0;
            // Inspect hit testing without invoking native movement: offscreen
            // startSystemMove returns false, but the real QML MouseArea receives input.
            auto *dragArea = root->findChild<QObject *>("compactDrag");
            for (const QPointF point : {QPointF(3, 3), QPointF(200, 3), QPointF(397, 101)}) {
                QMouseEvent press(QEvent::MouseButtonPress, point,
                                  window->mapToGlobal(point.toPoint()), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                if (dragArea && dragArea->property("pressed").toBool())
                    ++moves;
                QMouseEvent release(QEvent::MouseButtonRelease, point,
                                    window->mapToGlobal(point.toPoint()), Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
            }
            if (moves != 3) {
                fail("Compact background margins do not receive dragging input");
                return;
            }
            root->setProperty("optimized", false);
            QTimer::singleShot(150, &app, [&, window, fail] {
                if (window->size() != QSize(760, 680))
                    fail("Compact restore lost the normal window bounds");
                else
                    app.exit(0);
            });
        });
        QTimer::singleShot(6000, &app, [&] { app.exit(46); });
    }
    if (arguments.contains("--region-test")) {
#ifdef Q_OS_WIN
        QDir().mkpath("artifacts");
        QFile::remove("artifacts/region-result.json");
        QFile::remove("artifacts/region-error.txt");
        auto fixture = std::make_shared<RegionFixture>();
        fixture->show();
        auto *frame = qobject_cast<QQuickWindow *>(
            engine.rootObjects().first()->findChild<QObject *>("captureRegionFrame"));
        auto fail = [&app](QString error) {
            QFile file("artifacts/region-error.txt");
            file.open(QIODevice::WriteOnly);
            file.write(error.toUtf8());
            app.exit(21);
        };
        auto drag = [frame](double targetX, double targetY) {
            const auto start = QPointF(frame->x() + 12, frame->y() + 10);
            const auto destination =
                start + QPointF(targetX - (frame->x() + 4),
                                targetY - (frame->y() + frame->property("contentTop").toInt()));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(12, 10), start, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(frame, &press);
            QMouseEvent move(QEvent::MouseMove, destination - QPointF(frame->position()),
                             destination, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(frame, &move);
            QCoreApplication::processEvents();
            QMouseEvent release(QEvent::MouseButtonRelease,
                                destination - QPointF(frame->position()), destination,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(frame, &release);
        };
        auto started = std::make_shared<bool>(false);
        auto readbackBaseline = std::make_shared<quint64>(0);
        auto compactReadbacks = std::make_shared<quint64>(0);
        const auto uiThread = GetCurrentThreadId();
        auto hotkey = [uiThread](int id) { PostThreadMessage(uiThread, WM_HOTKEY, WPARAM(id), 0); };
        QObject::connect(
            &controller, &RecorderController::stateChanged, &app,
            [&, fixture, frame, fail, drag, started, hotkey, readbackBaseline, compactReadbacks] {
                if (controller.state() != "recording" || *started)
                    return;
                *started = true;
                QTimer::singleShot(450, &app, [frame, fail] {
                    if (!frame || !frame->isVisible() ||
                        frame->mask().contains(QPoint(frame->width() / 2, frame->height() / 2))) {
                        fail("Region frame does not have a click-through center");
                        return;
                    }
                    DWORD affinity = 0;
                    if (!GetWindowDisplayAffinity(reinterpret_cast<HWND>(frame->winId()),
                                                  &affinity) ||
                        affinity != WDA_EXCLUDEFROMCAPTURE) {
                        fail("Region frame is not excluded from Windows capture");
                        return;
                    }
                    frame->grabWindow().save("artifacts/region-frame.png");
                });
                QTimer::singleShot(450, &app, [&, fail, readbackBaseline] {
                    *readbackBaseline = controller.captureReadbacks();
                    auto *root = engine.rootObjects().first();
                    auto *help = root->findChild<QObject *>("helpDialog");
                    auto *window = qobject_cast<QQuickWindow *>(root);
                    if ((help && help->property("visible").toBool()) || !controller.optimized() ||
                        window->width() != 400 || window->height() != 104)
                        fail("Shortcut recording left a help popup open or failed to enter compact "
                             "mode");
                });
                QTimer::singleShot(800, &app, [&, fixture, drag, fail] {
                    const auto x = fixture->x() + fixture->width() - 192;
                    drag(x, fixture->y() + 32);
                    if (qAbs(controller.regionInfo().value("x").toDouble() - x) > 1)
                        fail("Dragging the frame during recording did not translate the crop");
                });
                QTimer::singleShot(1200, &app, [hotkey] { hotkey(GlobalHotkeys::PauseId); });
                QTimer::singleShot(1400, &app, [&, fixture, drag, fail] {
                    drag(fixture->x() + 32, fixture->y() + 32);
                    if (controller.state() != "paused" ||
                        qAbs(controller.regionInfo().value("x").toDouble() - fixture->x() - 32) > 1)
                        fail("Dragging while paused did not translate the crop");
                });
                QTimer::singleShot(1700, &app, [hotkey] { hotkey(GlobalHotkeys::PauseId); });
                QTimer::singleShot(2450, &app, [&, fail, readbackBaseline, compactReadbacks] {
                    *compactReadbacks = controller.captureReadbacks() - *readbackBaseline;
                    if (arguments.contains("--native-gpu") &&
                        controller.captureReadbacks() != *readbackBaseline) {
                        fail("Native compact recording performed CPU pixel readback");
                        return;
                    }
                    if (controller.estimatedSize() == luma::recordingSizeText(0)) {
                        fail("Live file estimate did not update from the encoded video");
                        return;
                    }
                    auto *root = engine.rootObjects().first();
                    auto *size = root->findChild<QQuickItem *>("compactSize");
                    auto *pause = root->findChild<QQuickItem *>("compactPause");
                    if (!size || !size->isVisible() ||
                        size->mapToScene(QPointF(size->width(), 0)).x() >
                            pause->mapToScene(QPointF()).x()) {
                        fail("Compact size indicator overlaps the recording buttons");
                        return;
                    }
                    qobject_cast<QQuickWindow *>(root)->grabWindow().save(
                        "artifacts/recording-compact.png");
                });
                QTimer::singleShot(2700, &app, [hotkey] { hotkey(GlobalHotkeys::RecordId); });
            });
        auto saveFlowStarted = std::make_shared<bool>(false);
        QObject::connect(
            &controller, &RecorderController::lastFileChanged, &app, [&, saveFlowStarted, fail] {
                if (*saveFlowStarted)
                    return;
                *saveFlowStarted = true;
                QTimer::singleShot(350, &app, [&, fail] {
                    auto *root = engine.rootObjects().first();
                    auto *dialog = root->findChild<QObject *>("recordingSave");
                    auto *editor = root->findChild<QObject *>("editor");
                    auto *timer = root->findChild<QObject *>("recordingTimer");
                    if (!dialog->property("opened").toBool() ||
                        editor->property("opened").toBool() ||
                        timer->property("visible").toBool() || controller.elapsed() != "00:00:00") {
                        fail("Stop must show saving first and hide/reset the idle timer");
                        return;
                    }
                    if (!controller.hasUnsavedRecording() ||
                        dialog->property("folder").toUrl() != controller.outputFolder() ||
                        !QDateTime::fromString(
                             QFileInfo(controller.lastFile().toLocalFile()).completeBaseName(),
                             "yyyy-MM-dd_HH-mm-ss-zzz")
                             .isValid()) {
                        fail("Saving must default to the chosen folder and recording start time");
                        return;
                    }
                    auto *window = qobject_cast<QQuickWindow *>(root);
                    if (window->close()) {
                        fail("Closing with an unsaved recording was not blocked");
                        return;
                    }
                    auto *exitDialog = root->findChild<QObject *>("unsavedExit");
                    if (!exitDialog->property("visible").toBool()) {
                        fail("Unsaved close did not ask for confirmation");
                        return;
                    }
                    window->grabWindow().save("artifacts/unsaved-exit.png");
                    QMetaObject::invokeMethod(root->findChild<QObject *>("cancelUnsavedExit"),
                                              "clicked");
                    if (!controller.hasUnsavedRecording() || !window->isVisible()) {
                        fail("Canceling exit lost the unsaved recording");
                        return;
                    }
                    QEvent quitEvent(QEvent::Quit);
                    QCoreApplication::sendEvent(&app, &quitEvent);
                    QCoreApplication::processEvents();
                    if (!exitDialog->property("visible").toBool() ||
                        !controller.hasUnsavedRecording()) {
                        fail("Application Quit bypassed the unsaved confirmation");
                        return;
                    }
                    QMetaObject::invokeMethod(root->findChild<QObject *>("cancelUnsavedExit"),
                                              "clicked");
                    const auto suggestedName =
                        QFileInfo(controller.lastFile().toLocalFile()).completeBaseName() +
                        "_saved.mp4";
                    auto *nameField = root->findChild<QObject *>("recordingName");
                    nameField->setProperty("text", suggestedName);
                    window->close();
                    QMetaObject::invokeMethod(root->findChild<QObject *>("saveBeforeExit"),
                                              "clicked");
                    if (!root->property("closeAfterSave").toBool() || !window->isVisible() ||
                        nameField->property("text").toString() != suggestedName) {
                        fail("Save-before-exit must retain the chosen name and wait for saving");
                        return;
                    }
                    nameField->setProperty("text", "invalid/name.mp4");
                    QMetaObject::invokeMethod(root->findChild<QObject *>("saveRecording"),
                                              "clicked");
                    if (root->property("closeAfterSave").toBool() ||
                        !controller.hasUnsavedRecording() || controller.message().isEmpty()) {
                        fail(
                            "Failed save-before-exit must retain the recording and cancel closing");
                        return;
                    }
                    nameField->setProperty("text", suggestedName);
                    controller.clearMessage();
                    root->setProperty("closeAfterSave", true);
                    QMetaObject::invokeMethod(dialog, "reject");
                    if (root->property("closeAfterSave").toBool() ||
                        !controller.hasUnsavedRecording()) {
                        fail("Canceling save-before-exit must cancel closing and retain the clip");
                        return;
                    }
                    controller.toggleRecording();
                    if (controller.state() != "idle" || !controller.hasUnsavedRecording()) {
                        fail("Starting another capture must not replace the unsaved recording");
                        return;
                    }
                    qobject_cast<QQuickWindow *>(root)->grabWindow().save(
                        "artifacts/recording-save.png");
                    QMetaObject::invokeMethod(root->findChild<QObject *>("editBeforeSave"),
                                              "clicked");
                    QTimer::singleShot(350, &app, [&, root, editor, dialog, fail] {
                        if (!editor->property("opened").toBool() ||
                            dialog->property("opened").toBool()) {
                            fail("The save dialog did not open the optional editor");
                            return;
                        }
                        QMetaObject::invokeMethod(editor, "close");
                        QTimer::singleShot(200, &app, [&, root, dialog, fail] {
                            if (!dialog->property("opened").toBool()) {
                                fail("Closing the editor did not return to saving");
                                return;
                            }
                            QMetaObject::invokeMethod(root->findChild<QObject *>("saveRecording"),
                                                      "clicked");
                        });
                    });
                });
            });
        QObject::connect(
            &controller, &RecorderController::recordingSaved, &app, [&, compactReadbacks] {
                if (controller.hasUnsavedRecording() ||
                    QFileInfo(controller.lastFile().toLocalFile()).absolutePath() !=
                        QDir(controller.outputFolder().toLocalFile()).absolutePath()) {
                    fail("Saving must clear pending state and write directly into the chosen "
                         "folder");
                    return;
                }
                QFile out("artifacts/region-result.json");
                out.open(QIODevice::WriteOnly);
                out.write(QJsonDocument(
                              QJsonObject{{"file", controller.lastFile().toLocalFile()},
                                          {"width", controller.settings().value("width").toInt()},
                                          {"height", controller.settings().value("height").toInt()},
                                          {"dragVerified", true},
                                          {"saveFlowVerified", true},
                                          {"idleTimerVerified", true},
                                          {"unsavedExitVerified", true},
                                          {"flatOutputVerified", true},
                                          {"nativeGpu", arguments.contains("--native-gpu")},
                                          {"compactCpuReadbacks", double(*compactReadbacks)},
                                          {"hotkeysVerified", controller.globalHotkeysAvailable()}})
                              .toJson());
                app.exit(0);
            });
        QTimer::singleShot(800, &app, [&, fixture, fail, hotkey] {
            if (!controller.globalHotkeysAvailable()) {
                fail(controller.hotkeyStatus());
                return;
            }
            const auto scale = fixture->devicePixelRatio();
            const QSize pixels(qRound(fixture->width() * scale), qRound(fixture->height() * scale));
            luma::Source source{QString::number(quintptr(fixture->winId())),
                                "Silent region fixture",
                                luma::SourceKind::Window,
                                QRect(QPoint(), pixels),
                                quint32(app.applicationPid()),
                                fixture->geometry()};
            const auto crop =
                luma::regionFromLogical(QRectF(fixture->x() + 32, fixture->y() + 32, 160, 120),
                                        fixture->geometry(), pixels);
            controller.configureRegionTest(source, crop);
            if (arguments.contains("--native-gpu"))
                controller.setSetting("encoder", "native_gpu");
            QMetaObject::invokeMethod(
                engine.rootObjects().first()->findChild<QObject *>("helpDialog"), "open");
            if (controller.settings().value("width").toInt() != crop.width() ||
                !controller.regionInfo().value("active").toBool()) {
                fail("Selected pixel dimensions were not used as output dimensions");
                return;
            }
            QTimer::singleShot(400, &app, [hotkey] { hotkey(GlobalHotkeys::RecordId); });
        });
        QTimer::singleShot(25000, &app, [fail] { fail("Silent region test timed out"); });
#else
        return 77;
#endif
    }
    if (arguments.contains("--encoder-test")) {
        QDir().mkpath("artifacts");
        QFile::remove("artifacts/preflight-trace.log");
        controller.setSetting("encoder", "libx264");
        if (arguments.contains("--encoder"))
            controller.setSetting("encoder", arguments.value(arguments.indexOf("--encoder") + 1));
        controller.setSetting("width", 640);
        controller.setSetting("height", 360);
        controller.setSetting("fps", 30);
        QObject::connect(&controller, &RecorderController::encoderChanged, &app, [&] {
            QFile trace("artifacts/preflight-trace.log");
            if (trace.open(QIODevice::WriteOnly | QIODevice::Append))
                trace.write(("observer " + controller.encoderStatus() + "\n").toUtf8());
            if (controller.encoderStatus() == "passed") {
                QDir().mkpath("artifacts");
                QFile out("artifacts/encoder-result.json");
                out.open(QIODevice::WriteOnly);
                out.write(QJsonDocument(
                              QJsonObject{{"passed", true}, {"detail", controller.encoderDetail()}})
                              .toJson());
                app.exit(arguments.contains("--expect-failure") ? 10 : 0);
            } else if (controller.encoderStatus() == "failed") {
                QFile out("artifacts/encoder-failure-result.json");
                out.open(QIODevice::WriteOnly);
                out.write(QJsonDocument(QJsonObject{{"failedAsExpected",
                                                     arguments.contains("--expect-failure")},
                                                    {"detail", controller.encoderDetail()},
                                                    {"idle", controller.state() == "idle"}})
                              .toJson());
                app.exit(arguments.contains("--expect-failure") ? 0 : 10);
            }
        });
        QTimer::singleShot(0, &controller, &RecorderController::checkEncoder);
        QTimer::singleShot(20000, &app, [&] {
            QFile file("artifacts/encoder-timeout.json");
            file.open(QIODevice::WriteOnly);
            file.write(QJsonDocument(QJsonObject{{"state", controller.state()},
                                                 {"status", controller.encoderStatus()},
                                                 {"detail", controller.encoderDetail()}})
                           .toJson());
            QCoreApplication::exit(11);
        });
    }
    if (arguments.contains("--workflow-test")) {
#ifdef Q_OS_WIN
        QDir().mkpath("artifacts");
        QFile::remove("artifacts/workflow-result.json");
        QFile::remove("artifacts/workflow-error.txt");
        auto *root = engine.rootObjects().first();
        auto *window = qobject_cast<QQuickWindow *>(root);
        ShowWindow(reinterpret_cast<HWND>(window->winId()), SW_SHOWNOACTIVATE);
        auto *animation = new QTimer(&app);
        animation->setInterval(50);
        QObject::connect(animation, &QTimer::timeout, &app, [root] {
            root->setProperty("captureTestTick", root->property("captureTestTick").toInt() + 1);
        });
        animation->start();
        auto tones = std::make_shared<std::array<QProcess, 2>>();
        (*tones)[0].start(app.applicationFilePath(), {"--tone", "440"});
        (*tones)[1].start(app.applicationFilePath(), {"--tone", "880"});
        (*tones)[0].waitForStarted();
        (*tones)[1].waitForStarted();
        auto recordingStarted = std::make_shared<bool>(false);
        auto auditionFinished = std::make_shared<bool>(false);
        auto pauseObserved = std::make_shared<bool>(false);
        const auto uiThread = GetCurrentThreadId();
        auto fail = [&app](QString error) {
            QFile out("artifacts/workflow-error.txt");
            out.open(QIODevice::WriteOnly);
            out.write(error.toUtf8());
            app.exit(12);
        };
        QObject::connect(
            &controller, &RecorderController::audioTestChanged, &app,
            [&, auditionFinished, tones, fail, uiThread] {
                if (!controller.audioTestActive() && !controller.audioSample().isEmpty() &&
                    !*auditionFinished) {
                    *auditionFinished = true;
                    QFile::remove("artifacts/audition.wav");
                    QFile::copy(controller.audioSample().toLocalFile(), "artifacts/audition.wav");
                    if (controller.audioTestStatus().contains("소리가 감지되지")) {
                        fail(controller.audioTestStatus());
                        return;
                    }
                    QTimer::singleShot(350, &app, [&, uiThread, fail] {
                        auto *player =
                            engine.rootObjects().first()->findChild<QObject *>("audioCheckPlayer");
                        auto *button =
                            engine.rootObjects().first()->findChild<QObject *>("samplePlayButton");
                        if (!player || player->property("duration").toLongLong() < 4900 ||
                            !button) {
                            fail("Audition player did not load the five-second sample");
                            return;
                        }
                        QMetaObject::invokeMethod(button, "clicked");
                        QTimer::singleShot(250, &app, [player, button, uiThread, fail] {
                            if (player->property("playbackState").toInt() !=
                                QMediaPlayer::PlayingState) {
                                fail("Audition playback did not start");
                                return;
                            }
                            QMetaObject::invokeMethod(button, "clicked");
                            PostThreadMessage(uiThread, WM_HOTKEY, GlobalHotkeys::RecordId, 0);
                        });
                    });
                }
            });
        QObject::connect(
            &controller, &RecorderController::stateChanged, &app,
            [&, recordingStarted, pauseObserved, tones, fail, uiThread] {
                if (controller.state() == "paused")
                    *pauseObserved = true;
                if (controller.state() == "recording" && !*recordingStarted) {
                    *recordingStarted = true;
                    QTimer::singleShot(450, &app, [&, fail] {
                        auto *root = engine.rootObjects().first();
                        auto *window = qobject_cast<QQuickWindow *>(root);
                        auto *menu = root->findChild<QObject *>("mainMenu");
                        auto *content = root->findChild<QObject *>("normalContent");
                        if (!controller.optimized() || window->width() != 400 ||
                            window->height() != 104 || !menu ||
                            menu->property("visible").toBool() || !content ||
                            content->property("visible").toBool()) {
                            fail(QString("Compact view: optimized=%1 size=%2x%3 menu=%4 content=%5")
                                     .arg(controller.optimized())
                                     .arg(window->width())
                                     .arg(window->height())
                                     .arg(menu && menu->property("visible").toBool())
                                     .arg(content && content->property("visible").toBool()));
                            return;
                        }
                        window->grabWindow().save("artifacts/ui-compact.png");
                        const auto settings = controller.settings();
                        QMetaObject::invokeMethod(root->findChild<QObject *>("compactRestore"),
                                                  "clicked");
                        controller.setExpanded(true);
                        if (controller.optimized() || !menu->property("visible").toBool() ||
                            controller.settings() != settings) {
                            fail("Restoring the mixer changed recording settings or menu state");
                            return;
                        }
                        controller.setOptimized(true);
                    });
                    QTimer::singleShot(1800, &app, [&controller, tones] {
                        controller.setAudioGain("app:" + QString::number((*tones)[0].processId()),
                                                0.5);
                    });
                    QTimer::singleShot(2000, &app, [&] {
                        QMetaObject::invokeMethod(
                            engine.rootObjects().first()->findChild<QObject *>("compactPause"),
                            "clicked");
                    });
                    if (!arguments.contains("--stop-paused"))
                        QTimer::singleShot(3000, &app, [uiThread] {
                            PostThreadMessage(uiThread, WM_HOTKEY, GlobalHotkeys::PauseId, 0);
                        });
                    QTimer::singleShot(3800, &app, [&controller, tones] {
                        controller.setAudioMuted("app:" + QString::number((*tones)[1].processId()),
                                                 true);
                    });
                    QTimer::singleShot(4800, &app, [&controller, tones] {
                        controller.setAudioMuted("app:" + QString::number((*tones)[1].processId()),
                                                 false);
                    });
                    QTimer::singleShot(
                        arguments.contains("--stop-paused") ? 2800 : 6000, &app, [&] {
                            QMetaObject::invokeMethod(
                                engine.rootObjects().first()->findChild<QObject *>("compactStop"),
                                "clicked");
                        });
                }
            });
        QObject::connect(
            &controller, &RecorderController::lastFileChanged, &app,
            [&, tones, pauseObserved, fail] {
                const auto file = controller.lastFile().toLocalFile();
                if (file.isEmpty())
                    return;
                auto *menu = engine.rootObjects().first()->findChild<QObject *>("mainMenu");
                if (controller.optimized() || !controller.expanded() || !menu ||
                    !menu->property("visible").toBool() || !file.endsWith(".mp4") ||
                    !controller.hasUnsavedRecording()) {
                    fail("Final MP4 folder or restored expanded mode/menu is incorrect");
                    return;
                }
                QFile out("artifacts/workflow-result.json");
                out.open(QIODevice::WriteOnly);
                out.write(
                    QJsonDocument(
                        QJsonObject{{"file", file},
                                    {"warning", QString()},
                                    {"pauseObserved", *pauseObserved},
                                    {"compactModeVerified", true},
                                    {"restoredExpandedMode", true},
                                    {"encoderPassed", controller.encoderStatus() == "passed"},
                                    {"hotkeysRegistered", controller.globalHotkeysAvailable()}})
                        .toJson());
                for (auto &process : *tones) {
                    process.kill();
                    process.waitForFinished();
                }
                app.exit(*pauseObserved ? 0 : 13);
            });
        QTimer::singleShot(2100, &app, [&, tones, fail, window] {
            if (!controller.globalHotkeysAvailable()) {
                fail(controller.hotkeyStatus());
                return;
            }
            const auto style = GetWindowLongPtr(reinterpret_cast<HWND>(window->winId()), GWL_STYLE);
            if ((style & WS_CAPTION) == WS_CAPTION) {
                fail("Windows native caption was not removed");
                return;
            }
            luma::Source source{QString::number(quintptr(window->winId())),
                                "FARecorder workflow fixture", luma::SourceKind::Window,
                                QRect(0, 0, 780, 780), quint32(QCoreApplication::applicationPid())};
            QList<luma::AudioSource> audio{
                {QString::number((*tones)[0].processId()), "Tone440", "test",
                 luma::AudioKind::Application, quint32((*tones)[0].processId())},
                {QString::number((*tones)[1].processId()), "Tone880", "test",
                 luma::AudioKind::Application, quint32((*tones)[1].processId())}};
            controller.configureWorkflowTest(source, audio);
            controller.setOutputFolder(
                QUrl::fromLocalFile(QDir::current().absoluteFilePath("artifacts/selected-output")));
            const auto fingerprint = controller.settings();
            controller.setExpanded(true);
            controller.setExpanded(false);
            if (controller.settings() != fingerprint || controller.audio().size() != 2) {
                fail("Mode switch changed capture settings");
                return;
            }
            controller.toggleAudioTest();
        });
        QTimer::singleShot(40000, &app, [fail] { fail("Workflow test timed out"); });
#else
        return 77;
#endif
    }
    if (arguments.contains("--capture-test")) {
#ifdef Q_OS_WIN
        QDir().mkpath("artifacts");
        QFile::remove("artifacts/capture-error.txt");
        QFile::remove("artifacts/capture-result.json");
        QFile::remove("artifacts/capture-events.log");
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            QFile out("artifacts/capture-events.log");
            if (out.open(QIODevice::WriteOnly | QIODevice::Append))
                out.write((QString::number(luma::clockNs() / 1e9, 'f', 3) + " " + message + "\n")
                              .toUtf8());
        });
        auto *animation = new QTimer(&app);
        animation->setInterval(50);
        QObject::connect(animation, &QTimer::timeout, &app, [&] {
            auto *root = engine.rootObjects().first();
            root->setProperty("captureTestTick", root->property("captureTestTick").toInt() + 1);
        });
        animation->start();
        auto platform = std::shared_ptr<luma::Platform>(luma::createPlatform());
        auto capture = std::shared_ptr<luma::VideoCapture>(platform->createVideoCapture());
        auto session = std::make_shared<luma::RecordingSession>();
        auto tone1 = std::make_shared<QProcess>();
        auto tone2 = std::make_shared<QProcess>();
        tone1->start(app.applicationFilePath(), {"--tone", "440"});
        tone2->start(app.applicationFilePath(), {"--tone", "880"});
        tone1->waitForStarted();
        tone2->waitForStarted();
        auto mix1 = std::make_shared<luma::AudioMix>();
        auto mix2 = std::make_shared<luma::AudioMix>();
        const auto testOrigin = luma::clockNs();
        QObject::connect(session.get(), &luma::RecordingSession::started, &app,
                         [testOrigin](QString) {
                             qWarning("TEST started %.3f", (luma::clockNs() - testOrigin) / 1e9);
                         });
        QObject::connect(session.get(), &luma::RecordingSession::finalizing, &app, [testOrigin] {
            qWarning("TEST finalizing %.3f", (luma::clockNs() - testOrigin) / 1e9);
        });
        QObject::connect(
            session.get(), &luma::RecordingSession::finished, &app,
            [&, platform, capture, session, tone1, tone2](QString file, QString warning) {
                tone1->kill();
                tone2->kill();
                tone1->waitForFinished();
                tone2->waitForFinished();
                capture->stop();
                QFile result("artifacts/capture-result.json");
                result.open(QIODevice::WriteOnly);
                result.write(QJsonDocument(QJsonObject{{"file", file},
                                                       {"warning", warning},
                                                       {"frames", double(session->frames())}})
                                 .toJson());
                app.exit(warning.isEmpty() ? 0 : 2);
            });
        QObject::connect(session.get(), &luma::RecordingSession::failed, &app,
                         [&, platform, capture, session, tone1, tone2](QString error, QString) {
                             QFile result("artifacts/capture-error.txt");
                             result.open(QIODevice::WriteOnly);
                             result.write(error.toUtf8());
                             tone1->kill();
                             tone2->kill();
                             app.exit(3);
                         });
        QTimer::singleShot(600, &app, [&, platform, capture, session, tone1, tone2, mix1, mix2] {
            try {
                auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
                // WGC cannot capture a hidden HWND. Show only our own test window,
                // without activating it or changing the user's foreground app.
                ShowWindow(reinterpret_cast<HWND>(window->winId()), SW_SHOWNOACTIVATE);
                luma::Source source{QString::number(quintptr(window->winId())), "Luma self-test",
                                    luma::SourceKind::Window, QRect(0, 0, 1380, 920)};
                capture->start(source, {}, true, [&](QString error) {
                    QFile out("artifacts/capture-error.txt");
                    out.open(QIODevice::WriteOnly);
                    out.write(error.toUtf8());
                });
                QTimer::singleShot(
                    800, &app, [&, platform, capture, session, tone1, tone2, mix1, mix2] {
                        luma::Settings settings;
                        settings.width = 640;
                        settings.height = 360;
                        settings.fps = 30;
                        settings.delaySeconds = 0;
                        settings.outputFolder =
                            QDir::current().absoluteFilePath("artifacts/recordings");
                        settings.recoveryFolder =
                            QDir::current().absoluteFilePath("artifacts/capture-work");
                        QList<luma::AudioSource> audio{
                            {QString::number(tone1->processId()), "Tone440", "test",
                             luma::AudioKind::Application, quint32(tone1->processId()), mix1},
                            {QString::number(tone2->processId()), "Tone880", "test",
                             luma::AudioKind::Application, quint32(tone2->processId()), mix2}};
                        session->start(platform.get(), capture.get(), settings, audio);
                        QTimer::singleShot(1800, &app, [mix1] {
                            qWarning("TEST gain");
                            mix1->gain = 0.5f;
                        });
                        QTimer::singleShot(2800, &app, [mix2] {
                            qWarning("TEST mute");
                            mix2->muted = true;
                        });
                        QTimer::singleShot(3800, &app, [mix2] {
                            qWarning("TEST unmute");
                            mix2->muted = false;
                        });
                        QTimer::singleShot(5000, &app, [session] {
                            qWarning("TEST request stop");
                            session->requestStop();
                        });
                    });
            } catch (const std::exception &ex) {
                QFile result("artifacts/capture-error.txt");
                result.open(QIODevice::WriteOnly);
                result.write(ex.what());
                app.exit(4);
            }
        });
        QTimer::singleShot(30000, &app, [] { QCoreApplication::exit(5); });
#else
        return 6;
#endif
    }
    if (arguments.contains("--edit-test")) {
        QFile::remove("artifacts/edit-error.txt");
        QFile::remove("artifacts/edit-result.json");
        const auto input = arguments.value(arguments.indexOf("--edit-test") + 1);
        controller.openRecording(QUrl::fromLocalFile(input));
        QObject::connect(&controller, &RecorderController::trimFinished, &app, [&](QUrl file) {
            QFile result("artifacts/edit-result.json");
            result.open(QIODevice::WriteOnly);
            result.write(QJsonDocument(QJsonObject{{"file", file.toLocalFile()}}).toJson());
            app.exit(0);
        });
        QTimer::singleShot(2500, &app, [&] {
            auto *player = engine.rootObjects().first()->findChild<QObject *>("editorPlayer");
            if (!player || player->property("duration").toLongLong() < 2500) {
                QFile out("artifacts/edit-error.txt");
                out.open(QIODevice::WriteOnly);
                out.write("Media player did not load the test video");
                app.exit(7);
                return;
            }
            if (player->property("playbackState").toInt() != QMediaPlayer::PausedState) {
                QFile out("artifacts/edit-error.txt");
                out.open(QIODevice::WriteOnly);
                out.write("Editor preview did not pause after showing the first frame");
                app.exit(9);
                return;
            }
            auto *root = engine.rootObjects().first();
            const double duration = player->property("duration").toDouble();
            root->findChild<QObject *>("trimIn")->setProperty("value", 500.0 / duration);
            root->findChild<QObject *>("trimOut")->setProperty("value", 2500.0 / duration);
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            auto screenshot = window->grabWindow();
            if (!screenshot.isNull())
                screenshot.save("artifacts/editor-preview.png");
            auto *choice = root->findChild<QObject *>("saveChoice");
            QMetaObject::invokeMethod(choice, "open");
            QTimer::singleShot(200, &app, [&, root, window] {
                const auto screenshot = window->grabWindow();
                if (!screenshot.isNull())
                    screenshot.save("artifacts/save-choice-preview.png");
                auto *button = root->findChild<QObject *>(
                    arguments.contains("--overwrite") ? "saveOverwrite" : "saveNew");
                QMetaObject::invokeMethod(button, "clicked");
            });
        });
        QTimer::singleShot(20000, &app, [] { QCoreApplication::exit(8); });
    }
    if (app.arguments().contains("--ui-smoke")) {
        engine.rootObjects().first()->setProperty("previewScreenWidth", 1600);
        engine.rootObjects().first()->setProperty("previewScreenHeight", 1000);
        if (arguments.contains("--small")) {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            window->setWidth(720);
            window->setHeight(640);
        }
        QTimer::singleShot(1000, &app, [&] {
            auto *root = engine.rootObjects().first();
            auto *menu = root->findChild<QObject *>("mainMenu");
            auto *folderButton = root->findChild<QObject *>("chooseFolderButton");
            if (controller.settings().value("container") != "mp4" ||
                controller.appInfo().value("name") != "FARecorder" ||
                controller.appInfo().value("version") != QCoreApplication::applicationVersion() ||
                !menu || !menu->property("visible").toBool() || !folderButton ||
                !folderButton->property("visible").toBool()) {
                qWarning() << "UI metadata/controls:" << controller.settings().value("container")
                           << controller.appInfo() << QCoreApplication::applicationVersion()
                           << "menu" << (menu && menu->property("visible").toBool()) << "folder"
                           << (folderButton && folderButton->property("visible").toBool());
                app.exit(17);
                return;
            }
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (arguments.contains("--small")) {
                auto *folder = qobject_cast<QQuickItem *>(folderButton);
                auto *record = root->findChild<QQuickItem *>("recordButton");
                if (!folder || !record ||
                    folder->mapToScene(QPointF(0, folder->height())).y() > window->height() ||
                    record->mapToScene(QPointF(0, record->height())).y() > window->height()) {
                    app.exit(42);
                    return;
                }
            }
            const auto image = window->grabWindow();
            if (!image.isNull()) {
                QDir().mkpath("artifacts");
                image.save(arguments.contains("--small") ? "artifacts/ui-small.png"
                           : arguments.contains("--populated")
                               ? (arguments.contains("--expanded")
                                      ? "artifacts/ui-audio-expanded.png"
                                      : "artifacts/ui-audio.png")
                           : arguments.contains("--custom")   ? "artifacts/ui-custom.png"
                           : arguments.contains("--expanded") ? "artifacts/ui-expanded.png"
                                                              : "artifacts/ui-preview.png");
            }
            if (arguments.contains("--populated")) {
                auto *mic = root->findChild<QObject *>("microphonePicker");
                auto *computer = root->findChild<QObject *>("computerPicker");
                if (!mic || !computer || !mic->property("text").toString().contains("USB Audio") ||
                    !computer->property("text").toString().contains("Discord")) {
                    app.exit(35);
                    return;
                }
                controller.setAudioMuted("mic:fixture-mic", true);
                if (!mic->property("text").toString().contains("USB Audio")) {
                    app.exit(36);
                    return;
                }
                controller.setAudioMuted("mic:fixture-mic", false);
                auto *micChoices = root->findChild<QObject *>("microphoneChoices");
                auto *computerChoices = root->findChild<QObject *>("computerChoices");
                QMetaObject::invokeMethod(micChoices, "open");
                QMetaObject::invokeMethod(computerChoices, "open");
                QCoreApplication::processEvents();
                std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual =
                    [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
                    if (parent->objectName() == name)
                        return parent;
                    for (auto *child : parent->childItems())
                        if (auto *found = findVisual(child, name))
                            return found;
                    return nullptr;
                };
                // Repeater delegates belong to the visual tree, not necessarily
                // to the root's QObject ownership tree.
                auto *headset =
                    findVisual(window->contentItem(), "source-option-mic:fixture-headset");
                auto *speaker =
                    findVisual(window->contentItem(), "source-option-system:fixture-speaker");
                auto *discord =
                    findVisual(window->contentItem(), "source-option-app:fixture-discord");
                if (!headset || !speaker || !discord) {
                    qWarning() << "Source choices were not created" << headset << speaker
                               << discord;
                    app.exit(43);
                    return;
                }
                headset->setProperty("checked", true);
                QMetaObject::invokeMethod(headset, "toggled");
                if (!mic->property("text").toString().contains("외 1개")) {
                    app.exit(44);
                    return;
                }
                headset->setProperty("checked", false);
                QMetaObject::invokeMethod(headset, "toggled");
                speaker->setProperty("checked", true);
                QMetaObject::invokeMethod(speaker, "toggled");
                if (!computer->property("text").toString().contains("스피커") ||
                    discord->property("checked").toBool()) {
                    app.exit(45);
                    return;
                }
                discord->setProperty("checked", true);
                QMetaObject::invokeMethod(discord, "toggled");
                if (!computer->property("text").toString().contains("Discord") ||
                    speaker->property("checked").toBool()) {
                    app.exit(46);
                    return;
                }
                QMetaObject::invokeMethod(micChoices, "close");
                QMetaObject::invokeMethod(computerChoices, "close");
                auto *tabs = root->findChild<QObject *>("mixerTabs");
                if (!tabs || tabs->property("count").toInt() != 2) {
                    app.exit(37);
                    return;
                }
                tabs->setProperty("currentIndex", 1);
            }
            if (!controller.licenseText("MIT").contains("Copyright (c) 2026 yanggogi1423") ||
                !controller.licenseText("OFL").contains("NAVER Corporation") ||
                !controller.licenseText("OFL").contains("SIL OPEN FONT LICENSE")) {
                app.exit(38);
                return;
            }
            if (arguments.contains("--custom")) {
                auto *root = engine.rootObjects().first();
                const auto settings = controller.settings();
                controller.setExpanded(true);
                controller.setExpanded(false);
                const auto fps = root->findChild<QObject *>("simpleFps"),
                           delay = root->findChild<QObject *>("simpleDelay"),
                           quality = root->findChild<QObject *>("simpleQuality");
                if (controller.settings() != settings || !fps || !delay || !quality ||
                    !fps->property("displayText").toString().contains("55") ||
                    !delay->property("displayText").toString().contains("7") ||
                    !quality->property("displayText").toString().contains("1600")) {
                    app.exit(16);
                    return;
                }
            }
            auto *help = root->findChild<QObject *>("helpDialog");
            QMetaObject::invokeMethod(root->findChild<QObject *>("shortcutsAction"), "trigger");
            if (!help || !help->property("visible").toBool()) {
                app.exit(39);
                return;
            }
            QMetaObject::invokeMethod(help, "close");
            auto *licenses = root->findChild<QObject *>("licenseDialog");
            QMetaObject::invokeMethod(root->findChild<QObject *>("licensesAction"), "trigger");
            if (!licenses || !licenses->property("visible").toBool()) {
                app.exit(40);
                return;
            }
            QMetaObject::invokeMethod(licenses, "close");
            auto *updates = root->findChild<QObject *>("updateDialog");
            auto *updateAction = root->findChild<QObject *>("updateAction");
            const bool invoked = QMetaObject::invokeMethod(updateAction, "trigger");
            if (!updates || !updates->property("visible").toBool()) {
                qWarning() << "Update popup failed:" << "action" << bool(updateAction) << "invoked"
                           << invoked << "enabled"
                           << (updateAction && updateAction->property("enabled").toBool())
                           << "popup" << bool(updates) << "size"
                           << (updates ? updates->property("width") : QVariant())
                           << (updates ? updates->property("height") : QVariant()) << "state"
                           << updater.state();
                app.exit(50);
                return;
            }
            QMetaObject::invokeMethod(updates, "close");
            auto *about = root->findChild<QObject *>("aboutDialog");
            auto *aboutAction = root->findChild<QObject *>("aboutAction");
            if (arguments.contains("--review")) {
                auto steps = std::make_shared<QList<QPair<QString, QString>>>(
                    QList<QPair<QString, QString>>{{"fileMenu", "menu"},
                                                   {"microphoneChoices", "microphones"},
                                                   {"computerChoices", "computer-audio"},
                                                   {"helpDialog", "shortcuts"},
                                                   {"licenseDialog", "licenses"},
                                                   {"updateDialog", "updates"}});
                auto index = std::make_shared<int>(0);
                auto timer = new QTimer(&app);
                timer->setInterval(1000);
                QObject::connect(
                    timer, &QTimer::timeout, &app, [&, root, window, steps, index, timer] {
                        if (*index > 0)
                            QMetaObject::invokeMethod(
                                root->findChild<QObject *>(steps->at(*index - 1).first), "close");
                        if (*index == steps->size()) {
                            timer->stop();
                            app.exit(0);
                            return;
                        }
                        auto *popup = root->findChild<QObject *>(steps->at(*index).first);
                        if (!popup) {
                            app.exit(41);
                            return;
                        }
                        QMetaObject::invokeMethod(popup, "open");
                        const auto name = steps->at(*index).second;
                        QTimer::singleShot(250, &app, [window, name, &app] {
                            if (!window->grabWindow().save("artifacts/ui-" + name + ".png")) {
                                qWarning() << "Cannot capture review popup" << name;
                                app.exit(51);
                            }
                        });
                        ++*index;
                    });
                timer->start();
                return;
            }
            QMetaObject::invokeMethod(aboutAction, "trigger");
            QTimer::singleShot(200, &app, [&, about, window] {
                if (!about || !about->property("opened").toBool()) {
                    app.exit(18);
                    return;
                }
                window->grabWindow().save("artifacts/ui-about.png");
                app.exit(0);
            });
        });
    }
    return app.exec();
}
