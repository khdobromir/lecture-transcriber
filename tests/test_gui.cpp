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
#include <QSemaphore>
#include <QElapsedTimer>
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
    const auto send = [&](QJsonObject object) {
        object.insert("protocol", mode == "wrong_version" ? 2 : 1);
        if (object.value("type") != "hello") {
            for (const auto* key : {"stage", "message", "result"}) if (!object.contains(key)) object.insert(key, "");
        }
        std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush;
    };
    send({{"type", "hello"}});
    if (mode == "bad_json") { std::cout << "not JSON\n" << std::flush; return 1; }
    send({{"type", "result"}, {"result", root}});
    send({{"type", "stage"}, {"stage", "recognize"}, {"result", root}});
    if (mode == "unknown_type") send({{"type", "unexpected"}, {"result", root}});
    if (mode == "duplicate_hello") send({{"type", "hello"}});
    if (mode == "bad_counts") send({{"type", "progress"}, {"stage", "recognize"}, {"fraction", 0.5}, {"finished", 2}, {"total", 1}, {"result", root}, {"eta_seconds", QJsonValue::Null}});
    if (mode == "long_line") { std::cout << std::string(transcribe::max_event_line + 1, 'x') << '\n' << std::flush; return 1; }
    if (mode == "many_lines") {
        for (int i = 0; i < 3000; ++i) send({{"type", "warning"}, {"result", root}, {"message", QString(128, 'w')}});
    }
    send({{"type", "progress"}, {"stage", "recognize"}, {"fraction", 1.0}, {"finished", 1}, {"total", 1}, {"result", root}, {"eta_seconds", QJsonValue::Null}});
    if (mode == "exit_only" || mode == "wrong_version") return 0;
    if (mode == "cancel") {
        put(root + "/transcripts/transcript.txt", "частичный текст\n");
        std::string command; std::getline(std::cin, command);
        send({{"type", "failed"}, {"result", root}, {"status", "interrupted"}, {"code", 130}, {"message", "Отменено"}});
        return 130;
    }
        const auto release = qEnvironmentVariable("TRANSCRIBE_TEST_RELEASE");
    if (!release.isEmpty()) {
        QElapsedTimer deadline; deadline.start();
        while (!QFile::exists(release)) { if (deadline.elapsed() > 5000) return 1; QThread::msleep(1); }
    }
    if (mode != "missing_manifest") {
        put(root + "/result.json", R"({"version":1,"status":"completed","code":0,"model":"medium"})");
        for (const auto* extension : {"txt", "srt", "vtt"}) put(root + "/transcripts/transcript." + extension, "текст\n");
    }
    if (mode == "split_utf8") {
        const auto bytes = QJsonDocument(QJsonObject{{"protocol", 1}, {"type", "completed"}, {"stage", ""}, {"message", ""}, {"result", root}, {"status", "completed"}, {"code", 0}}).toJson(QJsonDocument::Compact) + '\n';
        const auto cut = bytes.indexOf(QString("Л").toUtf8()) + 1;
        std::cout.write(bytes.data(), cut); std::cout.flush(); QTest::qSleep(30);
        std::cout.write(bytes.data() + cut, bytes.size() - cut); std::cout.flush();
    } else send({{"type", mode == "contradictory_terminal" ? "failed" : "completed"}, {"result", root}, {"status", "completed"}, {"code", 0}});
    if (mode == "after_terminal") send({{"type", "warning"}, {"message", "late event"}, {"result", root}});
    if (mode == "truncated_line") std::cout << "{\"protocol\":1" << std::flush;
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
        const auto release = fixture.temp.path() + "/release";
        qputenv("TRANSCRIBE_TEST_RELEASE", release.toUtf8());
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        qunsetenv("TRANSCRIBE_TEST_RELEASE");
        QVERIFY(backend.busy());
        QTRY_COMPARE_WITH_TIMEOUT(backend.progress(), 1.0, 3000);
        QVERIFY(backend.busy()); // Barrier holds publication after 100% recognition.
        put(release, "ready");
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Готово")); QVERIFY(backend.error().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(backend.history()->rowCount() == 1, 3000);
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QVERIFY(backend.busy()); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000);
        QCOMPARE(backend.status(), QString("Готово"));
    }
    void unconfirmedCompletion_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"exit_only", "missing_manifest", "wrong_version", "bad_json", "unknown_type", "duplicate_hello", "bad_counts", "contradictory_terminal", "after_terminal", "long_line", "truncated_line"}) QTest::newRow(mode) << QString::fromLatin1(mode);
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
    void manyValidLinesRemainResponsive() {
        qputenv("TRANSCRIBE_TEST_MODE", "many_lines"); Fixture fixture;
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        int turns = 0; QTimer pulse; pulse.setInterval(0); connect(&pulse, &QTimer::timeout, this, [&] { ++turns; }); pulse.start();
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000); pulse.stop();
        QCOMPARE(backend.status(), QString("Готово")); QVERIFY(turns > 1);
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
    void stateTransitionGuards() {
        QVERIFY(transitionAllowed(TaskState::idle, TaskState::starting));
        QVERIFY(!transitionAllowed(TaskState::failed, TaskState::completed));
        QVERIFY(!transitionAllowed(TaskState::completed, TaskState::cancel_requested));
        QVERIFY(transitionAllowed(TaskState::cancel_requested, TaskState::finalizing));
        QVERIFY(transitionAllowed(ModelState::cancelling, ModelState::ready));
        QVERIFY(!transitionAllowed(ModelState::ready, ModelState::downloading));
        QVERIFY(!transitionAllowed(ModelState::cancelling, ModelState::downloading));
    }
    void preflightFailurePreservesSelectedResultAndRetries() {
        qputenv("TRANSCRIBE_TEST_MODE", "normal"); Fixture fixture;
        const auto saved = fixture.output + "/saved";
        put(saved + "/result.json", R"({"version":1,"status":"completed"})");
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root});
        QTRY_COMPARE(backend.history()->rowCount(), 1); backend.viewResult(saved);
        QCOMPARE(backend.selectedStatus(), QString("Готово"));
        backend.start("", fixture.options());
        QCOMPARE(backend.state(), TaskState::failed); QCOMPARE(backend.resultDirectory(), saved);
        QCOMPARE(backend.selectedStatus(), QString("Готово")); QVERIFY(backend.taskResultDirectory().isEmpty());
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000); QCOMPARE(backend.state(), TaskState::completed);
    }
    void failedBackendStartCanRetry() {
        qputenv("TRANSCRIBE_TEST_MODE", "normal"); Fixture fixture;
        const auto binary = fixture.temp.path() + "/backend"
#ifdef Q_OS_WIN
            ".exe"
#endif
            ;
        Backend backend({binary, fixture.ini, fixture.root});
        backend.start(fixture.temp.path() + "/input.wav", fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000); QCOMPARE(backend.state(), TaskState::failed); QVERIFY(backend.canRetry());
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), binary));
#ifdef Q_OS_WIN
        // The copied native Qt helper resolves its runtime beside the original SDK app.
        const auto oldPath = qgetenv("PATH");
        qputenv("PATH", QFileInfo(QCoreApplication::applicationFilePath()).absolutePath().toUtf8() + ';' + oldPath);
