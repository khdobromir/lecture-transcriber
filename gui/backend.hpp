#pragma once
#include "history.hpp"
#include "models.hpp"
#include "protocol.hpp"
#include "states.hpp"
#include <QFutureWatcher>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class Backend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString selectedStatus READ selectedStatus NOTIFY changed)
    Q_PROPERTY(QString taskResultDirectory READ taskResultDirectory NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(QString previewError READ previewError NOTIFY changed)
    Q_PROPERTY(QString historyError READ historyError NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString resultDirectory READ resultDirectory NOTIFY changed)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString eta READ eta NOTIFY changed)
    Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY changed)
    Q_PROPERTY(QString modelSelection READ modelSelection NOTIFY changed)
    Q_PROPERTY(QString appHome READ appHome CONSTANT)
    Q_PROPERTY(HistoryModel* history READ history CONSTANT)
    Q_PROPERTY(bool modelBusy READ modelBusy NOTIFY changed)
    Q_PROPERTY(double modelProgress READ modelProgress NOTIFY changed)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY changed)
    Q_PROPERTY(bool modelCanRecover READ modelCanRecover NOTIFY changed)
    Q_PROPERTY(QString modelBackupPath READ modelBackupPath NOTIFY changed)
public:
    explicit Backend(QObject* parent = nullptr);
    struct Paths { QString binary, settingsFile, dataRoot; QVector<ModelManager::Record> modelRecords = {}; };
    using Cancel = std::shared_ptr<std::atomic<bool>>;
    struct Readers {
        std::function<QString(const QString&, const Cancel&)> preview, completion;
        std::function<QVector<HistoryRow>(const QStringList&, const QStringList&, const QVariantMap&, const Cancel&)> history;
    };
    explicit Backend(Paths paths, QObject* parent = nullptr);
    Backend(Paths paths, Readers readers, QObject* parent = nullptr);
    ~Backend() override;
    bool busy() const { return taskBusy(state_); }
    bool active() const { return busy() || models_.busy(); }
    TaskState state() const { return state_; }
    QString status() const;
    QString selectedStatus() const { return historyStatus(selectedStatus_); }
    QString taskResultDirectory() const { return result_; }
    QString warning() const { return warning_; }
    QString previewError() const { return previewError_; }
    QString historyError() const { return historyError_; }
    bool canRetry() const { return !active() && !lastInput_.isEmpty() && (state_ == TaskState::failed || state_ == TaskState::interrupted); }
    bool canCancel() const { return busy() && state_ != TaskState::cancel_requested && !terminal_; }
    QString stage() const { return stage_; }
    QString error() const { return error_; }
    QString resultDirectory() const { return selected_; }
    QString transcript() const { return transcript_; }
    double progress() const { return progress_; }
    QString eta() const { return eta_; }
    QString outputDirectory() const { return output_; }
    QString modelSelection() const { return modelSelection_; }
    QString appHome() const { return dataRoot_; }
    HistoryModel* history() { return &history_; }
    bool modelBusy() const { return models_.busy(); }
    double modelProgress() const { return models_.progress(); }
    QString modelStatus() const { return models_.status(); }
    bool modelCanRecover() const { return models_.canRecover(); }
    QString modelBackupPath() const { return models_.backupPath(); }
    Q_INVOKABLE void start(const QString& input, const QVariantMap& settings);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void viewCurrentResult();
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void viewResult(const QString& directory);
    Q_INVOKABLE void openResult(const QString& extension = {});
    Q_INVOKABLE void openTaskResult(const QString& extension = {});
    Q_INVOKABLE void downloadModel(const QString& selection);
    Q_INVOKABLE void recoverModel();
    Q_INVOKABLE void importModel(const QString& path, const QString& selection);
    Q_INVOKABLE QString filePath(const QUrl& url) const;
signals:
    void changed();
    void transcriptChanged();
private:
    QString modelSelection_ = "medium";
    QString binary_, dataRoot_, output_, stage_, error_, warning_, result_, selected_, selectedStatus_ = "unknown", transcript_, eta_, previewError_, historyError_, lastInput_;
    QVariantMap lastValues_;
    TaskState state_ = TaskState::idle;
    bool hello_ = false, terminal_ = false, protocolFailure_ = false, historyDirty_ = false, previewDirty_ = false, followingTask_ = true, closing_ = false;
    bool previewOutstanding_ = false, historyOutstanding_ = false;
    quint64 taskGeneration_ = 0, selectionGeneration_ = 0, previewGeneration_ = 0, historyGeneration_ = 0, historyReadGeneration_ = 0, completionGeneration_ = 0;
    Cancel previewCancel_, historyCancel_, completionCancel_;
    Readers readers_;
    double progress_ = -1;
    QByteArray pending_, diagnostics_;
    QString terminalStatus_;
    int terminalCode_ = -1;
    QProcess process_;
    QTimer previewTimer_, cancelTimer_;
    QSettings settings_;
    transcribe::ProtocolParser parser_;
    bool eventsScheduled_ = false;
    std::optional<std::pair<int, QProcess::ExitStatus>> exit_;
    HistoryModel history_;
    ModelManager models_;
    QFutureWatcher<QVector<HistoryRow>> historyWatcher_;
    QFutureWatcher<QString> previewWatcher_;
    QFutureWatcher<QString> completionWatcher_;
    QStringList roots_, known_;
    QVariantMap knownTimes_;
    void readEvents();
    void acceptEvent(const transcribe::ProtocolMessage& message);
    void protocolError(const QString& message);
    void exited(int code, QProcess::ExitStatus exitStatus);
    void finishExit();
    void readPreview();
    void settled();
    void transition(TaskState state);
    void selectResult(const QString& directory, const QString& status);
};
