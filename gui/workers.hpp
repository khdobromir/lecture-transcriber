#pragma once
#include <QException>
#include <QFutureWatcher>
#include <QString>
#include <exception>

inline QString workerError(const std::exception& error) {
    if (const auto* wrapped = dynamic_cast<const QUnhandledException*>(&error)) {
        try { std::rethrow_exception(wrapped->exception()); }
        catch (const std::exception& cause) { return QString::fromUtf8(cause.what()); }
        catch (...) { return QStringLiteral("Ошибка фоновой операции"); }
    }
    return QString::fromUtf8(error.what());
}
inline void joinWorker(QFutureWatcherBase& worker) noexcept {
    try { worker.waitForFinished(); }
    catch (...) { // NOLINT(bugprone-empty-catch): Qt has joined the failed worker; teardown cannot propagate its exception.
    }
}
