#include "backend.hpp"
#include "models.hpp"
#include "files.hpp"
#include "windows.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>

namespace {
void put(const QString& path, const QByteArray& value) {
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size()) qFatal("fixture write failed");
}
int helper(const QStringList& args) {
    const auto value = [&](const QString& key) { const auto index = args.indexOf(key); return index >= 0 ? args.value(index + 1) : QString{}; };
    const QString mode = qEnvironmentVariable("TRANSCRIBE_TEST_MODE", "normal");
    const auto root = value("--out") + "/Лекция_😀";
    QDir().mkpath(root + "/transcripts"); QDir().mkpath(root + "/audio");
    const auto send = [&](QJsonObject object) { object.insert("protocol", mode == "wrong_version" ? 2 : 1); std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush; };
    send({{"type", "hello"}});
    if (mode == "bad_json") { std::cout << "not JSON\n" << std::flush; return 1; }
    send({{"type", "result"}, {"result", root}});
    send({{"type", "stage"}, {"stage", "recognize"}, {"result", root}});
    send({{"type", "progress"}, {"fraction", 1.0}, {"result", root}, {"eta_seconds", QJsonValue::Null}});
    if (mode == "exit_only" || mode == "wrong_version") return 0;
    if (mode == "cancel") {
        put(root + "/transcripts/transcript.txt", "частичный текст\n");
        std::string command; std::getline(std::cin, command);
        send({{"type", "failed"}, {"result", root}, {"status", "interrupted"}, {"code", 130}, {"message", "Отменено"}});
        return 130;
    }
    QTest::qSleep(150);
    if (mode != "missing_manifest") {
        put(root + "/result.json", R"({"version":1,"status":"completed","code":0,"model":"medium"})");
        for (const auto* extension : {"txt", "srt", "vtt"}) put(root + "/transcripts/transcript." + extension, "текст\n");
    }
    if (mode == "split_utf8") {
        const auto bytes = QJsonDocument(QJsonObject{{"protocol", 1}, {"type", "completed"}, {"result", root}, {"status", "completed"}, {"code", 0}}).toJson(QJsonDocument::Compact) + '\n';
        const auto cut = bytes.indexOf(QString("Л").toUtf8()) + 1;
        std::cout.write(bytes.data(), cut); std::cout.flush(); QTest::qSleep(30);
        std::cout.write(bytes.data() + cut, bytes.size() - cut); std::cout.flush();
    } else send({{"type", "completed"}, {"result", root}, {"status", "completed"}, {"code", 0}});
    return 0;
}
struct Fixture {
    QTemporaryDir temp;
    QString root = temp.path() + "/data", output = temp.path() + "/results", ini = temp.path() + "/settings.ini";
    Fixture() {
        const auto engine = root + "/whisper.cpp/build/bin/whisper-cli"
#ifdef Q_OS_WIN
            ".exe"
#endif
            ;
        put(engine, "mock"); QFile::setPermissions(engine, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        put(root + "/models/ggml-medium-q5_0.bin", "model"); put(root + "/models/ggml-silero-v6.2.0.bin", "vad");
        put(temp.path() + "/input.wav", "audio");
        QSettings settings(ini, QSettings::IniFormat); settings.setValue("output", output);
    }
    QVariantMap options() const { return {{"output", output}}; }
};
struct ModelServer {
    QTcpServer server;
    QByteArray payload = QByteArray(4096, 'm');
    bool ignoreRange = false, corrupt = false, stall = false;
    int requests = 0;
    ModelServer() {
        if (!server.listen(QHostAddress::LocalHost)) qFatal("HTTP fixture listen failed");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                auto bytes = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                    *bytes += socket->readAll();
                    if (!bytes->contains("\r\n\r\n")) return;
                    socket->disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                    ++requests;
                    qint64 offset = 0;
                    for (const auto& header : bytes->split('\n')) if (header.trimmed().toLower().startsWith("range:")) {
                        const auto equal = header.indexOf('='); offset = header.mid(equal + 1).split('-').first().toLongLong();
                    }
                    if (ignoreRange) offset = 0;
                    const auto body = (corrupt ? QByteArray(payload.size(), 'x') : payload).mid(offset);
                    QByteArray headers = offset ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(offset) + '-' + QByteArray::number(payload.size() - 1) + '/' + QByteArray::number(payload.size()) + "\r\n" : QByteArray("HTTP/1.1 200 OK\r\n");
                    headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                    socket->write(headers + (stall ? body.left(100) : body)); socket->flush();
                    if (!stall) socket->disconnectFromHost();
                });
            }
        });
    }
    QVector<ModelManager::Record> records() const {
        const QString url = "http://127.0.0.1:" + QString::number(server.serverPort()) + "/model";
        const auto hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
        return {{"medium", "ggml-medium.bin", url, hash}, {"vad", "ggml-vad.bin", url, hash}};
    }
};
}
class GuiTests : public QObject {
    Q_OBJECT
private slots:
    void readersPermitPublication() {
        QTemporaryDir temp;
        for (const auto* name : {"transcript.txt", "result.json"}) {
            const auto target = temp.path() + '/' + name;
            const auto replacement = temp.path() + "/replacement";
            put(target, "previous"); put(replacement, "published");
#ifdef Q_OS_WIN
            // Reproduce the exact previous GUI QFile sharing conflict first.
            QFile legacy(target); QVERIFY(legacy.open(QIODevice::ReadOnly));
            const auto sourcePath = transcribe::utf8_path(replacement.toUtf8().toStdString());
            const auto destinationPath = transcribe::utf8_path(target.toUtf8().toStdString());
            QVERIFY(!MoveFileExW(sourcePath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING));
            const auto sharingError = GetLastError();
            QVERIFY(sharingError == ERROR_SHARING_VIOLATION || sharingError == ERROR_ACCESS_DENIED); legacy.close();
#endif
            transcribe::SharedReader held(transcribe::utf8_path(target.toUtf8().toStdString()));
            transcribe::replace_file(transcribe::utf8_path(replacement.toUtf8().toStdString()), transcribe::utf8_path(target.toUtf8().toStdString()));
            QCOMPARE(held.read(0, 100), std::string("previous"));
            QCOMPARE(readSmallFile(target, 100).value(), QByteArray("published"));
            QVERIFY(!readSmallFile(target, 2));
        }
    }
    void confirmedSuccessAndSplitUtf8_data() { QTest::addColumn<QString>("mode"); QTest::newRow("normal") << "normal"; QTest::newRow("split") << "split_utf8"; }
    void confirmedSuccessAndSplitUtf8() {
        QFETCH(QString, mode); qputenv("TRANSCRIBE_TEST_MODE", mode.toUtf8()); Fixture fixture;
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QVERIFY(backend.busy());
        QTRY_COMPARE_WITH_TIMEOUT(backend.progress(), 1.0, 3000);
        QVERIFY(backend.busy()); // 100% recognition is not completion.
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Готово")); QVERIFY(backend.error().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(backend.history()->rowCount() == 1, 3000);
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QVERIFY(backend.busy()); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Готово"));
    }
    void unconfirmedCompletion_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"exit_only", "missing_manifest", "wrong_version", "bad_json"}) QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void unconfirmedCompletion() {
        QFETCH(QString, mode); qputenv("TRANSCRIBE_TEST_MODE", mode.toUtf8()); Fixture fixture;
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Ошибка")); QVERIFY(!backend.error().isEmpty());
    }
    void cancellationPreservesPartial() {
        qputenv("TRANSCRIBE_TEST_MODE", "cancel"); Fixture fixture;
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.resultDirectory().isEmpty(), 3000); backend.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Прервано"));
        QVERIFY(QFile::exists(backend.resultDirectory() + "/transcripts/transcript.txt"));
        QVERIFY(QDir(backend.resultDirectory() + "/audio").exists());
    }
    void invalidInputDoesNotLaunch() {
        Fixture fixture; Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        backend.start("", fixture.options()); QVERIFY(!backend.busy()); QVERIFY(!backend.error().isEmpty());
        QVERIFY(!QDir(fixture.output).exists());
    }
