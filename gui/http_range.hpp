#pragma once
#include <QByteArray>
#include <QRegularExpression>
#include <optional>

struct ContentRange {
    qint64 begin = 0, end = 0, total = 0;
    bool unsatisfied = false;
    static std::optional<ContentRange> parse(const QByteArray& header) {
        if (header.size() > 128) return {};
        const auto text = QString::fromLatin1(header);
        const auto missing = QRegularExpression("\\Abytes \\*/([0-9]+)\\z").match(text);
        bool ok = false;
        if (missing.hasMatch()) {
            const auto total = missing.captured(1).toLongLong(&ok);
            if (ok && total >= 0) return ContentRange{0, 0, total, true};
            return {};
        }
        const auto match = QRegularExpression("\\Abytes ([0-9]+)-([0-9]+)/([0-9]+)\\z").match(text);
        if (!match.hasMatch()) return {};
        const auto begin = match.captured(1).toLongLong(&ok); if (!ok) return {};
        const auto end = match.captured(2).toLongLong(&ok); if (!ok) return {};
        const auto total = match.captured(3).toLongLong(&ok); if (!ok) return {};
        if (begin < 0 || end < begin || total <= end) return {};
        return ContentRange{begin, end, total, false};
    }
};
