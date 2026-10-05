#include "backend.hpp"
#include "files.hpp"
#include "cli.hpp"
#include "platform.hpp"
#include "result.hpp"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    const auto bytes = readSmallFile(directory + "/result.json", 65536);
    if (directory.isEmpty() || !bytes) return Backend::tr("Не удалось подтвердить сохранение результата");
    const auto json = QJsonDocument::fromJson(*bytes).object();
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
    knownTimes_ = settings_.value("history/timestamps").toMap();
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
        if (historyDirty_) { historyDirty_ = false; refreshHistory(); return; }
        known_.clear(); knownTimes_.clear();
        for (const auto& row : rows) { known_.append(row.directory); knownTimes_.insert(row.directory, row.created); }
        settings_.setValue("history/timestamps", knownTimes_);
        settings_.setValue("history/directories", known_); history_.replace(rows);
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
        parser_ = transcribe::ProtocolParser{}; exit_.reset();
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
    if (protocolFailure_) { process_.readAllStandardOutput(); if (exit_) finishExit(); return; }
    constexpr qsizetype readBudget = 65536;
    qsizetype received = 0;
    unsigned lines = 0;
    while (lines < 64) {
        const auto end = pending_.indexOf('\n');
        if (end >= 0) {
            const auto line = pending_.left(end); pending_.remove(0, end + 1);
            const auto decoded = parser_.accept(std::string_view(line.constData(), static_cast<std::size_t>(line.size())));
            if (const auto* problem = std::get_if<transcribe::ProtocolProblem>(&decoded)) {
                protocolError(QString::fromUtf8(problem->message)); pending_.clear(); break;
            }
            acceptEvent(std::get<transcribe::ProtocolMessage>(decoded)); ++lines;
        } else {
            if (pending_.size() > static_cast<qsizetype>(transcribe::max_event_line)) {
                protocolError(tr("Слишком большое событие backend")); pending_.clear(); break;
            }
            if (received >= readBudget || !process_.bytesAvailable()) break;
            const auto bytes = process_.read(readBudget - received);
            received += bytes.size(); pending_ += bytes;
            if (bytes.isEmpty()) break;
        }
    }
    if (!protocolFailure_ && (pending_.contains('\n') || process_.bytesAvailable())) {
        if (!eventsScheduled_) {
            eventsScheduled_ = true;
            QTimer::singleShot(0, this, [this] { eventsScheduled_ = false; readEvents(); });
        }
    } else if (exit_) finishExit();
}
void Backend::acceptEvent(const transcribe::ProtocolMessage& message) {
    if (message.hello) { hello_ = true; return; }
    const auto& event = message.event;
    const auto directory = pathText(event.result);
    if (!directory.isEmpty() && result_ != directory) {
        result_ = directory; transcript_.clear(); emit transcriptChanged();
    }
    const auto stage = QString::fromStdString(event.stage);
    if (!cancelRequested_ && (event.type == transcribe::EventType::stage || event.type == transcribe::EventType::finalizing)) {
        if (stage == "local") stage_ = tr("Проверка локального входа…");
        else if (stage == "probe") stage_ = tr("Получение информации о видео…");
        else if (stage == "download") stage_ = tr("Загрузка аудио…");
        else if (stage == "cache") stage_ = tr("Чтение аудио из кэша…");
        else if (stage == "prepare") stage_ = tr("Подготовка аудио…");
        else if (stage == "split") stage_ = tr("Подготовка частей…");
        else if (stage == "recognize") stage_ = tr("Распознавание речи…");
        else if (stage == "merge" || event.type == transcribe::EventType::finalizing) stage_ = tr("Сохранение результата…");
        if (stage != "recognize") { progress_ = -1; eta_.clear(); }
    }
    if (event.type == transcribe::EventType::progress) {
        progress_ = event.fraction;
        if (event.eta) {
            const auto seconds = static_cast<qint64>(std::min(*event.eta, 359999.0));
            eta_ = tr("Осталось примерно %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        } else eta_ = tr("Оценка времени рассчитывается…");
    } else if (event.type == transcribe::EventType::warning) error_ = tr("Предупреждение: %1").arg(QString::fromStdString(event.message));
    else if (event.type == transcribe::EventType::completed || event.type == transcribe::EventType::failed) {
        terminal_ = true; terminalStatus_ = QString::fromStdString(event.status); terminalCode_ = event.code;
        if (event.type == transcribe::EventType::failed) error_ = QString::fromStdString(event.message);
    }
    emit changed();
}
void Backend::protocolError(const QString& message) { protocolFailure_ = true; error_ = message; cancel(); emit changed(); }
void Backend::exited(int code, QProcess::ExitStatus exitStatus) {
    exit_ = std::pair{code, exitStatus};
    diagnostics_ += process_.readAllStandardError(); diagnostics_ = diagnostics_.right(65536);
    cancelTimer_.stop(); previewTimer_.stop(); readPreview(); readEvents();
}
void Backend::finishExit() {
    if (!exit_) return;
    const auto [code, exitStatus] = *exit_; exit_.reset();
    if (!protocolFailure_ && hello_ && terminal_ && terminalStatus_ == "completed" && terminalCode_ == 0 && code == 0 && exitStatus == QProcess::NormalExit && pending_.isEmpty()) {
        const auto directory = result_;
        completionWatcher_.setFuture(QtConcurrent::run([directory] { return verifyCompletion(directory); }));
        return;
    }
    status_ = !protocolFailure_ && terminal_ && terminalStatus_ == "interrupted" && terminalCode_ == code && code != 0 &&
        exitStatus == QProcess::NormalExit && pending_.isEmpty() ? "interrupted" : "failed";
    if (error_.isEmpty()) error_ = tr("Backend завершился без подтверждённого результата. Проверьте журналы.");
    settled();
}
void Backend::settled() {
    busy_ = false; previewTimer_.stop(); cancelTimer_.stop();
    if (!result_.isEmpty() && !known_.contains(result_)) known_.prepend(result_);
    refreshHistory(); emit changed();
}
void Backend::readPreview() {
    if (result_.isEmpty()) return;
    if (previewWatcher_.isRunning()) { previewDirty_ = true; return; }
    previewPath_ = result_; const auto directory = result_;
    previewWatcher_.setFuture(QtConcurrent::run([directory] {
        try {
            transcribe::SharedReader reader(transcribe::utf8_path((directory + "/transcripts/transcript.txt").toUtf8().toStdString()));
            constexpr std::size_t budget = std::size_t{512} * 1024;
            const bool tail = reader.size() > budget;
            auto bytes = QByteArray::fromStdString(reader.read(tail ? reader.size() - budget : 0, budget));
            if (tail) { const auto newline = bytes.indexOf('\n'); bytes = newline >= 0 ? bytes.mid(newline + 1) : QByteArray{}; }
            return (tail ? Backend::tr("Показан конец текста; полный TXT сохранён в каталоге результата.\n\n") : QString{}) + QString::fromUtf8(bytes);
        } catch (const std::exception&) { return QString{}; }
    }));
}
void Backend::refreshHistory() {
    if (historyWatcher_.isRunning()) { historyDirty_ = true; return; }
    const auto roots = roots_, known = known_; const auto timestamps = knownTimes_;
    historyWatcher_.setFuture(QtConcurrent::run([roots, known, timestamps] { return HistoryModel::scan(roots, known, timestamps); }));
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
