#include "backend.hpp"
#include "cli.hpp"
#include "platform.hpp"
#include "result.hpp"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

namespace {
QString pathText(const std::filesystem::path& path) { return QString::fromStdString(transcribe::path_utf8(path)); }
QString backendBinary() {
    auto path = QCoreApplication::applicationDirPath() + "/transcribe";
#ifdef Q_OS_WIN
    path += ".exe";
#endif
    return path;
}
QString settingsFile() {
    const auto override = qEnvironmentVariable("TRANSCRIBE_GUI_SETTINGS_FILE");
    if (!override.isEmpty()) return QFileInfo(override).absoluteFilePath();
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
}
QString verifyCompletion(const QString& directory) {
    QFile file(directory + "/result.json");
    if (directory.isEmpty() || file.size() > 65536 || !file.open(QIODevice::ReadOnly)) return Backend::tr("Не удалось подтвердить сохранение результата");
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value("version").toInt() != 1 || json.value("status").toString() != "completed" || json.value("code").toInt(-1) != 0)
        return Backend::tr("Backend не подтвердил успешное завершение");
    for (const auto* suffix : {"txt", "srt", "vtt"}) {
        QFileInfo exportFile(directory + "/transcripts/transcript." + suffix);
        if (!exportFile.isFile() || exportFile.isSymLink()) return Backend::tr("Отсутствует итоговый экспорт %1").arg(suffix);
    }
    return {};
}
}
Backend::Backend(QObject* parent) : Backend(Paths{backendBinary(), settingsFile(), pathText(transcribe::app_home())}, parent) {}
Backend::Backend(Paths paths, QObject* parent)
    : QObject(parent), binary_(std::move(paths.binary)), dataRoot_(std::move(paths.dataRoot)), settings_(paths.settingsFile, QSettings::IniFormat), history_(this), models_(dataRoot_, this) {
    QFile selected(dataRoot_ + "/default-model");
    if (selected.size() < 128 && selected.open(QIODevice::ReadOnly)) modelSelection_ = QString::fromUtf8(selected.readLine()).trimmed();
    modelSelection_ = settings_.value("model", modelSelection_).toString();
    if (modelSelection_ != "small" && modelSelection_ != "medium" && modelSelection_ != "turbo") modelSelection_ = "medium";
    output_ = settings_.value("output", pathText(transcribe::default_output())).toString();
    roots_ = settings_.value("roots").toStringList();
    if (!roots_.contains(output_)) roots_.append(output_);
    known_ = settings_.value("history/directories").toStringList();
    previewTimer_.setInterval(500);
    cancelTimer_.setSingleShot(true); cancelTimer_.setInterval(5000);
    connect(&cancelTimer_, &QTimer::timeout, this, [this] {
        if (process_.state() != QProcess::NotRunning) {
            error_ = tr("Backend не ответил на отмену; процесс остановлен принудительно. Частичные файлы сохранены.");
            process_.kill(); emit changed();
        }
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &Backend::readEvents);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        diagnostics_ += process_.readAllStandardError();
        if (diagnostics_.size() > 65536) diagnostics_ = diagnostics_.right(65536);
    });
    connect(&process_, &QProcess::finished, this, &Backend::exited);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { error_ = tr("Не удалось запустить backend: %1").arg(process_.errorString()); status_ = "failed"; settled(); }
    });
    connect(&previewTimer_, &QTimer::timeout, this, &Backend::readPreview);
    connect(&previewWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (previewPath_ == result_) { transcript_ = previewWatcher_.result(); emit transcriptChanged(); }
        if (previewDirty_) { previewDirty_ = false; readPreview(); }
    });
    connect(&completionWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto failure = completionWatcher_.result();
        if (failure.isEmpty()) { status_ = "completed"; stage_ = tr("Готово"); progress_ = 1; eta_.clear(); }
        else { status_ = "failed"; error_ = failure; }
        settled();
    });
    connect(&historyWatcher_, &QFutureWatcherBase::finished, this, [this] {
        const auto rows = historyWatcher_.result();
        for (const auto& row : rows) if (!known_.contains(row.directory)) known_.append(row.directory);
        settings_.setValue("history/directories", known_); history_.replace(rows);
        if (historyDirty_) { historyDirty_ = false; refreshHistory(); }
    });
    connect(&models_, &ModelManager::changed, this, &Backend::changed);
    connect(&models_, &ModelManager::failed, this, [this](const QString& message) { error_ = message; emit changed(); });
    connect(&models_, &ModelManager::installed, this, [this](const QString& selection) {
        if (selection != "vad") { modelSelection_ = selection; settings_.setValue("model", selection); }
        error_.clear(); emit changed();
    });
    refreshHistory();
}
Backend::~Backend() {
    if (process_.state() != QProcess::NotRunning) {
        process_.write("{\"type\":\"cancel\"}\n"); process_.closeWriteChannel();
        // Normal window-close waits asynchronously. This path is only application teardown.
        if (!process_.waitForFinished(5000)) { process_.kill(); process_.waitForFinished(1000); }
    }
    historyWatcher_.waitForFinished(); previewWatcher_.waitForFinished(); completionWatcher_.waitForFinished();
}
QString Backend::filePath(const QUrl& url) const { return url.toLocalFile(); }
void Backend::start(const QString& input, const QVariantMap& values) {
    if (active()) return;
    error_.clear();
    try {
        QStringList arguments{"--machine", "--no-progress"};
        const auto add = [&](const QString& option, const QString& value) { if (!value.isEmpty()) arguments << option << value; };
        const auto selection = values.value("model", modelSelection_).toString();
        add("--model", selection);
        for (const auto* key : {"threads", "chunks", "jobs"}) {
            const auto number = values.value(key).toInt();
            if (number > 0) add("--" + QString::fromLatin1(key), QString::number(number));
        }
        const auto requestedOutput = values.value("output", output_).toString();
        const QString output = requestedOutput.trimmed().isEmpty() ? pathText(transcribe::default_output()) : QDir(requestedOutput).absolutePath();
        add("--out", output);
        add("--prompt", values.value("prompt").toString()); add("--cookies", values.value("cookies").toString()); add("--cookies-from-browser", values.value("browser").toString());
        add("--cache-dir", values.value("cacheDirectory").toString());
        add("--cache-limit-gib", values.value("cacheLimit", 10).toString());
        if (!values.value("vad", true).toBool()) arguments << "--no-vad";
        if (values.value("keepAudio", false).toBool()) arguments << "--keep-audio";
        if (!values.value("cache", true).toBool()) arguments << "--no-cache";
        if (values.value("refreshCache", false).toBool()) arguments << "--refresh-cache";
        arguments << "--" << input;
        std::vector<std::string> raw;
        for (const auto& argument : arguments) raw.push_back(argument.toUtf8().toStdString());
        std::vector<std::string_view> views(raw.begin(), raw.end());
        const auto options = transcribe::parse_arguments(views, 1);
        // Read-only preflight gives useful feedback before a task appears as running.
        // Windows package tools resolve relative to the GUI/CLI installation directory.
        transcribe::validate_inputs(options, {transcribe::utf8_path(dataRoot_.toUtf8().toStdString()), std::filesystem::current_path()});
        if (selection == "small" || selection == "medium" || selection == "turbo") {
            modelSelection_ = selection; settings_.setValue("model", selection);
        }
        output_ = output; settings_.setValue("output", output_);
        if (!roots_.contains(output_)) roots_.append(output_);
        settings_.setValue("roots", roots_);
        pending_.clear(); diagnostics_.clear(); result_.clear(); transcript_.clear(); eta_.clear();
        hello_ = terminal_ = protocolFailure_ = cancelRequested_ = false; terminalCode_ = -1; terminalStatus_.clear();
        busy_ = true; status_ = "processing"; stage_ = tr("Запуск…"); progress_ = -1;
        auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("TRANSCRIBE_HOME", dataRoot_);
        process_.setProcessEnvironment(environment);
        process_.setProgram(binary_); process_.setArguments(arguments); process_.start();
        previewTimer_.start(); emit transcriptChanged(); emit changed();
    } catch (const std::exception& error) { error_ = QString::fromUtf8(error.what()); status_ = "failed"; emit changed(); }
}
void Backend::cancel() {
    if (models_.busy()) models_.cancel();
    if (!busy_ || cancelRequested_ || process_.state() == QProcess::NotRunning) return;
    cancelRequested_ = true; stage_ = tr("Отмена и сохранение частичного результата…");
    process_.write("{\"type\":\"cancel\"}\n"); cancelTimer_.start(); emit changed();
}
void Backend::readEvents() {
    const auto bytes = process_.readAllStandardOutput();
    if (protocolFailure_) return;
    pending_ += bytes;
    if (pending_.size() > qsizetype{256} * 1024) { protocolError(tr("Слишком большое событие backend")); pending_.clear(); return; }
    for (auto end = pending_.indexOf('\n'); end >= 0; end = pending_.indexOf('\n')) {
        const auto line = pending_.left(end); pending_.remove(0, end + 1);
        QJsonParseError error{}; const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { protocolError(tr("Некорректное событие backend")); return; }
        acceptEvent(document.object()); if (protocolFailure_) return;
    }
}
void Backend::acceptEvent(const QJsonObject& event) {
    if (event.value("protocol").toInt() != 1) { protocolError(tr("Несовместимая версия протокола backend")); return; }
    const auto type = event.value("type").toString();
    if (!hello_) {
        if (type != "hello") { protocolError(tr("Backend не выполнил проверку протокола")); return; }
        hello_ = true; return;
    }
    const auto directory = event.value("result").toString();
    if (!directory.isEmpty()) {
        if (!QDir::isAbsolutePath(directory)) { protocolError(tr("Backend передал относительный путь результата")); return; }
        if (result_ != directory) { result_ = directory; transcript_.clear(); emit transcriptChanged(); }
    }
    const auto stage = event.value("stage").toString();
    if (!cancelRequested_ && (type == "stage" || type == "finalizing")) {
        if (stage == "probe") stage_ = tr("Получение информации о видео…");
        else if (stage == "download") stage_ = tr("Загрузка аудио…");
        else if (stage == "cache") stage_ = tr("Чтение аудио из кэша…");
        else if (stage == "prepare") stage_ = tr("Подготовка аудио…");
        else if (stage == "split") stage_ = tr("Подготовка частей…");
        else if (stage == "recognize") stage_ = tr("Распознавание речи…");
        else if (stage == "merge" || type == "finalizing") stage_ = tr("Проверка и сохранение экспортов…");
        if (stage != "recognize") progress_ = -1;
    }
    if (type == "progress") {
        const double fraction = event.value("fraction").toDouble(-1);
        if (!std::isfinite(fraction) || fraction < 0 || fraction > 1) { protocolError(tr("Некорректный прогресс backend")); return; }
        progress_ = fraction;
        const auto eta = event.value("eta_seconds");
        if (eta.isDouble() && std::isfinite(eta.toDouble()) && eta.toDouble() >= 0) {
            const auto seconds = static_cast<qint64>(std::min(eta.toDouble(), 359999.0));
            eta_ = tr("Осталось примерно %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        } else eta_ = tr("Оценка времени рассчитывается…");
    } else if (type == "warning") error_ = tr("Предупреждение: %1").arg(event.value("message").toString());
    else if (type == "completed" || type == "failed") {
        if (terminal_) { protocolError(tr("Backend передал повторный итоговый статус")); return; }
        terminal_ = true; terminalStatus_ = event.value("status").toString(); terminalCode_ = event.value("code").toInt(-1);
        if (type == "failed") error_ = event.value("message").toString();
    }
    emit changed();
}
void Backend::protocolError(const QString& message) { protocolFailure_ = true; error_ = message; cancel(); emit changed(); }
void Backend::exited(int code, QProcess::ExitStatus exitStatus) {
    readEvents(); cancelTimer_.stop(); previewTimer_.stop(); readPreview();
    if (!protocolFailure_ && hello_ && terminal_ && terminalStatus_ == "completed" && terminalCode_ == 0 && code == 0 && exitStatus == QProcess::NormalExit && pending_.trimmed().isEmpty()) {
        const auto directory = result_;
        completionWatcher_.setFuture(QtConcurrent::run([directory] { return verifyCompletion(directory); }));
        return;
    }
    status_ = !protocolFailure_ && terminalStatus_ == "interrupted" && code != 0 ? "interrupted" : "failed";
    if (error_.isEmpty()) error_ = tr("Backend завершился без подтверждённого результата. Проверьте журналы.");
    settled();
}
void Backend::settled() { busy_ = false; previewTimer_.stop(); cancelTimer_.stop(); refreshHistory(); emit changed(); }
void Backend::readPreview() {
    if (result_.isEmpty()) return;
    if (previewWatcher_.isRunning()) { previewDirty_ = true; return; }
    previewPath_ = result_; const auto directory = result_;
    previewWatcher_.setFuture(QtConcurrent::run([directory] {
        QFile file(directory + "/transcripts/transcript.txt");
        if (!file.open(QIODevice::ReadOnly)) return QString{};
        constexpr qint64 budget = qint64{512} * 1024;
        const bool tail = file.size() > budget;
        if (tail) { file.seek(file.size() - budget); file.readLine(); }
        return (tail ? Backend::tr("Показан конец текста; полный TXT сохранён в каталоге результата.\n\n") : QString{}) + QString::fromUtf8(file.read(budget));
    }));
}
void Backend::refreshHistory() {
    if (historyWatcher_.isRunning()) { historyDirty_ = true; return; }
    const auto roots = roots_, known = known_;
    historyWatcher_.setFuture(QtConcurrent::run([roots, known] { return HistoryModel::scan(roots, known); }));
}
void Backend::viewResult(const QString& directory) { if (busy_) return; result_ = directory; status_ = history_.statusFor(directory); stage_.clear(); eta_.clear(); error_.clear(); progress_ = -1; transcript_.clear(); emit changed(); emit transcriptChanged(); readPreview(); }
void Backend::openResult(const QString& extension) {
    if (result_.isEmpty()) return;
    if (!extension.isEmpty() && extension != "txt" && extension != "srt" && extension != "vtt" && extension != "logs") return;
    auto path = result_;
    if (extension == "logs") path += "/logs";
    else if (!extension.isEmpty()) path += "/transcripts/transcript." + extension;
    if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) { error_ = tr("Не удалось открыть %1").arg(path); emit changed(); }
}
void Backend::downloadModel(const QString& selection) { if (!busy_) { error_.clear(); models_.download(selection); emit changed(); } }
void Backend::importModel(const QString& path, const QString& selection) { if (!busy_) { error_.clear(); models_.importModel(path, selection); emit changed(); } }
