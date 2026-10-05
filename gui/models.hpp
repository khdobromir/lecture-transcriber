#pragma once
#include "platform.hpp"
#include "states.hpp"
#include <QFile>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QObject>
#include <QVector>
#include <QTimer>
#include <atomic>
#include <memory>
#include <functional>

class ModelManager : public QObject {
    Q_OBJECT
public:
    struct Record { QString selection, filename, url; QByteArray sha256; };
    // Runs in the worker after commit; carries data-only test synchronization.
    using Cancel = std::shared_ptr<std::atomic<bool>>;
    struct Workers { std::function<void()> afterCommit = {}; std::function<void(const Cancel&)> hashCheckpoint = {}; };
    explicit ModelManager(QString root, QObject* parent = nullptr);
    ModelManager(QString root, QVector<Record> records, QObject* parent = nullptr);
    ModelManager(QString root, QVector<Record> records, Workers workers, QObject* parent = nullptr);
    ~ModelManager() override;
    bool busy() const { return modelBusy(state_); }
    ModelState state() const { return state_; }
    bool canRecover() const { return !busy() && !recoverySelection_.isEmpty(); }
    QString backupPath() const { return backupPath_; }
    double progress() const { return progress_; }
    QString status() const { return status_; }
    void download(const QString& selection);
    void recover();
    void cancel();
    void importModel(const QString& source, const QString& selection);
    // Also used by tests with a local HTTP fixture; production URLs come from the embedded manifest.
    static QVector<Record> manifest();
    static bool verify(const QString& path, const QByteArray& expected, const Cancel& cancel,
        const std::function<void(const Cancel&)>& checkpoint = {});
signals:
    void changed();
    void installed(QString selection);
    void failed(QString message);
private:
    QString root_, selection_, status_, recoverySelection_, backupPath_;
    QVector<Record> records_, queue_;
    qsizetype index_ = 0;
    ModelState state_ = ModelState::idle;
    bool responseChecked_ = false, restarted_ = false, repair_ = false, closing_ = false, publishingFinal_ = false;
    quint64 generation_ = 0;
    double progress_ = -1;
    qint64 offset_ = 0;
    qint64 expectedTotal_ = -1;
    QString responseError_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QFile part_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::shared_ptr<std::atomic<ModelState>> workerPhase_;
    std::shared_ptr<transcribe::FileLock> lock_;
    QFutureWatcher<std::shared_ptr<transcribe::FileLock>> lockWatcher_;
    QFutureWatcher<bool> verifyWatcher_;
    QFutureWatcher<bool> importWatcher_;
    QFutureWatcher<bool> publishWatcher_, backupWatcher_;
    QTimer phaseTimer_;
    Workers workers_;
    bool verifyingPart_ = false, probingPart_ = false;
    void next();
    void beginDownload(const QString& selection, bool repair);
    void preparePartial();
    void publish(bool finalOnly = false);
    void backup(const QString& target);
    void transition(ModelState state, const QString& message = {});
    void fetch();
    bool checkResponse();
    void finish(const QString& error = {});
};