#endif
        backend.retry(); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 3000); QCOMPARE(backend.state(), TaskState::completed);
#ifdef Q_OS_WIN
        qputenv("PATH", oldPath);
#endif
    }
    void stalePreviewCannotChangeReselectedResult() {
        Fixture fixture; auto ready = std::make_shared<std::atomic<bool>>(false);
        auto release = std::make_shared<QSemaphore>(); auto calls = std::make_shared<std::atomic<int>>(0);
        Backend::Readers readers;
        readers.preview = [ready, release, calls](const QString&, const Backend::Cancel& cancel) {
            if (calls->fetch_add(1) == 0) {
                ready->store(true);
                while (!release->tryAcquire(1, 10) && !cancel->load()) {}
                return QString("stale A");
            }
            return QString("fresh A");
        };
        Backend backend({"missing", fixture.ini, fixture.root}, readers);
        backend.viewResult("A"); QTRY_VERIFY(ready->load());
        backend.viewResult("B"); backend.viewResult("A"); release->release();
        QTRY_COMPARE(backend.transcript(), QString("fresh A")); QCOMPARE(backend.resultDirectory(), QString("A"));
    }
    void cancellationAfterCommitKeepsCompletedDuringVerification() {
        qputenv("TRANSCRIBE_TEST_MODE", "normal"); Fixture fixture;
        auto ready = std::make_shared<std::atomic<bool>>(false), release = std::make_shared<std::atomic<bool>>(false);
        Backend::Readers readers;
        readers.completion = [ready, release](const QString& directory, const Backend::Cancel& cancel) {
            if (!QFile::exists(directory + "/result.json")) throw std::runtime_error("missing committed manifest");
            ready->store(true);
            while (!release->load() && !cancel->load()) QThread::msleep(1);
            return QString{};
        };
        Backend backend({QCoreApplication::applicationFilePath(), fixture.ini, fixture.root}, readers);
        backend.start(fixture.temp.path() + "/input.wav", fixture.options()); QTRY_VERIFY(ready->load());
        QCOMPARE(backend.state(), TaskState::finalizing); QVERIFY(!backend.canCancel());
        backend.cancel(); QCOMPARE(backend.state(), TaskState::finalizing);
        release->store(true); QTRY_VERIFY(!backend.busy()); QCOMPARE(backend.state(), TaskState::completed);
    }
    void teardownCancelsPreviewAndHistoryWorkers() {
        Fixture fixture; auto previews = std::make_shared<std::atomic<bool>>(false), histories = std::make_shared<std::atomic<bool>>(false);
        Backend::Readers readers;
        readers.preview = [previews](const QString&, const Backend::Cancel& cancel) -> QString {
            previews->store(true); while (!cancel->load()) QThread::msleep(1); throw std::runtime_error("preview teardown");
        };
        readers.history = [histories](const QStringList&, const QStringList&, const QVariantMap&, const Backend::Cancel& cancel) -> QVector<HistoryRow> {
            histories->store(true); while (!cancel->load()) QThread::msleep(1); throw std::runtime_error("history teardown");
        };
        auto backend = std::make_unique<Backend>(Backend::Paths{"missing", fixture.ini, fixture.root}, readers);
        backend->viewResult("A"); QTRY_VERIFY(previews->load() && histories->load());
        QElapsedTimer elapsed; elapsed.start(); backend.reset(); QVERIFY(elapsed.elapsed() < 1000);
    }
    void teardownCancelsCompletionVerification() {
        qputenv("TRANSCRIBE_TEST_MODE", "normal"); Fixture fixture; auto ready = std::make_shared<std::atomic<bool>>(false);
        Backend::Readers readers;
        readers.completion = [ready](const QString&, const Backend::Cancel& cancel) -> QString {
            ready->store(true); while (!cancel->load()) QThread::msleep(1); throw std::runtime_error("completion teardown");
        };
        auto backend = std::make_unique<Backend>(Backend::Paths{QCoreApplication::applicationFilePath(), fixture.ini, fixture.root}, readers);
        backend->start(fixture.temp.path() + "/input.wav", fixture.options()); QTRY_VERIFY(ready->load());
        QElapsedTimer elapsed; elapsed.start(); backend.reset(); QVERIFY(elapsed.elapsed() < 1000);
    }
    void outdatedHistoryScanCannotPublishRows() {
        Fixture fixture; auto calls = std::make_shared<std::atomic<int>>(0), ready = std::make_shared<std::atomic<int>>(0);
        Backend::Readers readers;
        readers.history = [calls, ready](const QStringList&, const QStringList&, const QVariantMap&, const Backend::Cancel& cancel) {
            if (calls->fetch_add(1) == 0) { ready->store(1); while (!cancel->load()) QThread::msleep(1); return QVector<HistoryRow>{{"obsolete", "completed", "medium", "obsolete", "", false}}; }
            return QVector<HistoryRow>{{"current", "completed", "medium", "current", "", false}};
        };
        Backend backend({"missing", fixture.ini, fixture.root}, readers); QTRY_COMPARE(ready->load(), 1);
        backend.refreshHistory(); backend.refreshHistory();
        QTRY_COMPARE(backend.history()->rowCount(), 1);
        QCOMPARE(backend.history()->data(backend.history()->index(0), HistoryModel::Directory).toString(), QString("current"));
    }
    void workerFailureHasItsOwnContext() {
        Fixture fixture; Backend::Readers readers;
        readers.preview = [](const QString&, const Backend::Cancel&) -> QString { throw std::runtime_error("preview failure"); };
        readers.history = [](const QStringList&, const QStringList&, const QVariantMap&, const Backend::Cancel&) -> QVector<HistoryRow> { throw std::runtime_error("history failure"); };
        Backend backend({"missing", fixture.ini, fixture.root}, readers); backend.viewResult("A");
        QTRY_VERIFY(!backend.previewError().isEmpty()); QTRY_VERIFY(!backend.historyError().isEmpty()); QVERIFY(backend.error().isEmpty());
    }
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
    void corruptInstalledModelRequiresExplicitBackupRecovery() {
        QTemporaryDir temp; ModelServer server; const auto target = temp.path() + "/models/ggml-medium.bin";
        put(target, "damaged installed model");
        ModelManager manager(temp.path(), server.records()); QSignalSpy installed(&manager, &ModelManager::installed);
        manager.download("medium"); QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.state(), ModelState::failed); QVERIFY(manager.canRecover()); QCOMPARE(server.requests, 0);
        server.corrupt = true; manager.recover(); QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.state(), ModelState::failed); QVERIFY(QFile::exists(manager.backupPath()));
        const auto original = readSmallFile(target, 100), backup = readSmallFile(manager.backupPath(), 100);
        if (!original || !backup) QFAIL("recovery did not preserve the original and backup");
        QCOMPARE(*original, QByteArray("damaged installed model"));
        QCOMPARE(*backup, QByteArray("damaged installed model"));
        // Failed recovery preserves the original; another explicit attempt verifies it again.
        server.corrupt = false; manager.download("medium"); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.canRecover());
        manager.recover(); QTRY_VERIFY(!manager.busy()); QCOMPARE(manager.state(), ModelState::ready); QCOMPARE(installed.count(), 1);
    }
    void cancelHashAndPublicationCanRetry_data() {
        QTest::addColumn<ModelState>("phase"); QTest::newRow("hash") << ModelState::verifying; QTest::newRow("publish") << ModelState::publishing;
    }
    void cancelHashAndPublicationCanRetry() {
        QFETCH(ModelState, phase); QTemporaryDir temp; ModelServer server;
        for (const auto& record : server.records()) put(temp.path() + "/models/" + record.filename + ".part", server.payload);
        ModelManager manager(temp.path(), server.records()); bool cancelled = false;
        auto connection = connect(&manager, &ModelManager::changed, this, [&] {
            if (!cancelled && manager.state() == phase) { cancelled = true; manager.cancel(); }
        });
        manager.download("medium"); QTRY_VERIFY(!manager.busy()); QVERIFY(cancelled); QCOMPARE(manager.state(), ModelState::cancelled);
        disconnect(connection); manager.download("medium"); QTRY_VERIFY(!manager.busy()); QCOMPARE(manager.state(), ModelState::ready);
    }
    void lateModelCancelKeepsCommittedInstallation() {
        QTemporaryDir temp; ModelServer server; const auto source = temp.path() + "/import.bin"; put(source, server.payload);
        auto ready = std::make_shared<std::atomic<bool>>(false); auto release = std::make_shared<QSemaphore>();
        ModelManager::Workers workers{[ready, release] { ready->store(true); if (!release->tryAcquire(1, 5000)) throw std::runtime_error("commit barrier timeout"); }};
        ModelManager manager(temp.path(), server.records(), workers);
        manager.importModel(source, "medium"); QTRY_VERIFY(ready->load()); manager.cancel(); QCOMPARE(manager.state(), ModelState::cancelling);
        release->release(); QTRY_VERIFY(!manager.busy()); QCOMPARE(manager.state(), ModelState::ready);
        unsigned checkpoints = 0;
        transcribe::FileLock lock(transcribe::utf8_path((temp.path() + "/.install.lock").toStdString()), [&] { if (++checkpoints > 1) throw std::runtime_error("installation lock held after commit"); });
        QCOMPARE(checkpoints, 1U);
    }
    void teardownCancelsActiveModelHashAndImport() {
        QTemporaryDir temp; ModelServer server; const auto source = temp.path() + "/models/ggml-medium.bin"; put(source, server.payload);
        for (bool importing : {false, true}) {
            auto ready = std::make_shared<std::atomic<bool>>(false); ModelManager::Workers workers;
            workers.hashCheckpoint = [ready](const ModelManager::Cancel& cancel) { ready->store(true); while (!cancel->load()) QThread::msleep(1); };
            auto manager = std::make_unique<ModelManager>(temp.path(), server.records(), workers);
            if (importing) manager->importModel(source, "medium"); else manager->download("medium");
            QTRY_VERIFY(ready->load());
            QElapsedTimer elapsed; elapsed.start(); manager.reset(); QVERIFY(elapsed.elapsed() < 1000);
            const auto preserved = readSmallFile(source, 8192); if (!preserved) QFAIL("teardown lost installed model");
            QCOMPARE(*preserved, server.payload);
        }
    }
    void teardownCancelsModelLockAndImport() {
        QTemporaryDir temp; ModelServer server;
        transcribe::FileLock lock(transcribe::utf8_path((temp.path() + "/.install.lock").toStdString()));
        for (bool importing : {false, true}) {
            auto manager = std::make_unique<ModelManager>(temp.path(), server.records());
            if (importing) manager->importModel("missing", "medium"); else manager->download("medium");
            QCOMPARE(manager->state(), ModelState::waiting_lock);
            QElapsedTimer elapsed; elapsed.start(); manager.reset(); QVERIFY(elapsed.elapsed() < 1000);
        }
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
