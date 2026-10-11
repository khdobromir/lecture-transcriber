#include "models.hpp"
#include "files.hpp"
#include "http_range.hpp"
#include "workers.hpp"
#include <QUuid>
#include <QCryptographicHash>
#include <QDir>
#include <QException>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <stdexcept>

namespace {
void saveSelection(const QDir& root, const QString& selection) {
    if (selection == "vad") return;
    QSaveFile file(root.filePath("default-model"));
    const auto bytes = selection.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Не удалось сохранить выбор модели");
}
void checkCancelled(const std::shared_ptr<std::atomic<bool>>& cancel) {
    if (cancel->load()) throw std::runtime_error("Загрузка отменена");
}
void safeModelPath(const QString& path) {
    auto current = transcribe::utf8_path(path.toUtf8().toStdString());
    while (!current.empty()) {
        if (transcribe::indirect_path(current)) throw std::runtime_error("Ссылка в пути модели");
        const auto parent = current.parent_path();
        if (current == parent) break;
        current = parent;
    }
}
}
ModelManager::ModelManager(QString root, QObject* parent) : ModelManager(std::move(root), manifest(), parent) {}
ModelManager::ModelManager(QString root, QVector<Record> records, QObject* parent)
    : ModelManager(std::move(root), std::move(records), Workers{}, parent) {}
