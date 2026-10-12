#pragma once
#include "text.hpp"
#include <QException>
#include <QFutureWatcher>
#include <QString>
#include <exception>

inline UiText workerError(const std::exception& error) {
    if (const auto* wrapped = dynamic_cast<const QUnhandledException*>(&error)) {
        try {
            std::rethrow_exception(wrapped->exception());
        } catch (const std::exception& cause) { return UiText(transcribe::exception_message(cause)); } catch (...) {
            return UiText::translated("Workers", QT_TRANSLATE_NOOP("Workers", "Ошибка фоновой операции"));
        }
    }
    return UiText(transcribe::exception_message(error));
}
inline void joinWorker(QFutureWatcherBase& worker) noexcept {
    try {
        worker.waitForFinished();
    } catch (...) { // NOLINT(bugprone-empty-catch): Qt has joined the failed worker; teardown cannot propagate its exception.
    }
}
