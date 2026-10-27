#include "RecorderController.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QShortcut>
#include <QAudioSink>
#include <QAudioFormat>
#include <QBuffer>
#include <QFontDatabase>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <winrt/base.h>
#endif

int main(int argc,char** argv){
#ifdef Q_OS_WIN
    winrt::init_apartment(winrt::apartment_type::single_threaded);
#endif
    QGuiApplication app(argc,argv);app.setOrganizationName("Luma");app.setApplicationName("Luma Recorder");app.setApplicationVersion("0.1.0-alpha.1");
#ifdef Q_OS_WIN
    // The offscreen Qt test platform does not enumerate the Windows font store.
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/malgun.ttf");
    app.setFont(QFont("Malgun Gothic"));
#endif
    QQuickStyle::setStyle("Basic");
    const auto arguments=app.arguments();
    if(arguments.contains("--tone")){
        const int index=arguments.indexOf("--tone");const double hz=arguments.value(index+1,"440").toDouble();
        QAudioFormat format;format.setSampleRate(48000);format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Int16);
        QByteArray samples(48000*4*12,0);
        for(int frame=0;frame<48000*12;frame++){
            const auto sample=qint16(500*std::sin(2.0*3.141592653589793*hz*frame/48000));
            qToLittleEndian(sample,samples.data()+frame*4);qToLittleEndian(sample,samples.data()+frame*4+2);
        }
        QBuffer buffer(&samples);buffer.open(QIODevice::ReadOnly);QAudioSink sink(format);
        QTimer::singleShot(1500,&app,[&]{sink.start(&buffer);});QTimer::singleShot(13500,&app,&QCoreApplication::quit);return app.exec();
    }
    QQmlApplicationEngine engine;auto* preview=new PreviewProvider;engine.addImageProvider("preview",preview);
    RecorderController controller(preview);engine.rootContext()->setContextProperty("recorder",&controller);
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(1);},Qt::QueuedConnection);
    engine.loadFromModule("Luma","Main");if(engine.rootObjects().isEmpty())return 1;
    if(arguments.contains("--capture-test")){
#ifdef Q_OS_WIN
        QDir().mkpath("artifacts");QFile::remove("artifacts/capture-error.txt");QFile::remove("artifacts/capture-result.json");
        auto* animation=new QTimer(&app);animation->setInterval(50);
        QObject::connect(animation,&QTimer::timeout,&app,[&]{auto* root=engine.rootObjects().first();root->setProperty("captureTestTick",root->property("captureTestTick").toInt()+1);});animation->start();
        auto platform=std::shared_ptr<luma::Platform>(luma::createPlatform());
        auto capture=std::shared_ptr<luma::VideoCapture>(platform->createVideoCapture());
        auto session=std::make_shared<luma::RecordingSession>();
        auto tone1=std::make_shared<QProcess>();auto tone2=std::make_shared<QProcess>();
        tone1->start(app.applicationFilePath(),{"--tone","440"});tone2->start(app.applicationFilePath(),{"--tone","880"});
        tone1->waitForStarted();tone2->waitForStarted();
        auto mix1=std::make_shared<luma::AudioMix>();auto mix2=std::make_shared<luma::AudioMix>();
        QObject::connect(session.get(),&luma::RecordingSession::finished,&app,[&,platform,capture,session,tone1,tone2](QString file,QString warning){
            tone1->kill();tone2->kill();tone1->waitForFinished();tone2->waitForFinished();
            capture->stop();QFile result("artifacts/capture-result.json");result.open(QIODevice::WriteOnly);
            result.write(QJsonDocument(QJsonObject{{"file",file},{"warning",warning},{"frames",double(session->frames())}}).toJson());
            app.exit(warning.isEmpty()?0:2);
        });
        QObject::connect(session.get(),&luma::RecordingSession::failed,&app,[&,platform,capture,session,tone1,tone2](QString error,QString){
            QFile result("artifacts/capture-error.txt");result.open(QIODevice::WriteOnly);result.write(error.toUtf8());tone1->kill();tone2->kill();app.exit(3);
        });
        QTimer::singleShot(600,&app,[&,platform,capture,session,tone1,tone2,mix1,mix2]{
            try{
                auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                // WGC cannot capture a hidden HWND. Show only our own test window,
                // without activating it or changing the user's foreground app.
                ShowWindow(reinterpret_cast<HWND>(window->winId()),SW_SHOWNOACTIVATE);
                luma::Source source{QString::number(quintptr(window->winId())),"Luma self-test",luma::SourceKind::Window,QRect(0,0,1380,920)};
                capture->start(source,{},true,[&](QString error){QFile out("artifacts/capture-error.txt");out.open(QIODevice::WriteOnly);out.write(error.toUtf8());});
                QTimer::singleShot(800,&app,[&,platform,capture,session,tone1,tone2,mix1,mix2]{
                    luma::Settings settings;settings.width=640;settings.height=360;settings.fps=30;settings.delaySeconds=0;settings.outputFolder=QDir::current().absoluteFilePath("artifacts/recordings");
                    QList<luma::AudioSource> audio{
                        {QString::number(tone1->processId()),"Tone440","test",luma::AudioKind::Application,quint32(tone1->processId()),mix1},
                        {QString::number(tone2->processId()),"Tone880","test",luma::AudioKind::Application,quint32(tone2->processId()),mix2}};
                    session->start(platform.get(),capture.get(),settings,audio);
                    QTimer::singleShot(1800,&app,[mix1]{mix1->gain=0.5f;});
                    QTimer::singleShot(2800,&app,[mix2]{mix2->muted=true;});
                    QTimer::singleShot(3800,&app,[mix2]{mix2->muted=false;});
                    QTimer::singleShot(5000,&app,[session]{session->requestStop();});
                });
            }catch(const std::exception& ex){QFile result("artifacts/capture-error.txt");result.open(QIODevice::WriteOnly);result.write(ex.what());app.exit(4);}
        });
        QTimer::singleShot(30000,&app,[]{QCoreApplication::exit(5);});
#else
        return 6;
#endif
    }
    if(arguments.contains("--edit-test")){
        const auto input=arguments.value(arguments.indexOf("--edit-test")+1);
        controller.openRecording(QUrl::fromLocalFile(input));
        QObject::connect(&controller,&RecorderController::trimFinished,&app,[&](QUrl file){
            QFile result("artifacts/edit-result.json");result.open(QIODevice::WriteOnly);result.write(QJsonDocument(QJsonObject{{"file",file.toLocalFile()}}).toJson());app.exit(0);
        });
        QTimer::singleShot(2500,&app,[&]{
            auto* player=engine.rootObjects().first()->findChild<QObject*>("editorPlayer");
            if(!player||player->property("duration").toLongLong()<2500){QFile out("artifacts/edit-error.txt");out.open(QIODevice::WriteOnly);out.write("Media player did not load the test video");app.exit(7);return;}
            // The UI choice must exist; overwrite is selected only by an explicit flag.
            player->setProperty("source",QUrl());
            controller.exportTrim(.5,2.5,arguments.contains("--overwrite"));
        });
        QTimer::singleShot(20000,&app,[]{QCoreApplication::exit(8);});
    }
    if(app.arguments().contains("--ui-smoke")){
        QTimer::singleShot(1000,&app,[&]{
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            const auto image=window->grabWindow();
            if(!image.isNull()){QDir().mkpath("artifacts");image.save("artifacts/ui-preview.png");}
            app.exit(0);
        });
    }
    return app.exec();
}