ModelManager::ModelManager(QString root, QVector<Record> records, Workers workers, QObject* parent)
    : QObject(parent), root_(std::move(root)), records_(std::move(records)), workers_(std::move(workers)) {
    phaseTimer_.setInterval(25);
    connect(&phaseTimer_, &QTimer::timeout, this, [this] {
        if (workerPhase_ && state_ != ModelState::cancelling) transition(workerPhase_->load());
    });
    connect(&lockWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (closing_ || !lockWatcher_.future().isFinished()) return;
        try { lock_ = lockWatcher_.future().takeResult(); next(); }
        catch (const std::exception& error) { finish(workerError(error)); }
        catch (...) { finish(tr("Не удалось получить блокировку моделей")); }
    });
    connect(&verifyWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (closing_ || !verifyWatcher_.future().isFinished()) return;
        try {
            checkCancelled(cancel_);
            const bool valid = verifyWatcher_.result();
            const auto& record = queue_[index_];
            const QString target = root_ + "/models/" + record.filename;
            if (verifyingPart_) {
                if (!valid && probingPart_) { probingPart_ = false; fetch(); return; }
                if (!valid) { QFile::remove(target + ".part"); finish(tr("SHA-256 модели не совпадает. Повторите загрузку.")); return; }
                publish();
            } else if (valid) { ++index_; next(); }
            else if (QFile::exists(target)) {
                if (repair_) backup(target);
                else { recoverySelection_ = selection_; finish(tr("Установленная модель повреждена. Сохраните резервную копию и восстановите её.")); }
            } else preparePartial();
        } catch (const std::exception& error) { finish(workerError(error)); }
        catch (...) { finish(tr("Ошибка проверки модели")); }
    });
    connect(&importWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (closing_ || !importWatcher_.future().isFinished()) return;
        try {
            if (!importWatcher_.result()) { finish(tr("Выбранный файл не соответствует SHA-256 модели")); return; }
            finish(); // Worker success means publication and selection already committed.
        } catch (const std::exception& error) { finish(workerError(error)); }
        catch (...) { finish(tr("Ошибка импорта модели")); }
    });
    connect(&publishWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (closing_ || !publishWatcher_.future().isFinished()) return;
        try {
            publishWatcher_.result();
            if (publishingFinal_) finish();
            else { ++index_; next(); }
        } catch (const std::exception& error) { finish(workerError(error)); }
        catch (...) { finish(tr("Ошибка сохранения модели")); }
    });
    connect(&backupWatcher_, &QFutureWatcherBase::finished, this, [this] {
        if (closing_ || !backupWatcher_.future().isFinished()) return;
        try { backupWatcher_.result(); preparePartial(); }
        catch (const std::exception& error) { finish(workerError(error)); }
        catch (...) { finish(tr("Ошибка резервного копирования модели")); }
    });
}
ModelManager::~ModelManager() {
    closing_ = true; ++generation_; phaseTimer_.stop();
    if (cancel_) cancel_->store(true);
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
    for (auto* watcher : {static_cast<QFutureWatcherBase*>(&lockWatcher_), static_cast<QFutureWatcherBase*>(&verifyWatcher_),
            static_cast<QFutureWatcherBase*>(&importWatcher_), static_cast<QFutureWatcherBase*>(&publishWatcher_), static_cast<QFutureWatcherBase*>(&backupWatcher_)}) {
        watcher->disconnect(this); joinWorker(*watcher);
    }
    part_.close(); lock_.reset();
}
void ModelManager::transition(ModelState state, const QString& message) {
    if (!transitionAllowed(state_, state)) return;
    state_ = state;
    if (!message.isEmpty()) status_ = message;
    else {
        switch (state) {
        case ModelState::waiting_lock: status_ = tr("Ожидание доступа к моделям…"); break;
        case ModelState::checking_existing: status_ = tr("Проверка установленной модели…"); break;
        case ModelState::downloading: status_ = tr("Загрузка модели…"); break;
        case ModelState::verifying: status_ = tr("Проверка SHA-256 модели…"); break;
        case ModelState::publishing: status_ = tr("Сохранение модели…"); break;
        case ModelState::cancelling: status_ = tr("Отмена операции с моделью…"); break;
        default: break;
        }
    }
    emit changed();
}
QVector<ModelManager::Record> ModelManager::manifest() {
    Q_INIT_RESOURCE(model_manifest);
    QFile file(":/models/models.tsv");
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Не найден встроенный манифест моделей");
    QVector<Record> records;
    for (const auto& raw : QString::fromUtf8(file.readAll()).split('\n')) {
        if (raw.trimmed().isEmpty() || raw.startsWith('#')) continue;
        const auto columns = raw.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (columns.size() != 5 || !QRegularExpression("^ggml-[a-zA-Z0-9_.-]+\\.bin$").match(columns[1]).hasMatch() ||
            !QRegularExpression("^[a-f0-9]{40}$").match(columns[3]).hasMatch() || !QRegularExpression("^[a-f0-9]{64}$").match(columns[4]).hasMatch())
            throw std::runtime_error("Некорректный манифест моделей");
        records.push_back({columns[0], columns[1], "https://huggingface.co/" + columns[2] + "/resolve/" + columns[3] + '/' + columns[1], columns[4].toLatin1()});
    }
    return records;
}
bool ModelManager::verify(const QString& path, const QByteArray& expected, const Cancel& cancel,
    const std::function<void(const Cancel&)>& checkpoint) {
    safeModelPath(path);
    QFile file(path);
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Не удалось прочитать модель");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        checkCancelled(cancel);
        const auto bytes = file.read(qint64{1024} * 1024);
        if (file.error() != QFileDevice::NoError) throw std::runtime_error("Ошибка чтения модели");
        if (checkpoint) checkpoint(cancel);
        checkCancelled(cancel);
        hash.addData(bytes);
    }
    checkCancelled(cancel); return hash.result().toHex() == expected;
}
void ModelManager::download(const QString& selection) { beginDownload(selection, false); }
void ModelManager::recover() { if (canRecover()) { const auto selection = recoverySelection_; beginDownload(selection, true); } }
void ModelManager::beginDownload(const QString& selection, bool repair) {
    if (busy()) return;
    try {
        queue_.clear();
        const auto& records = records_;
        for (const auto& record : records) if (record.selection == selection) queue_.push_back(record);
        if (queue_.isEmpty() || selection == "vad") {
            cancel_ = std::make_shared<std::atomic<bool>>(false); transition(ModelState::waiting_lock);
            finish(tr("Выберите small, medium или turbo")); return;
        }
        for (const auto& record : records) if (record.selection == "vad") queue_.push_back(record);
        selection_ = selection; index_ = 0; progress_ = -1; repair_ = repair; recoverySelection_.clear(); ++generation_;
        cancel_ = std::make_shared<std::atomic<bool>>(false); transition(ModelState::waiting_lock);
        const auto root = root_; const auto cancel = cancel_;
        lockWatcher_.setFuture(QtConcurrent::run([root, cancel] {
            safeModelPath(root);
            if (!QDir().mkpath(root + "/models")) throw std::runtime_error("Не удалось создать каталог моделей");
            return std::make_shared<transcribe::FileLock>(transcribe::utf8_path((root + "/.install.lock").toStdString()), [cancel] { checkCancelled(cancel); });
        }));
    } catch (const std::exception& error) { finish(workerError(error)); }
}
void ModelManager::next() {
    checkCancelled(cancel_);
    if (index_ == queue_.size()) { publish(true); return; }
    const auto record = queue_[index_];
    progress_ = -1; verifyingPart_ = probingPart_ = restarted_ = false; transition(ModelState::checking_existing, tr("Проверка %1…").arg(record.selection));
    const auto cancel = cancel_; const auto target = root_ + "/models/" + record.filename; const auto checkpoint = workers_.hashCheckpoint;
    verifyWatcher_.setFuture(QtConcurrent::run([target, record, cancel, checkpoint] { return verify(target, record.sha256, cancel, checkpoint); }));
}
void ModelManager::preparePartial() {
    checkCancelled(cancel_);
    const auto record = queue_[index_]; const auto target = root_ + "/models/" + record.filename;
    if (!QFile::exists(target + ".part")) { fetch(); return; }
    verifyingPart_ = probingPart_ = true;
    transition(ModelState::verifying);
    const auto partial = target + ".part"; const auto cancel = cancel_; const auto checkpoint = workers_.hashCheckpoint;
    verifyWatcher_.setFuture(QtConcurrent::run([partial, record, cancel, checkpoint] { return verify(partial, record.sha256, cancel, checkpoint); }));
}
void ModelManager::publish(bool finalOnly) {
    checkCancelled(cancel_);
    publishingFinal_ = finalOnly || index_ + 1 == queue_.size();
    transition(ModelState::publishing); progress_ = -1;
    const auto root = root_, selection = selection_;
    const auto target = finalOnly ? QString{} : root + "/models/" + queue_[index_].filename;
    const auto cancel = cancel_; const auto lock = lock_; const bool last = publishingFinal_; const auto committed = workers_.afterCommit;
    publishWatcher_.setFuture(QtConcurrent::run([root, selection, target, cancel, lock, last, committed] {
        if (!lock) throw std::runtime_error("Потеряна блокировка установки модели");
        checkCancelled(cancel);
        if (!target.isEmpty()) {
            transcribe::replace_file(transcribe::utf8_path((target + ".part").toUtf8().toStdString()),
                transcribe::utf8_path(target.toUtf8().toStdString()), [cancel] { checkCancelled(cancel); });
            QFile::remove(target + ".part.sha256");
        }
        // The final successful publication is the cancellation boundary.
        if (last) { saveSelection(QDir(root), selection); if (committed) committed(); }
        return true;
    }));
}
void ModelManager::backup(const QString& target) {
    backupPath_ = target + ".backup-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".bin";
    transition(ModelState::checking_existing, tr("Сохранение резервной копии: %1").arg(backupPath_));
    const auto destination = backupPath_; const auto cancel = cancel_; const auto lock = lock_;
    backupWatcher_.setFuture(QtConcurrent::run([target, destination, cancel, lock] {
        if (!lock) throw std::runtime_error("Потеряна блокировка резервного копирования");
        safeModelPath(target); safeModelPath(destination);
        QFile input(target); QSaveFile output(destination);
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) throw std::runtime_error("Не удалось создать резервную копию модели");
        while (!input.atEnd()) {
            checkCancelled(cancel); const auto bytes = input.read(qint64{1024} * 1024);
            if (input.error() != QFileDevice::NoError || output.write(bytes) != bytes.size()) throw std::runtime_error("Ошибка записи резервной копии модели");
        }
        checkCancelled(cancel);
        if (!output.commit()) throw std::runtime_error("Не удалось сохранить резервную копию модели");
        return true;
    }));
}
void ModelManager::fetch() {
    try {
    checkCancelled(cancel_);
    const auto record = queue_[index_];
    const auto partial = root_ + "/models/" + record.filename + ".part";
    safeModelPath(partial); safeModelPath(partial + ".sha256");
    const auto identity = readSmallFile(partial + ".sha256", 128);
    if (QFile::exists(partial) && (!identity || identity->trimmed() != record.sha256)) {
        if (!QFile::remove(partial)) { finish(tr("Не удалось сбросить временный файл другой ревизии модели")); return; }
    }
    QSaveFile binding(partial + ".sha256");
    if (!binding.open(QIODevice::WriteOnly) || binding.write(record.sha256) != record.sha256.size() || !binding.commit()) {
        finish(tr("Не удалось сохранить ревизию временного файла модели")); return;
    }
    part_.setFileName(partial);
    if (!part_.open(QIODevice::ReadWrite | QIODevice::Unbuffered)) { finish(tr("Не удалось открыть временный файл модели")); return; }
    offset_ = part_.size();
    if (!part_.seek(offset_)) { finish(tr("Не удалось продолжить временный файл модели")); return; }
    QNetworkRequest request(QUrl(record.url));
    const auto caBundle = qEnvironmentVariable("TRANSCRIBE_CA_BUNDLE");
    if (request.url().scheme() == "https" && !caBundle.isEmpty()) {
        const auto certificates = QSslCertificate::fromPath(caBundle);
        if (certificates.isEmpty()) { finish(tr("Не удалось прочитать сертификаты переносимого приложения. Замените файл приложения.")); return; }
        auto ssl = request.sslConfiguration();
        ssl.setCaCertificates(QSslConfiguration::systemCaCertificates() + certificates);
        request.setSslConfiguration(ssl);
    }
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (offset_) request.setRawHeader("Range", "bytes=" + QByteArray::number(offset_) + '-');
    responseChecked_ = false; responseError_.clear(); expectedTotal_ = -1;
    transition(ModelState::downloading, tr("Загрузка %1…").arg(record.selection));
    reply_ = network_.get(request);
    const QPointer<QNetworkReply> operation = reply_; const auto generation = generation_;
    connect(reply_, &QNetworkReply::readyRead, this, [this, operation, generation] {
        if (closing_ || generation != generation_ || !operation || reply_ != operation) return;
        if (!checkResponse()) return;
        const auto bytes = reply_->readAll();
        if (part_.write(bytes) != bytes.size()) { responseError_ = tr("Не удалось записать модель: проверьте свободное место"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this, operation, generation](qint64 received, qint64 total) {
        if (closing_ || generation != generation_ || !operation || reply_ != operation) return;
        progress_ = total > 0 ? std::clamp((static_cast<double>(offset_) + static_cast<double>(received)) /
            (static_cast<double>(offset_) + static_cast<double>(total)), 0.0, 1.0) : -1;
        emit changed();
    });
    connect(reply_, &QNetworkReply::finished, this, [this, operation, generation] {
        if (closing_ || generation != generation_ || !operation || reply_ != operation) return;
        auto* reply = operation.data();
        if (!cancel_->load() && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 416 && !restarted_) {
            const auto range = ContentRange::parse(reply->rawHeader("Content-Range"));
            if (range && range->unsatisfied && offset_ >= range->total) {
                part_.close(); reply->disconnect(this); reply->deleteLater(); reply_.clear();
                restarted_ = true;
                if (!QFile::remove(part_.fileName())) { finish(tr("Не удалось сбросить непригодный временный файл модели")); return; }
                fetch(); return;
            }
        }
        if (reply->error() == QNetworkReply::NoError && checkResponse()) {
            const auto rest = reply->readAll();
            if (part_.write(rest) != rest.size()) { part_.close(); reply->deleteLater(); reply_.clear(); finish(tr("Ошибка записи модели")); return; }
            if (expectedTotal_ >= 0 && part_.size() != expectedTotal_) {
                part_.close(); reply->deleteLater(); reply_.clear(); finish(tr("Размер полученной модели не соответствует HTTP диапазону")); return;
            }
            if (!part_.flush()) { part_.close(); reply->deleteLater(); reply_.clear(); finish(tr("Не удалось сохранить временную модель")); return; }
            part_.close(); reply->deleteLater(); reply_.clear();
            const auto record = queue_[index_]; const auto target = root_ + "/models/" + record.filename + ".part"; const auto cancel = cancel_;
            verifyingPart_ = true; probingPart_ = false; progress_ = -1; transition(ModelState::verifying, tr("Проверка SHA-256 %1…").arg(record.selection));
            const auto checkpoint = workers_.hashCheckpoint;
            verifyWatcher_.setFuture(QtConcurrent::run([target, record, cancel, checkpoint] { return verify(target, record.sha256, cancel, checkpoint); }));
        } else {
            const auto message = cancel_->load() ? tr("Загрузка отменена; временный файл сохранён") :
                (!responseError_.isEmpty() ? responseError_ : tr("Загрузка не завершена: %1").arg(reply->errorString()));
            part_.close(); reply->deleteLater(); reply_.clear(); finish(message);
        }
    });
    } catch (const std::exception& error) { finish(workerError(error)); }
}
bool ModelManager::checkResponse() {
    if (responseChecked_) return true;
    if (!reply_) return false;
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!status) return false;
    if (status == 200) {
        if (offset_) { if (!part_.resize(0) || !part_.seek(0)) { reply_->abort(); return false; } offset_ = 0; }
        bool ok = false; const auto size = reply_->rawHeader("Content-Length").toLongLong(&ok);
        if (ok && size >= 0) expectedTotal_ = size;
    } else if (status == 206) {
        const auto range = ContentRange::parse(reply_->rawHeader("Content-Range"));
        bool ok = false; const auto size = reply_->rawHeader("Content-Length").toLongLong(&ok);
        if (!range || range->unsatisfied || range->begin != offset_ || range->end != range->total - 1 ||
            (reply_->hasRawHeader("Content-Length") && (!ok || size != range->end - range->begin + 1))) {
            responseError_ = tr("Некорректный Content-Range ответа модели; временный файл сохранён"); reply_->abort(); return false;
        }
        expectedTotal_ = range->total;
    } else { reply_->abort(); return false; }
    responseChecked_ = true; return true;
}
void ModelManager::cancel() {
    if (!busy()) return;
    if (cancel_) cancel_->store(true);
    transition(ModelState::cancelling);
    if (reply_) reply_->abort();
}
void ModelManager::finish(const QString& error) {
    part_.close(); phaseTimer_.stop(); workerPhase_.reset(); progress_ = -1; lock_.reset();
    const auto state = error.isEmpty() ? ModelState::ready : (cancel_ && cancel_->load() ? ModelState::cancelled : ModelState::failed);
    if (error.isEmpty()) recoverySelection_.clear();
    else if (repair_) recoverySelection_ = selection_;
    transition(state, error.isEmpty() ? tr("Модель готова") : error);
    if (error.isEmpty()) emit installed(selection_);
    else emit failed(error);
}
void ModelManager::importModel(const QString& source, const QString& selection) { // NOLINT(bugprone-easily-swappable-parameters): source path precedes selection, matching the GUI import action.
    if (busy()) return;
    try {
        queue_.clear(); for (const auto& record : records_) if (record.selection == selection) queue_.push_back(record);
        if (queue_.size() != 1) {
            cancel_ = std::make_shared<std::atomic<bool>>(false); transition(ModelState::waiting_lock);
            finish(tr("Неизвестная модель")); return;
        }
        const auto record = queue_.front(); selection_ = selection;
        progress_ = -1; recoverySelection_.clear(); repair_ = false; ++generation_;
        cancel_ = std::make_shared<std::atomic<bool>>(false); transition(ModelState::waiting_lock);
        workerPhase_ = std::make_shared<std::atomic<ModelState>>(ModelState::waiting_lock); phaseTimer_.start();
        const auto cancel = cancel_; const auto root = root_; const auto phase = workerPhase_; const auto committed = workers_.afterCommit; const auto checkpoint = workers_.hashCheckpoint;
        importWatcher_.setFuture(QtConcurrent::run([root, source, record, cancel, phase, committed, checkpoint] {
            safeModelPath(root); if (!QDir().mkpath(root + "/models")) throw std::runtime_error("Не удалось создать каталог моделей");
            transcribe::FileLock lock(transcribe::utf8_path((root + "/.install.lock").toStdString()), [cancel] { checkCancelled(cancel); });
            phase->store(ModelState::verifying);
            if (!verify(source, record.sha256, cancel, checkpoint)) return false;
            const auto target = root + "/models/" + record.filename;
            const auto partial = target + ".import.part";
            safeModelPath(target); safeModelPath(partial);
            QFile input(source), output(partial);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw std::runtime_error("Ошибка открытия модели");
            while (!input.atEnd()) {
                checkCancelled(cancel); const auto bytes = input.read(qint64{1024} * 1024);
                if (input.error() != QFileDevice::NoError || output.write(bytes) != bytes.size()) throw std::runtime_error("Ошибка копирования модели");
            }
            input.close(); output.close();
            if (!verify(partial, record.sha256, cancel, checkpoint)) return false;
            checkCancelled(cancel);
            phase->store(ModelState::publishing);
            // Publish while the cross-process installation lock is still held.
            transcribe::replace_file(transcribe::utf8_path(partial.toStdString()), transcribe::utf8_path(target.toStdString()), [cancel] { checkCancelled(cancel); });
            saveSelection(QDir(root), record.selection);
            if (committed) committed();
            return true;
        }));
    } catch (const std::exception& error) { finish(workerError(error)); }
}
