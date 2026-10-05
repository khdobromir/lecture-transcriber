#include "history.hpp"
#include "files.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

QString historyStatus(const QString& status) {
    if (status == "completed") return HistoryModel::tr("Готово");
    if (status == "interrupted") return HistoryModel::tr("Прервано");
    if (status == "failed") return HistoryModel::tr("Ошибка");
    if (status == "processing") return HistoryModel::tr("Не завершено");
    return HistoryModel::tr("Неизвестно");
}
int HistoryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const auto& row = rows_[index.row()];
    switch (role) {
    case Title: return row.title;
    case Status: return historyStatus(row.status);
    case Model: return row.model;
    case Directory: return row.directory;
    case Date: return row.date;
    case Available: return row.available;
    default: return {};
    }
}
QHash<int, QByteArray> HistoryModel::roleNames() const { return {{Title, "title"}, {Status, "status"}, {Model, "modelName"}, {Directory, "directory"}, {Date, "date"}, {Available, "available"}}; }
void HistoryModel::replace(QVector<HistoryRow> rows) { beginResetModel(); rows_ = std::move(rows); endResetModel(); }
QString HistoryModel::statusFor(const QString& directory) const {
    for (const auto& row : rows_) if (row.directory == directory) return row.status;
    return "unknown";
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): scan roots first, restore known result directories second.
QVector<HistoryRow> HistoryModel::scan(const QStringList& roots, const QStringList& known, const QVariantMap& timestamps,
    const std::shared_ptr<std::atomic<bool>>& cancel) {
    QSet<QString> directories(known.begin(), known.end());
    for (const auto& root : roots) {
        if (cancel && cancel->load()) return {};
        const auto children = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Time);
        for (const auto& child : children) {
            if (cancel && cancel->load()) return {};
            if (QFile::exists(child.filePath() + "/result.json") || QFile::exists(child.filePath() + "/source.txt")) directories.insert(child.filePath());
        }
    }
    QVector<HistoryRow> rows;
    for (const auto& directory : directories) {
        if (cancel && cancel->load()) return {};
        const QFileInfo info(directory);
        HistoryRow row{info.fileName(), "unknown", {}, directory, info.lastModified().toString(Qt::ISODate), info.isDir() && !info.isSymLink()};
        const auto birth = info.birthTime();
        row.created = timestamps.value(directory, 0).toLongLong();
        if (!row.created && row.available) row.created = (birth.isValid() ? birth : info.lastModified()).toMSecsSinceEpoch();
        if (row.available) {
            if (const auto bytes = readSmallFile(directory + "/result.json", 65536)) {
                const auto doc = QJsonDocument::fromJson(*bytes);
                const auto json = doc.object();
                if (json.value("version").toInt() == 1) {
                    row.title = json.value("title").toString(row.title); row.status = json.value("status").toString(); row.model = json.value("model").toString();
                    const auto created = json.value("created_unix_ms").toInteger(-1);
                    if (created >= 0) row.created = created;
                }
            } else {
                if (const auto legacy = readSmallFile(directory + "/source.txt", 65536)) {
                    for (const auto& line : QString::fromUtf8(*legacy).split('\n')) {
                        if (line.startsWith("Статус: ")) row.status = line.mid(8).trimmed();
                        if (line.startsWith("Модель: ")) row.model = line.mid(8).trimmed();
                    }
                }
            }
        }
        row.date = row.created > 0 ? QDateTime::fromMSecsSinceEpoch(row.created).toString(Qt::ISODate) : QString{};
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.created != b.created ? a.created > b.created : a.directory < b.directory; });
    if (rows.size() > 2000) rows.resize(2000);
    return rows;
}
