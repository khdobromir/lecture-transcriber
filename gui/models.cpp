#include "models.hpp"
#include "files.hpp"
#include "http_range.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QException>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <stdexcept>

namespace {
QString exceptionMessage(const std::exception& error) {
    if (const auto* wrapped = dynamic_cast<const QUnhandledException*>(&error)) {
        try { std::rethrow_exception(wrapped->exception()); }
        catch (const std::exception& cause) { return QString::fromUtf8(cause.what()); }
        catch (...) { return QStringLiteral("Ошибка фоновой операции с моделью"); }
    }
    return QString::fromUtf8(error.what());
}
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
    : QObject(parent), root_(std::move(root)), records_(std::move(records)) {
    connect(&lockWatcher_, &QFutureWatcherBase::finished, this, [this] {
        try { lock_ = lockWatcher_.future().takeResult(); next(); }
        catch (const std::exception& error) { finish(exceptionMessage(error)); }
        catch (...) { finish(cancel_ && cancel_->load() ? tr("Загрузка отменена") : tr("Не удалось получить блокировку моделей")); }
    });
    connect(&verifyWatcher_, &QFutureWatcherBase::finished, this, [this] {
        try {
            checkCancelled(cancel_);
            const bool valid = verifyWatcher_.result();
            const auto& record = queue_[index_];
            const QString target = root_ + "/models/" + record.filename;
            if (verifyingPart_) {
                if (!valid && probingPart_) { probingPart_ = false; fetch(); return; }
                if (!valid) { QFile::remove(target + ".part"); finish(tr("SHA-256 модели не совпадает. Повторите загрузку.")); return; }
                transcribe::replace_file(transcribe::utf8_path((target + ".part").toStdString()), transcribe::utf8_path(target.toStdString()));
                QFile::remove(target + ".part.sha256");
                ++index_; next();
            } else if (valid) { ++index_; next(); }
            else if (QFile::exists(target)) finish(tr("Установленная модель повреждена. Сохраните её отдельно перед повторной загрузкой."));
            else if (QFile::exists(target + ".part")) {
                verifyingPart_ = probingPart_ = true;
                const auto partial = target + ".part"; const auto cancel = cancel_;
                verifyWatcher_.setFuture(QtConcurrent::run([partial, record, cancel] { return verify(partial, record.sha256, cancel); }));
            } else fetch();
        } catch (const std::exception& error) { finish(exceptionMessage(error)); }
        catch (...) { finish(tr("Ошибка проверки модели")); }
    });
    connect(&importWatcher_, &QFutureWatcherBase::finished, this, [this] {
        try {
            if (!importWatcher_.result()) { finish(tr("Выбранный файл не соответствует SHA-256 модели")); return; }
            finish();
        } catch (const std::exception& error) { finish(exceptionMessage(error)); }
        catch (...) { finish(tr("Ошибка импорта модели")); }
    });
}
ModelManager::~ModelManager() {
    if (cancel_) cancel_->store(true);
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
    // QtConcurrent rethrows worker exceptions from waitForFinished as well as result.
    // Shutdown must wait for every worker without propagating from a destructor.
    try { lockWatcher_.waitForFinished(); } catch (...) { // NOLINT(bugprone-empty-catch): wait has already joined the failed worker; shutdown must not throw.
    }
    try { verifyWatcher_.waitForFinished(); } catch (...) { // NOLINT(bugprone-empty-catch): wait has already joined the failed worker; shutdown must not throw.
    }
    try { importWatcher_.waitForFinished(); } catch (...) { // NOLINT(bugprone-empty-catch): wait has already joined the failed worker; shutdown must not throw.
    }
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
bool ModelManager::verify(const QString& path, const QByteArray& expected, const std::shared_ptr<std::atomic<bool>>& cancel) {
    safeModelPath(path);
    QFile file(path);
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Не удалось прочитать модель");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        checkCancelled(cancel);
        const auto bytes = file.read(qint64{1024} * 1024);
        if (file.error() != QFileDevice::NoError) throw std::runtime_error("Ошибка чтения модели");
        hash.addData(bytes);
    }
    checkCancelled(cancel); return hash.result().toHex() == expected;
}
void ModelManager::download(const QString& selection) {
    if (busy_) return;
    try {
        queue_.clear();
        const auto& records = records_;
        for (const auto& record : records) if (record.selection == selection) queue_.push_back(record);
        if (queue_.isEmpty() || selection == "vad") { emit failed(tr("Выберите small, medium или turbo")); return; }
        for (const auto& record : records) if (record.selection == "vad") queue_.push_back(record);
        selection_ = selection; index_ = 0; busy_ = true; progress_ = -1;
        status_ = tr("Ожидание доступа к моделям…");
        cancel_ = std::make_shared<std::atomic<bool>>(false); emit changed();
        const auto root = root_; const auto cancel = cancel_;
        lockWatcher_.setFuture(QtConcurrent::run([root, cancel] {
            safeModelPath(root);
            if (!QDir().mkpath(root + "/models")) throw std::runtime_error("Не удалось создать каталог моделей");
            return std::make_shared<transcribe::FileLock>(transcribe::utf8_path((root + "/.install.lock").toStdString()), [cancel] { checkCancelled(cancel); });
        }));
    } catch (const std::exception& error) { finish(exceptionMessage(error)); }
}
void ModelManager::next() {
    checkCancelled(cancel_);
    if (index_ == queue_.size()) { finish(); return; }
    const auto record = queue_[index_];
    status_ = tr("Проверка %1…").arg(record.selection); progress_ = -1; verifyingPart_ = probingPart_ = restarted_ = false; emit changed();
    const auto cancel = cancel_; const auto target = root_ + "/models/" + record.filename;
    verifyWatcher_.setFuture(QtConcurrent::run([target, record, cancel] { return verify(target, record.sha256, cancel); }));
}
void ModelManager::fetch() {
    try {
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
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (offset_) request.setRawHeader("Range", "bytes=" + QByteArray::number(offset_) + '-');
    responseChecked_ = false; responseError_.clear(); expectedTotal_ = -1;
    status_ = tr("Загрузка %1…").arg(record.selection); emit changed();
    reply_ = network_.get(request);
    const QPointer<QNetworkReply> operation = reply_;
    connect(reply_, &QNetworkReply::readyRead, this, [this, operation] {
        if (!operation || reply_ != operation) return;
        if (!checkResponse()) return;
        const auto bytes = reply_->readAll();
        if (part_.write(bytes) != bytes.size()) { responseError_ = tr("Не удалось записать модель: проверьте свободное место"); reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this, operation](qint64 received, qint64 total) {
        if (!operation || reply_ != operation) return;
        progress_ = total > 0 ? std::clamp((static_cast<double>(offset_) + static_cast<double>(received)) /
            (static_cast<double>(offset_) + static_cast<double>(total)), 0.0, 1.0) : -1;
        emit changed();
    });
    connect(reply_, &QNetworkReply::finished, this, [this, operation] {
        if (!operation || reply_ != operation) return;
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
            verifyingPart_ = true; probingPart_ = false; status_ = tr("Проверка SHA-256 %1…").arg(record.selection); progress_ = -1; emit changed();
            verifyWatcher_.setFuture(QtConcurrent::run([target, record, cancel] { return verify(target, record.sha256, cancel); }));
        } else {
            const auto message = cancel_->load() ? tr("Загрузка отменена; временный файл сохранён") :
                (!responseError_.isEmpty() ? responseError_ : tr("Загрузка не завершена: %1").arg(reply->errorString()));
            part_.close(); reply->deleteLater(); reply_.clear(); finish(message);
        }
    });
    } catch (const std::exception& error) { finish(exceptionMessage(error)); }
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
void ModelManager::cancel() { if (cancel_) cancel_->store(true); if (reply_) reply_->abort(); }
void ModelManager::finish(QString error) {
    part_.close(); busy_ = false; progress_ = -1;
    if (error.isEmpty()) {
        // Downloads retain the installation lock here; imports save in their worker.
        if (lock_) {
            try { saveSelection(QDir(root_), selection_); }
            catch (const std::exception& cause) { error = exceptionMessage(cause); }
        }
    }
    lock_.reset();
    status_ = error.isEmpty() ? tr("Модель готова") : error;
    emit changed();
    if (error.isEmpty()) emit installed(selection_); else emit failed(error);
}
void ModelManager::importModel(const QString& source, const QString& selection) { // NOLINT(bugprone-easily-swappable-parameters): source path precedes selection, matching the GUI import action.
    if (busy_) return;
    try {
        queue_.clear(); for (const auto& record : records_) if (record.selection == selection) queue_.push_back(record);
        if (queue_.size() != 1) { emit failed(tr("Неизвестная модель")); return; }
        const auto record = queue_.front(); selection_ = selection;
        busy_ = true; progress_ = -1; status_ = tr("Проверка и импорт модели…"); cancel_ = std::make_shared<std::atomic<bool>>(false); emit changed();
        const auto cancel = cancel_; const auto root = root_;
        importWatcher_.setFuture(QtConcurrent::run([root, source, record, cancel] {
            safeModelPath(root); if (!QDir().mkpath(root + "/models")) throw std::runtime_error("Не удалось создать каталог моделей");
            transcribe::FileLock lock(transcribe::utf8_path((root + "/.install.lock").toStdString()), [cancel] { checkCancelled(cancel); });
            if (!verify(source, record.sha256, cancel)) return false;
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
            if (!verify(partial, record.sha256, cancel)) return false;
            checkCancelled(cancel);
            // Publish while the cross-process installation lock is still held.
            transcribe::replace_file(transcribe::utf8_path(partial.toStdString()), transcribe::utf8_path(target.toStdString()));
            saveSelection(QDir(root), record.selection);
            return true;
        }));
    } catch (const std::exception& error) { finish(exceptionMessage(error)); }
}
