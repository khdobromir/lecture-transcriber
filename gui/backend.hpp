#pragma once
#include "history.hpp"
#include "models.hpp"
#include <QFutureWatcher>
#include <QJsonObject>
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
public:
    explicit Backend(QObject* parent = nullptr);
    struct Paths { QString binary, settingsFile, dataRoot; };
    explicit Backend(Paths paths, QObject* parent = nullptr);
    ~Backend() override;
    bool busy() const { return busy_; }
    bool active() const { return busy_ || models_.busy(); }
    QString status() const { return historyStatus(status_); }
    QString stage() const { return stage_; }
    QString error() const { return error_; }
    QString resultDirectory() const { return result_; }
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
    Q_INVOKABLE void start(const QString& input, const QVariantMap& settings);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void viewResult(const QString& directory);
    Q_INVOKABLE void openResult(const QString& extension = {});
    Q_INVOKABLE void downloadModel(const QString& selection);
    Q_INVOKABLE void importModel(const QString& path, const QString& selection);
    Q_INVOKABLE QString filePath(const QUrl& url) const;
signals:
    void changed();
    void transcriptChanged();
private:
    QString modelSelection_ = "medium";
    QString binary_, dataRoot_, output_, status_ = "unknown", stage_, error_, result_, transcript_, eta_;
    bool busy_ = false, hello_ = false, terminal_ = false, protocolFailure_ = false, cancelRequested_ = false, historyDirty_ = false, previewDirty_ = false;
    double progress_ = -1;
    QByteArray pending_, diagnostics_;
    QString terminalStatus_;
    int terminalCode_ = -1;
    QProcess process_;
    QTimer previewTimer_, cancelTimer_;
    QSettings settings_;
    HistoryModel history_;
    ModelManager models_;
    QFutureWatcher<QVector<HistoryRow>> historyWatcher_;
    QFutureWatcher<QString> previewWatcher_;
    QFutureWatcher<QString> completionWatcher_;
    QString previewPath_;
    QStringList roots_, known_;
    QVariantMap knownTimes_;
    void readEvents();
    void acceptEvent(const QJsonObject& event);
    void protocolError(const QString& message);
    void exited(int code, QProcess::ExitStatus exitStatus);
    void readPreview();
    void settled();
};
