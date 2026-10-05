#pragma once
#include <cstdint>
#include <QAbstractListModel>
#include <QVector>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <QtQml/qqmlregistration.h>

struct HistoryRow { QString title, status, model, directory, date; bool available = true; qint64 created = 0; };
class HistoryModel : public QAbstractListModel {
    Q_OBJECT
    // NOLINTNEXTLINE(performance-enum-size): Qt registration marker enum is SDK-generated and never stored in an object.
    QML_ANONYMOUS
public:
    enum Role : std::uint16_t { Title = Qt::UserRole + 1, Status, Model, Directory, Date, Available };
    explicit HistoryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void replace(QVector<HistoryRow> rows);
    QString statusFor(const QString& directory) const;
    static QVector<HistoryRow> scan(const QStringList& roots, const QStringList& known, const QVariantMap& timestamps = {},
        const std::shared_ptr<std::atomic<bool>>& cancel = {});
private:
    QVector<HistoryRow> rows_;
};
QString historyStatus(const QString& status);
