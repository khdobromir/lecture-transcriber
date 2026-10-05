#pragma once
#include "platform.hpp"
#include <QFile>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QObject>
#include <QVector>
#include <atomic>
#include <memory>

class ModelManager : public QObject {
    Q_OBJECT
public:
    struct Record { QString selection, filename, url; QByteArray sha256; };
    explicit ModelManager(QString root, QObject* parent = nullptr);
    ModelManager(QString root, QVector<Record> records, QObject* parent = nullptr);
    ~ModelManager() override;
    bool busy() const { return busy_; }
    double progress() const { return progress_; }
    QString status() const { return status_; }
    void download(const QString& selection);
    void cancel();
    void importModel(const QString& source, const QString& selection);
    // Also used by tests with a local HTTP fixture; production URLs come from the embedded manifest.
    static QVector<Record> manifest();
    static bool verify(const QString& path, const QByteArray& expected, const std::shared_ptr<std::atomic<bool>>& cancel);
signals:
    void changed();
    void installed(QString selection);
    void failed(QString message);
private:
    QString root_, selection_, status_;
    QVector<Record> records_, queue_;
    qsizetype index_ = 0;
    bool busy_ = false, responseChecked_ = false, restarted_ = false;
    double progress_ = -1;
    qint64 offset_ = 0;
    qint64 expectedTotal_ = -1;
    QString responseError_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QFile part_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::shared_ptr<transcribe::FileLock> lock_;
    QFutureWatcher<std::shared_ptr<transcribe::FileLock>> lockWatcher_;
    QFutureWatcher<bool> verifyWatcher_;
    QFutureWatcher<bool> importWatcher_;
    bool verifyingPart_ = false, probingPart_ = false;
    void next();
    void fetch();
    bool checkResponse();
    void finish(QString error = {});
};
