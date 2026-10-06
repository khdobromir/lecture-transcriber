#pragma once
#include "platform.hpp"
#include <QByteArray>
#include <QString>
#include <optional>

// Readers own their native handle and permit replacement throughout the read.
inline std::optional<QByteArray> readSmallFile(const QString& path, std::size_t limit) {
    try {
        transcribe::SharedReader reader(transcribe::utf8_path(path.toUtf8().toStdString()));
        if (reader.size() > limit) return {};
        return QByteArray::fromStdString(reader.read(0, limit));
    } catch (const std::exception&) { return {}; }
}
