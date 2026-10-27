#include "RecorderController.h"
#include "AppInfo.h"
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QDir>
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
#ifdef Q_OS_WIN
    // The offscreen Qt test platform does not enumerate the Windows font store.
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/malgun.ttf");
    app.setFont(QFont("Malgun Gothic"));
#endif
    QQuickStyle::setStyle("Basic");
    const auto arguments = app.arguments();
    const bool testing =
        arguments.contains("--ui-smoke") || arguments.contains("--capture-test") ||
        arguments.contains("--edit-test") || arguments.contains("--workflow-test") ||
        arguments.contains("--encoder-test") || arguments.contains("--settings-test");
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
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Luma", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
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
                    !file.startsWith(controller.outputFolder().toLocalFile() + "/")) {
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
        QTimer::singleShot(1000, &app, [&] {
            auto *root = engine.rootObjects().first();
            auto *menu = root->findChild<QObject *>("mainMenu");
            auto *folderButton = root->findChild<QObject *>("chooseFolderButton");
            if (controller.settings().value("container") != "mp4" ||
                controller.appInfo().value("name") != "FARecorder" ||
                controller.appInfo().value("version") != QCoreApplication::applicationVersion() ||
                !menu || !menu->property("visible").toBool() || !folderButton ||
                !folderButton->property("visible").toBool()) {
                app.exit(17);
                return;
            }
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            const auto image = window->grabWindow();
            if (!image.isNull()) {
                QDir().mkpath("artifacts");
                image.save(arguments.contains("--custom")     ? "artifacts/ui-custom.png"
                           : arguments.contains("--expanded") ? "artifacts/ui-expanded.png"
                                                              : "artifacts/ui-preview.png");
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
            auto *about = root->findChild<QObject *>("aboutDialog");
            auto *aboutAction = root->findChild<QObject *>("aboutAction");
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
