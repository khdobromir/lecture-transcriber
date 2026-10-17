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
#include <QSignalSpy>
#include <QPalette>
#include <QScopeGuard>
#include <QtQml/qqmlextensionplugin.h>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): Qt requires this generated registration before QML engine startup.
Q_IMPORT_QML_PLUGIN(TranscribePlugin)
class QmlTests : public QObject {
    Q_OBJECT
private slots:
    void minimumWindowAndPalettes_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QString>("language");
        QTest::newRow("light-ru") << false << QString("ru");
        QTest::newRow("dark-ru") << true << QString("ru");
        QTest::newRow("light-en") << false << QString("en");
        QTest::newRow("dark-en") << true << QString("en");
    }
    void minimumWindowAndPalettes() {
        QFETCH(bool, dark);
        QFETCH(QString, language);
        const auto previous = QGuiApplication::palette();
        const auto restore = qScopeGuard([previous] { QGuiApplication::setPalette(previous); });
        QPalette palette;
        const auto background = dark ? QColor("#202020") : QColor("#f0f0f0"), foreground = dark ? QColor("#eeeeee") : QColor("#111111");
        for (const auto role : {QPalette::Window, QPalette::Base, QPalette::Button}) palette.setColor(role, background);
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) palette.setColor(role, foreground);
        QGuiApplication::setPalette(palette);
        QTemporaryDir temp;
        Backend backend({"missing", temp.path() + "/settings.ini", temp.path() + "/data"});
        backend.initializeLanguage();
        backend.language()->setSelection(language);
        backend.theme()->setMode(dark ? "dark" : "light");
        backend.theme()->setCustomColor(QColor("#6750a4"));
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) { for (const auto& error : errors) warnings.append(error.toString()); });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(720, 420);
        auto* source = window->findChild<QQuickItem*>("sourceInput");
        QVERIFY(source);
        source->setProperty("text", QString(1024, QChar(0x044f)));
        source->forceActiveFocus();
        QTRY_VERIFY(source->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_Backtab);
        QTRY_VERIFY(source->hasActiveFocus());
        const auto position = source->mapToScene(QPointF{});
        QVERIFY(position.x() >= 0);
        QVERIFY(position.x() + source->width() <= window->width());
        const auto screenshot = window->grabWindow();
        QVERIFY(!screenshot.isNull());
        const auto artifact = qEnvironmentVariable("TRANSCRIBE_GUI_VISUAL_DIRECTORY");
        if (!artifact.isEmpty()) {
            QVERIFY(QDir().mkpath(artifact));
            QVERIFY(screenshot.save(artifact + "/" + language + (dark ? "-dark.png" : "-light.png")));
        }
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void responsiveNavigationAndThemePreserveDraft() {
        QTemporaryDir temp;
        Backend backend({"missing", temp.path() + "/settings.ini", temp.path() + "/data"});
        backend.initializeLanguage();
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        connect(backend.language(), &LanguageController::changed, &engine, &QQmlEngine::retranslate);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* source = window->findChild<QQuickItem*>("sourceInput");
        QVERIFY(source);
        auto* navigation = window->findChild<QQuickItem*>("navigationTabs");
        QVERIFY(navigation);
        source->setProperty("text", QString("Draft лекция 😀"));
        QCOMPARE(backend.draft().value("source").toString(), QString("Draft лекция 😀"));
        const auto artifact = qEnvironmentVariable("TRANSCRIBE_GUI_VISUAL_DIRECTORY");
        for (const auto* language : {"ru", "en"}) {
            backend.language()->setSelection(language);
            for (const auto* mode : {"light", "dark"}) {
                backend.theme()->setMode(mode);
                backend.theme()->setCustomColor(QColor("#0061a4"));
                for (const auto size : {QSize(720, 420), QSize(999, 540), QSize(1000, 540), QSize(1400, 800), QSize(720, 420)}) {
                    window->resize(size);
                    QTest::qWait(20);
                    QCOMPARE(window->property("expandedNavigation").toBool(), size.width() >= 1000);
                    for (int page = 0; page < 4; ++page) {
                        navigation->setProperty("currentIndex", page);
                        QTest::qWait(20);
                        QCOMPARE(window->findChild<QQuickItem*>("sourceInput"), source);
                        QCOMPARE(source->property("text").toString(), QString("Draft лекция 😀"));
                        if (!artifact.isEmpty()) {
                            QVERIFY(QDir().mkpath(artifact));
                            const auto name = QString("/%1-%2-%3x%4-page%5.png").arg(language, mode).arg(size.width()).arg(size.height()).arg(page);
                            QVERIFY(window->grabWindow().save(artifact + name));
                        }
                    }
                }
            }
        }
        backend.theme()->setReducedMotion(true);
        QVERIFY(backend.theme()->reducedMotion());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void materialComponentStates() {
        QTemporaryDir temp;
        Backend backend({"missing", temp.path() + "/settings.ini", temp.path() + "/data"});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QFileInfo(GUI_QML_SOURCE).absolutePath() + "/ComponentGallery.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* button = window->findChild<QQuickItem*>("probeButton");
        QVERIFY(button);
        auto* field = window->findChild<QQuickItem*>("probeField");
        QVERIFY(field);
        auto* toggle = window->findChild<QQuickItem*>("probeSwitch");
        QVERIFY(toggle);
        for (const auto* mode : {"light", "dark"}) {
            backend.theme()->setMode(mode);
            for (const bool reduced : {false, true}) {
                backend.theme()->setReducedMotion(reduced);
                field->forceActiveFocus();
                QVERIFY(field->hasActiveFocus());
                QMetaObject::invokeMethod(field, "selectAll");
                QCOMPARE(field->property("selectedTextColor").value<QColor>(), backend.theme()->colors().value("onPrimary").value<QColor>());
                button->forceActiveFocus();
                const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
                QTest::mouseMove(window, point);
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
                QVERIFY(button->property("down").toBool());
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
                toggle->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_Space);
                QTest::qWait(200);
                const auto artifact = qEnvironmentVariable("TRANSCRIBE_GUI_VISUAL_DIRECTORY");
                if (!artifact.isEmpty()) {
                    QVERIFY(QDir().mkpath(artifact));
                    QVERIFY(window->grabWindow().save(artifact + QString("/components-%1-%2.png").arg(mode).arg(reduced)));
                }
            }
        }
    }
    void liveLanguagePreservesWindowAndMessages() {
        QTemporaryDir temp;
        Backend backend({"missing", temp.path() + "/settings.ini", temp.path() + "/data"});
        backend.initializeLanguage();
        backend.language()->setSelection("ru");
        QQmlApplicationEngine engine;
        connect(backend.language(), &LanguageController::changed, &engine, &QQmlEngine::retranslate);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* source = window->findChild<QQuickItem*>("sourceInput");
        QVERIFY(source);
        auto* picker = window->findChild<QQuickItem*>("languagePicker");
        QVERIFY(picker);
        source->setProperty("text", "Лекция %1 😀");
        source->forceActiveFocus();
        QTRY_VERIFY(source->hasActiveFocus());
        backend.openTaskResult(); // A cached application-owned diagnostic.
        QVERIFY(backend.error().contains("Каталог"));
        backend.history()->replace({{"Лекция 😀", "completed", "medium", temp.path(), "2026-10-12", true}});
        QSignalSpy historyChanged(backend.history(), &QAbstractItemModel::dataChanged);
        backend.language()->setSelection("en");
        QTRY_COMPARE(source->property("placeholderText").toString(), QString("Paste a URL or select a file"));
        QCOMPARE(backend.status(), QString("No task started"));
        QCOMPARE(backend.error(), QString("The result directory has not been created yet"));
        QCOMPARE(backend.history()->data(backend.history()->index(0), HistoryModel::Status).toString(), QString("Completed"));
        QCOMPARE(source->property("text").toString(), QString("Лекция %1 😀"));
        QVERIFY(source->hasActiveFocus());
        QCOMPARE(historyChanged.count(), 1);
        QCOMPARE(picker->property("currentIndex").toInt(), 2);
        backend.language()->setSelection("ru");
        QTRY_COMPARE(source->property("placeholderText").toString(), QString("Вставьте URL или выберите файл"));
        QVERIFY(backend.error().contains("Каталог"));
        QCOMPARE(source->property("text").toString(), QString("Лекция %1 😀"));
    }
    void windowLoadsAndKeyboardFocusWorks() {
        QTemporaryDir temp;
        QSettings settings(temp.path() + "/settings.ini", QSettings::IniFormat);
        settings.setValue("output", temp.path() + "/results");
        settings.sync();
        Backend backend({"/missing-backend", temp.path() + "/settings.ini", temp.path() + "/data"});
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) { for (const auto& error : errors) warnings.append(error.toString()); });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        auto* source = window->findChild<QQuickItem*>("sourceInput");
        QVERIFY(source);
        source->forceActiveFocus();
        QTRY_VERIFY(source->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(window->activeFocusItem() != source);
        QTest::keyClick(window, Qt::Key_Backtab);
        QTRY_VERIFY(source->hasActiveFocus());
        backend.history()->replace({{"<b>Лекция 😀</b>", "interrupted", "medium", temp.path(), "2026-10-04", true}});
        auto* tabs = window->findChild<QQuickItem*>("navigationTabs");
        QVERIFY(tabs);
        tabs->setProperty("currentIndex", 1);
        QTest::qWait(50);
        tabs->setProperty("currentIndex", 2);
        QTest::qWait(50);
        window->resize(720, 420);
        QTest::qWait(50);
        tabs->setProperty("currentIndex", 0);
        window->resize(1040, 820);
        QTest::qWait(100);
        const auto screenshot = window->grabWindow();
        QVERIFY(!screenshot.isNull());
        const auto requested = qEnvironmentVariable("TRANSCRIBE_GUI_PREVIEW");
        if (!requested.isEmpty()) QVERIFY(screenshot.save(requested));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    void closingModelInstallationIsAsynchronous() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/data";
        QDir().mkpath(root);
        transcribe::FileLock lock(transcribe::utf8_path((root + "/.install.lock").toUtf8().toStdString()));
        Backend backend({"missing", temp.path() + "/settings.ini", root, {{"medium", "ggml-model.bin", "http://127.0.0.1:1/model", QByteArray(64, '0')}}});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        backend.downloadModel("medium");
        QVERIFY(backend.modelBusy());
        window->close();
        auto* confirmation = window->findChild<QObject*>("closeConfirmation");
        QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        auto* cancel = window->findChild<QObject*>("cancelCloseButton");
        QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.active(), 3000);
        QVERIFY(backend.error().isEmpty());
        QVERIFY(backend.modelStatus().contains(QStringLiteral("отменена")));
    }
    void closingActiveTaskRequiresConfirmation() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/data";
        const auto put = [](const QString& path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture") < 0) qFatal("close fixture failed");
            file.close();
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        };
        put(root + "/whisper.cpp/build/bin/whisper-cli"
#ifdef Q_OS_WIN
                   ".exe"
#endif
        );
        put(root + "/models/ggml-medium-q5_0.bin");
        put(root + "/models/ggml-silero-v6.2.0.bin");
        put(temp.path() + "/input.wav");
        QSettings settings(temp.path() + "/settings.ini", QSettings::IniFormat);
        settings.setValue("output", temp.path() + "/results");
        settings.sync();
        Backend backend({TEST_GUI_HELPER, temp.path() + "/settings.ini", root});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        qputenv("TRANSCRIBE_TEST_MODE", "cancel");
        backend.start(temp.path() + "/input.wav", {{"output", temp.path() + "/results"}});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.resultDirectory().isEmpty(), 3000);
        window->close();
        auto* confirmation = window->findChild<QObject*>("closeConfirmation");
        QVERIFY(confirmation);
        QTRY_VERIFY(confirmation->property("visible").toBool());
        QVERIFY(window->isVisible());
        auto* stay = window->findChild<QObject*>("stayButton");
        QVERIFY(stay);
        QVERIFY(QMetaObject::invokeMethod(stay, "clicked"));
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        QVERIFY(backend.busy());
        window->close();
        QTRY_VERIFY(confirmation->property("visible").toBool());
        auto* cancel = window->findChild<QObject*>("cancelCloseButton");
        QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Прервано"));
        QVERIFY(QFile::exists(backend.resultDirectory() + "/transcripts/transcript.txt"));
        QVERIFY(QDir(backend.resultDirectory() + "/audio").exists());
    }
};
int main(int argc, char** argv) {
    QQuickStyle::setStyle("Material");
    QGuiApplication app(argc, argv);
    QmlTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_qml.moc"