#ifdef Q_OS_WIN
    void longOutputFailsPreflight() {
        Fixture fixture; Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        auto options = fixture.options(); const auto output = fixture.temp.path() + '/' + QString(175, 'x');
        options.insert("output", output);
        backend.start(fixture.temp.path() + "/input.wav", options);
        QVERIFY(!backend.busy()); QVERIFY(backend.error().contains("169")); QVERIFY(!QDir(output).exists());
    }
#endif
    void historyLegacyAndMissing() {
        QTemporaryDir temp; put(temp.path() + "/old/source.txt", "Статус: completed\nМодель: medium\n");
        const auto rows = HistoryModel::scan({temp.path()}, {temp.path() + "/missing"});
        QCOMPARE(rows.size(), 2);
        bool legacy = false, missing = false;
        for (const auto& row : rows) { legacy |= row.status == "completed" && row.model == "medium"; missing |= !row.available; }
        QVERIFY(legacy && missing);
    }
    void modelChecksumAndCancellation() {
        QTemporaryDir temp; const auto path = temp.path() + "/model"; put(path, "hello");
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        const auto digest = QCryptographicHash::hash("hello", QCryptographicHash::Sha256).toHex();
        QVERIFY(ModelManager::verify(path, digest, cancel)); QVERIFY(!ModelManager::verify(path, QByteArray(64, '0'), cancel));
        cancel->store(true); QVERIFY_THROWS_EXCEPTION(std::runtime_error, ModelManager::verify(path, digest, cancel));
    }
    void downloadResumeAndIgnoredRange_data() {
        QTest::addColumn<bool>("ignored"); QTest::newRow("206 resume") << false; QTest::newRow("200 restart") << true;
    }
    void downloadResumeAndIgnoredRange() {
        QFETCH(bool, ignored); QTemporaryDir temp; ModelServer server; server.ignoreRange = ignored;
        put(temp.path() + "/models/ggml-medium.bin.part", server.payload.left(600));
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QCOMPARE(server.requests, 2);
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        for (const auto& record : server.records()) QVERIFY(ModelManager::verify(temp.path() + "/models/" + record.filename, record.sha256, cancel));
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 2); QCOMPARE(server.requests, 2); // Both installed hashes were verified locally.
    }
    void completedPartPublishesWithoutNetwork() {
        QTemporaryDir temp; ModelServer server;
        for (const auto& record : server.records()) put(temp.path() + "/models/" + record.filename + ".part", server.payload);
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QCOMPARE(server.requests, 0);
    }
    void corruptDownloadDoesNotPublish() {
        QTemporaryDir temp; ModelServer server; server.corrupt = true;
        ModelManager manager(temp.path(), server.records()); QSignalSpy failed(&manager, &ModelManager::failed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(failed.count(), 1); QVERIFY(!QFile::exists(temp.path() + "/models/ggml-medium.bin"));
        QVERIFY(!QFile::exists(temp.path() + "/default-model"));
    }
    void cancelledDownloadResumesAndPreservesInstalled() {
        QTemporaryDir temp; ModelServer server; server.stall = true;
        put(temp.path() + "/models/ggml-vad.bin", server.payload);
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests, 1, 5000); QTest::qWait(150); manager.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(QFileInfo(temp.path() + "/models/ggml-medium.bin.part").size() > 0);
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(ModelManager::verify(temp.path() + "/models/ggml-vad.bin", server.records()[1].sha256, cancel));
        server.stall = false; manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1);
    }
    void cancelledLockWaitDoesNotBlockEventLoop() {
        QTemporaryDir temp; ModelServer server;
        transcribe::FileLock lock(transcribe::utf8_path((temp.path() + "/.install.lock").toStdString()));
        ModelManager manager(temp.path(), server.records()); QSignalSpy failed(&manager, &ModelManager::failed);
        manager.download("medium");
        bool responsive = false;
        QTimer::singleShot(30, &manager, [&] { responsive = true; manager.cancel(); });
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(responsive); QCOMPARE(failed.count(), 1); QCOMPARE(server.requests, 0);
        QVERIFY(failed.front().front().toString().contains(QStringLiteral("отменена")));
    }
    void importVerifiesBeforeReplacing() {
        QTemporaryDir temp; ModelServer server;
        const auto target = temp.path() + "/models/ggml-medium.bin";
        const auto source = temp.path() + "/import.bin";
        put(target, server.payload); put(source, "wrong model");
        ModelManager manager(temp.path(), server.records());
        QSignalSpy failed(&manager, &ModelManager::failed), installed(&manager, &ModelManager::installed);
        manager.importModel(source, "medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(failed.count(), 1); QCOMPARE(installed.count(), 0);
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(ModelManager::verify(target, server.records()[0].sha256, cancel));
        put(source, server.payload);
        manager.importModel(source, "medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QVERIFY(ModelManager::verify(target, server.records()[0].sha256, cancel));
        QFile selected(temp.path() + "/default-model"); QVERIFY(selected.open(QIODevice::ReadOnly));
        QCOMPARE(selected.readAll(), QByteArray("medium\n"));
        QCOMPARE(server.requests, 0);
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--machine")) return helper(app.arguments());
    GuiTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_gui.moc"
