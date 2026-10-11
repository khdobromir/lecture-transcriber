// Actual GUI controller -> actual CLI; only the external tools are substituted.
#include "backend.hpp"
#include "files.hpp"
#include "windows.hpp"
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#ifndef Q_OS_WIN
#include <cerrno>
#include <csignal>
#endif
namespace {
void put(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) qFatal("integration fixture directory failed");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("integration fixture write failed");
}
struct Environment {
    QByteArray name, previous; bool existed;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): environment key precedes its value, matching qputenv.
    Environment(QByteArray key, const QByteArray& value) : name(std::move(key)), previous(qEnvironmentVariable(name).toUtf8()), existed(qEnvironmentVariableIsSet(name)) { set(value); }
    void set(const QByteArray& value) const {
#ifdef Q_OS_WIN
        // qputenv uses the ANSI CRT API; fixture paths must retain emoji in UTF-16.
        const auto key = transcribe::wide_utf8(name.toStdString()), text = transcribe::wide_utf8(value.toStdString());
        // Keep the CRT view used by Qt and the OS view inherited by children in sync.
        if (_wputenv_s(key.c_str(), text.c_str()) != 0 || !SetEnvironmentVariableW(key.c_str(), text.c_str()))
            qFatal("fixture environment failed");
#else
        if (!qputenv(name, value)) qFatal("fixture environment failed");
#endif
    }
    ~Environment() {
        if (existed) set(previous);
        else {
#ifdef Q_OS_WIN
            const auto key = transcribe::wide_utf8(name.toStdString());
            if (_wputenv_s(key.c_str(), L"") != 0 || !SetEnvironmentVariableW(key.c_str(), nullptr))
                qFatal("fixture environment restore failed");
#else
            qunsetenv(name);
#endif
        }
    }
};
QString suffix() {
#ifdef Q_OS_WIN
    return ".exe";
#else
    return {};
#endif
}
struct Fixture {
    QTemporaryDir temp;
    QString directory = temp.path() + "/Лекция 😀 с пробелами", binary = directory + "/bin/transcribe" + suffix();
    QString root = directory + "/data", output = directory + "/results", ini = directory + "/settings.ini";
    QString audio = directory + "/Запись 😀.wav", barrier = directory + "/barrier";
    Environment path{"PATH", (directory + "/bin/tools").toUtf8() +
#ifdef Q_OS_WIN
        ';'
#else
        ':'
#endif
        + qgetenv("PATH")};
    Environment barrierEnv{"TRANSCRIBE_MOCK_BARRIER", barrier.toUtf8()};
    Environment audioEnv{"TRANSCRIBE_MOCK_AUDIO", audio.toUtf8()};
    Fixture() {
        const auto copy = [](const QString& source, const QString& target) {
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (!QFile::copy(source, target)) qFatal("integration fixture executable copy failed");
            QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        };
        copy(TRANSCRIBE_BINARY, binary);
        for (const auto* name : {"ffmpeg", "yt-dlp"}) copy(MOCK_BINARY, directory + "/bin/tools/" + name + suffix());
        copy(MOCK_BINARY, root + "/whisper.cpp/build/bin/whisper-cli" + suffix());
        put(root + "/models/ggml-medium-q5_0.bin", "fixture"); put(root + "/models/ggml-silero-v6.2.0.bin", "fixture");
        QByteArray wave; QDataStream stream(&wave, QIODevice::WriteOnly); stream.setByteOrder(QDataStream::LittleEndian);
        stream.writeRawData("RIFF", 4); stream << quint32{64036}; stream.writeRawData("WAVEfmt ", 8);
        stream << quint32{16} << quint16{1} << quint16{1} << quint32{16000} << quint32{32000} << quint16{2} << quint16{16};
        stream.writeRawData("data", 4); stream << quint32{64000}; wave += QByteArray(64000, '\1'); put(audio, wave);
        QSettings settings(ini, QSettings::IniFormat); settings.setValue("output", output); settings.sync();
    }
    Backend::Paths paths() const { return {binary, ini, root}; }
    QVariantMap options() const { return {{"output", output}, {"chunks", 2}, {"jobs", 2}}; }
    QStringList ready() const { return QDir(directory).entryList({"barrier.*.ready"}, QDir::Files); }
    void release() const { put(barrier + ".release", "ready"); }
    unsigned long workerPid(const QString& name) const {
        const auto bytes = readSmallFile(directory + '/' + name, 64);
        if (!bytes) qFatal("missing ready pid");
        bool ok = false; const auto pid = bytes->trimmed().toULong(&ok);
        if (!ok) qFatal("invalid ready pid");
        return pid;
    }
};
bool dead(unsigned long pid) {
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER;
    const bool stopped = WaitForSingleObject(process, 0) == WAIT_OBJECT_0; CloseHandle(process); return stopped;
#else
    return kill(static_cast<pid_t>(pid), 0) < 0 && errno == ESRCH;
#endif
}
QJsonObject manifest(const QString& directory) {
    const auto bytes = readSmallFile(directory + "/result.json", 65536);
    return bytes ? QJsonDocument::fromJson(*bytes).object() : QJsonObject{};
}
}
class IntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void fixtureEnvironmentPreservesUnicode() {
        Environment value{"TRANSCRIBE_INTEGRATION_UNICODE", QString("Лекция 😀 с пробелами").toUtf8()};
        QCOMPARE(qEnvironmentVariable("TRANSCRIBE_INTEGRATION_UNICODE"), QString("Лекция 😀 с пробелами"));
        QCOMPARE(transcribe::environment_utf8("TRANSCRIBE_INTEGRATION_UNICODE"), QString("Лекция 😀 с пробелами").toUtf8().toStdString());
    }
    void urlResultDirectoryTransitionAndCacheReuse() {
        Fixture fixture; Backend backend(fixture.paths()); fixture.release(); auto options = fixture.options();
        options.insert("cacheDirectory", fixture.directory + "/cache");
        QStringList announced;
        connect(&backend, &Backend::changed, this, [&] {
            const auto directory = backend.taskResultDirectory(); if (!directory.isEmpty() && (announced.isEmpty() || announced.back() != directory)) announced.append(directory);
        });
        backend.start("https://vkvideo.ru/video-1_2", options); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.state(), TaskState::completed); QVERIFY(announced.size() >= 2);
        QVERIFY(!QDir(announced.first()).exists()); QVERIFY(QDir(announced.last()).exists());
        QCOMPARE(manifest(announced.last()).value("status").toString(), QString("completed"));
        backend.start("https://vkvideo.ru/video-1_2", options); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.state(), TaskState::completed); QTRY_COMPARE(backend.history()->rowCount(), 2);
    }
    void successPublishesPreviewAndOrderedExports() {
        Fixture fixture; Backend backend(fixture.paths()); backend.start(fixture.audio, fixture.options());
        QTRY_COMPARE_WITH_TIMEOUT(fixture.ready().size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.transcript().contains("Текст 😀"), 5000); QVERIFY(backend.busy());
        const auto directory = backend.taskResultDirectory(); fixture.release();
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000); QCOMPARE(backend.state(), TaskState::completed); QVERIFY(backend.error().isEmpty());
        QCOMPARE(manifest(directory).value("status").toString(), QString("completed"));
        for (const auto* extension : {"txt", "srt", "vtt"}) {
            const auto bytes = readSmallFile(directory + "/transcripts/transcript." + extension, 65536);
            if (!bytes) QFAIL("missing GUI/CLI export");
            const auto text = QString::fromUtf8(*bytes); QVERIFY(text.contains("Текст 😀 1")); QVERIFY(text.contains("Текст 😀 2"));
            QVERIFY(text.indexOf("Текст 😀 1") < text.indexOf("Текст 😀 2"));
            if (QString::fromLatin1(extension) == "srt") QVERIFY(text.contains("00:00:01,000"));
            if (QString::fromLatin1(extension) == "vtt") QVERIFY(text.startsWith("WEBVTT"));
        }
        QTRY_COMPARE(backend.history()->rowCount(), 1);
        Backend restarted(fixture.paths()); QTRY_COMPARE(restarted.history()->rowCount(), 1);
        restarted.viewResult(directory); QTRY_VERIFY(restarted.transcript().contains("Текст 😀 2")); QCOMPARE(restarted.selectedStatus(), QString("Готово"));
        for (const auto& name : fixture.ready()) QTRY_VERIFY_WITH_TIMEOUT(dead(fixture.workerPid(name)), 3000);
    }
    void acceptedCancellationRetainsPartialAndCanRetry() {
        Fixture fixture; Backend backend(fixture.paths()); backend.start(fixture.audio, fixture.options());
        QTRY_COMPARE_WITH_TIMEOUT(fixture.ready().size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(backend.transcript().contains("Текст 😀"), 5000);
        const auto first = backend.taskResultDirectory(); const auto workers = fixture.ready(); backend.cancel();
        QCOMPARE(backend.state(), TaskState::cancel_requested); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.state(), TaskState::interrupted); QCOMPARE(manifest(first).value("status").toString(), QString("interrupted"));
        QVERIFY(QFile::exists(first + "/transcripts/transcript.txt")); QVERIFY(QDir(first + "/audio").exists());
        for (const auto& name : workers) QTRY_VERIFY_WITH_TIMEOUT(dead(fixture.workerPid(name)), 3000);
        QVERIFY(backend.canRetry()); fixture.release(); backend.retry(); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000);
        QCOMPARE(backend.state(), TaskState::completed); QVERIFY(backend.taskResultDirectory() != first);
        QTRY_COMPARE(backend.history()->rowCount(), 2);
    }
    void toolFailureRetainsOldResultAndCanRetry() {
        Fixture fixture; Backend backend(fixture.paths()); fixture.release(); backend.start(fixture.audio, fixture.options());
        QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000); QCOMPARE(backend.state(), TaskState::completed);
        const auto saved = backend.taskResultDirectory();
        {
            Environment failure{"TRANSCRIBE_MOCK_FAIL", "17"};
            backend.start(fixture.audio, fixture.options()); backend.viewResult(saved);
            QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000); QCOMPARE(backend.state(), TaskState::failed);
            QCOMPARE(backend.resultDirectory(), saved); QCOMPARE(backend.selectedStatus(), QString("Готово"));
            const auto failed = manifest(backend.taskResultDirectory()); QCOMPARE(failed.value("status").toString(), QString("failed")); QCOMPARE(failed.value("code").toInt(), 17);
        }
        backend.retry(); QTRY_VERIFY_WITH_TIMEOUT(!backend.busy(), 5000); QCOMPARE(backend.state(), TaskState::completed);
    }
};
int main(int argc, char** argv) { QCoreApplication app(argc, argv); IntegrationTests tests; return QTest::qExec(&tests, argc, argv); }
#include "test_gui_integration.moc"
