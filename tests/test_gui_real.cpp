// Candidate-only smoke: actual QML + controller + installed CLI + real tools/model.
#include "backend.hpp"
#include "files.hpp"
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

// NOLINTNEXTLINE(bugprone-throwing-static-initialization): Qt requires this generated registration before QML engine startup.
Q_IMPORT_QML_PLUGIN(TranscribePlugin)
namespace {
bool validSubtitles(const QByteArray& bytes, bool web) {
    const QRegularExpression pattern(web
        ? R"((\d{2,}):(\d{2}):(\d{2})\.(\d{3}) --> (\d{2,}):(\d{2}):(\d{2})\.(\d{3}))"
        : R"((\d{2,}):(\d{2}):(\d{2}),(\d{3}) --> (\d{2,}):(\d{2}):(\d{2}),(\d{3}))");
    auto matches = pattern.globalMatch(QString::fromUtf8(bytes));
    qint64 previous = 0; unsigned count = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto time = [&](int first) {
            return ((match.captured(first).toLongLong() * 60 + match.captured(first + 1).toLongLong()) * 60
                + match.captured(first + 2).toLongLong()) * 1000 + match.captured(first + 3).toLongLong();
        };
        for (const int field : {2, 3, 6, 7}) if (match.captured(field).toInt() >= 60) return false;
        const auto begin = time(1), end = time(5);
        if (begin < previous || end < begin || end > 30000) return false;
        previous = begin; ++count;
    }
    return count > 0;
}
QString checksum(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
}
class RealGuiTests : public QObject {
    Q_OBJECT
private slots:
    void portableHttpsDownload() {
        if (qEnvironmentVariableIsEmpty("TRANSCRIBE_CA_BUNDLE")) QSKIP("Portable certificate bundle not configured");
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QByteArray expected("94f29bbed6a22c35b992c5c6ebf0e7c92f13b836b90f36f461c9cf2f0f1d010d");
        ModelManager manager(temporary.path(), {{"small", "network-probe.bin",
            "https://raw.githubusercontent.com/ggml-org/whisper.cpp/927cfce34f31707e17f2bff35c349632fb9e2c3a/LICENSE", expected}});
        QSignalSpy installed(&manager, &ModelManager::installed), failures(&manager, &ModelManager::failed);
        manager.download("small");
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 60000);
        QVERIFY2(failures.isEmpty(), qPrintable(failures.isEmpty() ? QString{} : failures.first().first().toString()));
        QCOMPARE(installed.count(), 1);
        QCOMPARE(checksum(temporary.path() + "/models/network-probe.bin").toLatin1(), expected);
    }
    void installedCliWithRealSpeech() {
        const auto binary = qEnvironmentVariable("TRANSCRIBE_REAL_BINARY"), audio = qEnvironmentVariable("TRANSCRIBE_REAL_AUDIO");
        const auto home = qEnvironmentVariable("TRANSCRIBE_REAL_HOME"), artifacts = qEnvironmentVariable("TRANSCRIBE_REAL_ARTIFACTS");
        const auto selection = qEnvironmentVariable("TRANSCRIBE_REAL_MODEL", "small");
        QVERIFY2(QFileInfo(binary).isExecutable(), "Set TRANSCRIBE_REAL_BINARY to the installed CLI");
        QVERIFY2(QFileInfo(audio).isFile(), "Set TRANSCRIBE_REAL_AUDIO to the fixed speech example");
        QVERIFY2(!artifacts.isEmpty() && !QFileInfo::exists(artifacts), "Artifacts must be a new directory");
        QVERIFY(QDir().mkpath(artifacts));
        const auto records = ModelManager::manifest();
        QString modelHash, modelSource, modelFilename;
        for (const auto& record : records) if (record.selection == selection) {
            modelSource = home + "/models/" + record.filename; modelFilename = record.filename;
            modelHash = checksum(modelSource);
            QCOMPARE(modelHash.toLatin1(), record.sha256);
        }
        QVERIFY(!modelHash.isEmpty());
        QSettings settings(artifacts + "/settings.ini", QSettings::IniFormat);
        settings.setValue("output", artifacts + "/Результаты 😀"); settings.sync();
        const auto installedHome = artifacts + "/first-install";
#ifndef Q_OS_WIN
        // Linux installs the engine in the data home. Windows must use the
        // original tools beside the actual unpacked CLI, without a shadow copy.
        if (!transcribe::portable_bundle(transcribe::utf8_path((QFileInfo(binary).absolutePath() + "/tools").toUtf8().toStdString()))) {
            const auto engineDirectory = installedHome + "/whisper.cpp/build/bin";
            QVERIFY(QDir().mkpath(engineDirectory));
            const QDir sourceEngine(home + "/whisper.cpp/build/bin");
            for (const auto& file : sourceEngine.entryInfoList({"whisper-cli", "whisper-cli.exe", "*.dll"}, QDir::Files)) {
                const auto target = engineDirectory + '/' + file.fileName(); QVERIFY(QFile::copy(file.filePath(), target));
                QVERIFY(QFile::setPermissions(target, file.permissions()));
            }
        }
#endif
        Backend backend({binary, artifacts + "/settings.ini", installedHome});
        QQmlApplicationEngine engine;
        QStringList warnings, stages;
        bool livePreview = false;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
            for (const auto& error : errors) warnings.append(error.toString());
        });
        connect(&backend, &Backend::changed, this, [&] {
            if (!backend.stage().isEmpty() && !stages.contains(backend.stage())) stages.append(backend.stage());
        });
        connect(&backend, &Backend::transcriptChanged, this, [&] {
            if (backend.busy() && !backend.transcript().trimmed().isEmpty()) livePreview = true;
        });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(GUI_QML_SOURCE));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        backend.importModel(modelSource, selection); QVERIFY(backend.modelBusy());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.modelBusy(), 30000);
        QCOMPARE(checksum(installedHome + "/models/" + modelFilename), modelHash);
        QCOMPARE(backend.modelSelection(), selection);
        backend.start(audio, {{"output", artifacts + "/Результаты 😀"}, {"model", selection},
                              {"threads", 4}, {"chunks", 2}, {"jobs", 1}, {"vad", false}});
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 300000);
        QVERIFY2(backend.state() == TaskState::completed, qPrintable(backend.error()));
        const auto directory = backend.taskResultDirectory();
        const auto bytes = readSmallFile(directory + "/result.json", 65536);
        if (!bytes) QFAIL("Missing installed candidate result manifest");
        const auto result = QJsonDocument::fromJson(*bytes).object();
        QCOMPARE(result.value("status").toString(), QString("completed")); QCOMPARE(result.value("code").toInt(-1), 0);
        QJsonObject exports;
        for (const auto* extension : {"txt", "srt", "vtt"}) {
            const auto path = directory + "/transcripts/transcript." + extension;
            const auto text = readSmallFile(path, std::size_t{1024} * 1024);
            if (!text) QFAIL("Missing installed candidate export");
            QVERIFY(!text->trimmed().isEmpty());
            if (QString::fromLatin1(extension) == "srt") QVERIFY(validSubtitles(*text, false));
            if (QString::fromLatin1(extension) == "vtt") { QVERIFY(text->startsWith("WEBVTT")); QVERIFY(validSubtitles(*text, true)); }
            exports.insert(extension, QJsonObject{{"sha256", checksum(path)}, {"bytes", static_cast<double>(text->size())}});
        }
        QTRY_COMPARE(backend.history()->rowCount(), 1);
        QTRY_VERIFY(!backend.transcript().trimmed().isEmpty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QVERIFY(livePreview);
        QVERIFY(window->grabWindow().save(artifacts + "/gui.png"));
        const QJsonObject evidence{{"schema", 1}, {"source", TRANSCRIBE_SOURCE_SHA}, {"qt", qVersion()},
                                  {"audio_sha256", checksum(audio)}, {"model_sha256", modelHash}, {"model", selection},
                                  {"completed", true}, {"live_preview", livePreview}, {"first_model_import", true}, {"exports", exports},
                                  {"stages", QJsonArray::fromStringList(stages)}};
        QFile report(artifacts + "/validation.json"); QVERIFY(report.open(QIODevice::WriteOnly));
        const auto encoded = QJsonDocument(evidence).toJson(); QCOMPARE(report.write(encoded), encoded.size());
    }
};
int main(int argc, char** argv) {
    QQuickStyle::setStyle("Fusion"); QGuiApplication app(argc, argv);
    RealGuiTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_gui_real.moc"
