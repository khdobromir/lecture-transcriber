#pragma once
#include "message.hpp"
#include <QCoreApplication>
#include <QString>
#include <atomic>
#include <functional>
#include <utility>

// Only presentation reads this flag; workers retain messages without rendering them.
inline std::atomic_bool uiEnglish{false};
class UiText {
public:
    UiText() = default;
    UiText(QString raw) : render_([raw = std::move(raw)] { return raw; }) {}
    UiText(const char* raw) : UiText(QString::fromUtf8(raw)) {}
    UiText(transcribe::Message message) : render_([message = std::move(message)] {
                                              return QString::fromStdString(uiEnglish.load() ? message.en : message.ru);
                                          }) {}
    static UiText translated(const char* context, const char* source) {
        UiText text;
        text.render_ = [context, source] { return QCoreApplication::translate(context, source); };
        return text;
    }
    QString text() const { return render_ ? render_() : QString{}; }
    operator QString() const { return text(); }
    bool isEmpty() const { return text().isEmpty(); }
    void clear() { render_ = {}; }
    template<class... Args>
    UiText arg(Args&&... args) const {
        const auto value = QString("%1").arg(std::forward<Args>(args)...);
        UiText result;
        result.render_ = [previous = *this, value] { return previous.text().arg(value); };
        return result;
    }
    UiText& operator+=(const UiText& other) {
        *this = *this + other;
        return *this;
    }
    UiText& operator+=(char value) { return *this += UiText(QString(QChar::fromLatin1(value))); }
    friend UiText operator+(const UiText& left, const UiText& right) {
        UiText result;
        result.render_ = [left, right] { return left.text() + right.text(); };
        return result;
    }

private:
    std::function<QString()> render_;
};
