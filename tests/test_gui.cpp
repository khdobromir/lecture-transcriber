#include "backend.hpp"
#include "models.hpp"
#include "files.hpp"
#include "http_range.hpp"
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
    bool ignoreRange = false, corrupt = false, stall = false, always416 = false, disconnectEarly = false;
    QByteArray rangeOverride;
    QVector<qint64> offsets;
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
                    offsets.push_back(offset);
                    if (offset >= payload.size() || always416) {
                        socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + QByteArray::number(payload.size()) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost(); return;
                    }
                    const auto body = (corrupt ? QByteArray(payload.size(), 'x') : payload).mid(offset);
                    QByteArray headers = offset ? "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(offset) + '-' + QByteArray::number(payload.size() - 1) + '/' + QByteArray::number(payload.size()) + "\r\n" : QByteArray("HTTP/1.1 200 OK\r\n");
                    if (offset && !rangeOverride.isEmpty()) headers = "HTTP/1.1 206 Partial Content\r\nContent-Range: " + rangeOverride + "\r\n";
                    headers += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                    socket->write(headers + (stall || disconnectEarly ? body.left(100) : body)); socket->flush();
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
            const auto published = readSmallFile(target, 100);
            if (!published) QFAIL("published file is unreadable");
            QCOMPARE(*published, QByteArray("published"));
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
    void historyDiscoversAfterSaturationAndEvicts() {
        Fixture fixture; QStringList known;
        for (int i = 0; i < 2000; ++i) known.append(fixture.output + "/unavailable-" + QString::number(i));
        const auto newest = fixture.output + "/new-result";
        put(newest + "/result.json", R"({"version":1,"title":"new","status":"completed","created_unix_ms":4070908800000})");
        { QSettings settings(fixture.ini, QSettings::IniFormat); settings.setValue("history/directories", known); }
        {
            Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
            QTRY_COMPARE_WITH_TIMEOUT(backend.history()->rowCount(), 2000, 5000);
            QCOMPARE(backend.history()->data(backend.history()->index(0), HistoryModel::Directory).toString(), newest);
        }
        QSettings settings(fixture.ini, QSettings::IniFormat);
        QCOMPARE(settings.value("history/directories").toStringList().size(), 2000);
        QVERIFY(settings.value("history/directories").toStringList().contains(newest));
        QFile::remove(newest + "/result.json"); QDir().rmdir(newest);
        Backend restarted({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        QTRY_COMPARE_WITH_TIMEOUT(restarted.history()->rowCount(), 2000, 5000);
        QCOMPARE(restarted.history()->data(restarted.history()->index(0), HistoryModel::Directory).toString(), newest);
        QVERIFY(!restarted.history()->data(restarted.history()->index(0), HistoryModel::Available).toBool());
    }
    void historyRootsAndStableTies() {
        QTemporaryDir temp;
        const auto a = temp.path() + "/a/result", b = temp.path() + "/b/result";
        for (const auto& path : {a, b}) put(path + "/result.json", R"({"version":1,"status":"completed","created_unix_ms":1000})");
        const auto rows = HistoryModel::scan({temp.path() + "/b", temp.path() + "/a"}, {});
        QCOMPARE(rows.size(), 2); QCOMPARE(rows[0].directory, a); QCOMPARE(rows[1].directory, b);
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
        put(temp.path() + "/models/ggml-medium.bin.part.sha256", server.records().front().sha256);
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
    void invalidFullPartRecovers_data() {
        QTest::addColumn<int>("extra"); QTest::newRow("corrupt complete") << 0; QTest::newRow("oversized") << 100;
    }
    void invalidFullPartRecovers() {
        QFETCH(int, extra); QTemporaryDir temp; ModelServer server;
        const auto record = server.records().front(); const auto partial = temp.path() + "/models/" + record.filename + ".part";
        put(partial, QByteArray(server.payload.size() + extra, 'x')); put(partial + ".sha256", record.sha256);
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QCOMPARE(server.requests, 3);
        QCOMPARE(server.offsets.front(), static_cast<qint64>(server.payload.size() + extra)); QCOMPARE(server.offsets[1], qint64{0});
    }
    void invalidRangesDoNotWrite_data() {
        QTest::addColumn<QByteArray>("range");
        for (const auto* range : {"bytes 601-4095/4096", "bytes 600-99999/4096", "bytes 600-4095/*", "bytes 600-4095/4096 garbage", "bytes 600-4094/4096", "bytes 600-4095/0", "bytes 600-99999999999999999999999999/4096"}) QTest::newRow(range) << QByteArray(range);
    }
    void invalidRangesDoNotWrite() {
        QFETCH(QByteArray, range); QTemporaryDir temp; ModelServer server; server.rangeOverride = range;
        const auto record = server.records().front(); const auto partial = temp.path() + "/models/" + record.filename + ".part";
        put(partial, server.payload.left(600)); put(partial + ".sha256", record.sha256);
        ModelManager manager(temp.path(), server.records()); QSignalSpy failed(&manager, &ModelManager::failed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(failed.count(), 1); QCOMPARE(QFileInfo(partial).size(), qint64{600}); QVERIFY(!QFile::exists(partial.chopped(5)));
    }
    void recoveryIsBoundedAndRevisionBound() {
        QTemporaryDir temp; ModelServer server; server.always416 = true;
        const auto record = server.records().front(); const auto partial = temp.path() + "/models/" + record.filename + ".part";
        put(partial, QByteArray(server.payload.size(), 'x')); put(partial + ".sha256", record.sha256);
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(server.requests, 2); QCOMPARE(installed.count(), 0);
        server.always416 = false; server.requests = 0; server.offsets.clear();
        put(partial, server.payload.left(600)); put(partial + ".sha256", QByteArray(64, '0'));
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QCOMPARE(server.offsets.front(), qint64{0});
        unsigned checks = 0;
        transcribe::FileLock released(transcribe::utf8_path((temp.path() + "/.install.lock").toStdString()), [&] {
            if (++checks > 1) throw std::runtime_error("installation lock remained held after ready");
        });
        QCOMPARE(checks, 1U);
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
        QTRY_COMPARE_WITH_TIMEOUT(server.requests, 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(temp.path() + "/models/ggml-medium.bin.part").size() > 0, 5000); manager.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(QFileInfo(temp.path() + "/models/ggml-medium.bin.part").size() > 0);
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(ModelManager::verify(temp.path() + "/models/ggml-vad.bin", server.records()[1].sha256, cancel));
        server.stall = false; manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1);
    }
    void disconnectedDownloadPreservesPartialAndRetries() {
        QTemporaryDir temp; ModelServer server; server.disconnectEarly = true;
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        const auto partial = temp.path() + "/models/ggml-medium.bin.part";
        QCOMPARE(QFileInfo(partial).size(), qint64{100}); QCOMPARE(installed.count(), 0);
        server.disconnectEarly = false; manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 1); QCOMPARE(server.offsets[1], qint64{100});
    }
    void rangeParserProperties() {
        for (qint64 total = 1; total <= 256; ++total) {
            const auto begin = total / 2;
            const auto header = "bytes " + QByteArray::number(begin) + '-' + QByteArray::number(total - 1) + '/' + QByteArray::number(total);
            const auto range = ContentRange::parse(header);
            if (!range) QFAIL("valid range rejected");
            QVERIFY(!range->unsatisfied);
            QCOMPARE(range->begin, begin); QCOMPARE(range->end, total - 1); QCOMPARE(range->total, total);
            QVERIFY(!ContentRange::parse(header + "\n")); QVERIFY(!ContentRange::parse(header + " extra"));
            const auto missing = ContentRange::parse("bytes */" + QByteArray::number(total)); QVERIFY(missing && missing->unsatisfied);
        }
    }
    void writeErrorPreservesInstalledAndAllowsRetry() {
        QTemporaryDir temp; ModelServer server;
        const auto installed = temp.path() + "/models/ggml-vad.bin"; put(installed, server.payload);
        QDir().mkpath(temp.path() + "/models/ggml-medium.bin.part.sha256");
        ModelManager manager(temp.path(), server.records()); QSignalSpy ready(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(ready.count(), 0); QCOMPARE(server.requests, 0);
        auto cancel = std::make_shared<std::atomic<bool>>(false); QVERIFY(ModelManager::verify(installed, server.records()[1].sha256, cancel));
        QDir().rmdir(temp.path() + "/models/ggml-medium.bin.part.sha256");
        manager.download("medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000); QCOMPARE(ready.count(), 1);
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
        selected.close(); manager.importModel(target, "medium"); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QCOMPARE(installed.count(), 2); QVERIFY(ModelManager::verify(target, server.records()[0].sha256, cancel));
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--machine")) return helper(app.arguments());
    GuiTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_gui.moc"
