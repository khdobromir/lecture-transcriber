#include "backend.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): Qt requires this generated registration before QML engine startup.
Q_IMPORT_QML_PLUGIN(TranscribePlugin)
class QmlTests : public QObject {
    Q_OBJECT
private slots:
    void windowLoadsAndKeyboardFocusWorks() {
        QTemporaryDir temp;
        QSettings settings(temp.path() + "/settings.ini", QSettings::IniFormat); settings.setValue("output", temp.path() + "/results"); settings.sync();
        Backend backend({"/missing-backend", temp.path() + "/settings.ini", temp.path() + "/data"});
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) { for (const auto& error : errors) warnings.append(error.toString()); });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        auto* source = window->findChild<QQuickItem*>("sourceInput"); QVERIFY(source);
        source->forceActiveFocus(); QTRY_VERIFY(source->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab); QVERIFY(window->activeFocusItem() != source);
        QTest::keyClick(window, Qt::Key_Backtab); QTRY_VERIFY(source->hasActiveFocus());
        backend.history()->replace({{"<b>Лекция 😀</b>", "interrupted", "medium", temp.path(), "2026-10-04", true}});
        auto* tabs = window->findChild<QQuickItem*>("navigationTabs"); QVERIFY(tabs);
        tabs->setProperty("currentIndex", 1); QTest::qWait(50);
        tabs->setProperty("currentIndex", 2); QTest::qWait(50);
        window->resize(720, 600); QTest::qWait(50);
        tabs->setProperty("currentIndex", 0); window->resize(1040, 820);
        QTest::qWait(100);
        const auto screenshot = window->grabWindow(); QVERIFY(!screenshot.isNull());
        const auto requested = qEnvironmentVariable("TRANSCRIBE_GUI_PREVIEW");
        if (!requested.isEmpty()) QVERIFY(screenshot.save(requested));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void closingModelInstallationIsAsynchronous() {
        QTemporaryDir temp; const auto root = temp.path() + "/data"; QDir().mkpath(root);
        transcribe::FileLock lock(transcribe::utf8_path((root + "/.install.lock").toUtf8().toStdString()));
        Backend backend({"missing", temp.path() + "/settings.ini", root,
            {{"medium", "ggml-model.bin", "http://127.0.0.1:1/model", QByteArray(64, '0')}}});
        QQmlApplicationEngine engine; engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE)); QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        backend.downloadModel("medium"); QVERIFY(backend.modelBusy()); window->close();
        auto* confirmation = window->findChild<QObject*>("closeConfirmation"); QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        auto* cancel = window->findChild<QObject*>("cancelCloseButton"); QVERIFY(cancel); QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.active(), 3000); QVERIFY(backend.error().isEmpty());
        QVERIFY(backend.modelStatus().contains(QStringLiteral("отменена")));
    }
    void closingActiveTaskRequiresConfirmation() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/data";
        const auto put = [](const QString& path) {
            QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture") < 0) qFatal("close fixture failed");
            file.close(); QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        };
        put(root + "/whisper.cpp/build/bin/whisper-cli"
#ifdef Q_OS_WIN
            ".exe"
#endif
        );
        put(root + "/models/ggml-medium-q5_0.bin"); put(root + "/models/ggml-silero-v6.2.0.bin");
        put(temp.path() + "/input.wav");
        QSettings settings(temp.path() + "/settings.ini", QSettings::IniFormat);
        settings.setValue("output", temp.path() + "/results"); settings.sync();
        Backend backend({TEST_GUI_HELPER, temp.path() + "/settings.ini", root});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        qputenv("TRANSCRIBE_TEST_MODE", "cancel");
        backend.start(temp.path() + "/input.wav", {{"output", temp.path() + "/results"}});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.resultDirectory().isEmpty(), 3000);
        window->close();
        auto* confirmation = window->findChild<QObject*>("closeConfirmation"); QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("visible").toBool()); QVERIFY(window->isVisible());
        auto* stay = window->findChild<QObject*>("stayButton"); QVERIFY(stay);
        QVERIFY(QMetaObject::invokeMethod(stay, "clicked"));
        QTRY_VERIFY(!confirmation->property("visible").toBool()); QVERIFY(backend.busy());
        window->close(); QTRY_VERIFY(confirmation->property("visible").toBool());
        auto* cancel = window->findChild<QObject*>("cancelCloseButton"); QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Прервано"));
        QVERIFY(QFile::exists(backend.resultDirectory() + "/transcripts/transcript.txt"));
        QVERIFY(QDir(backend.resultDirectory() + "/audio").exists());
    }
};
int main(int argc, char** argv) { QQuickStyle::setStyle("Fusion"); QGuiApplication app(argc, argv); QmlTests test; return QTest::qExec(&test, argc, argv); }
#include "test_qml.moc"
