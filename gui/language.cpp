#include "language.hpp"
#include "text.hpp"
#include <QCoreApplication>
#include <stdexcept>

LanguageController::LanguageController(QSettings& settings, QObject* parent) : QObject(parent), settings_(settings) {
    if (!english_.load(":/i18n/transcribe_en.qm")) throw std::runtime_error("Missing embedded English translations");
    if (!qtRussian_.load(":/i18n/qtbase_ru.qm") || !qtQuickRussian_.load(":/i18n/qtdeclarative_ru.qm"))
        throw std::runtime_error("Missing embedded Qt Russian translations");
    selection_ = settings_.value("ui/language", "auto").toString();
    if (selection_ != "ru" && selection_ != "en") selection_ = "auto";
    apply();
}
LanguageController::~LanguageController() {
    QCoreApplication::removeTranslator(&english_);
    QCoreApplication::removeTranslator(&qtRussian_);
    QCoreApplication::removeTranslator(&qtQuickRussian_);
    uiEnglish.store(false);
}
QString LanguageController::resolve(const QString& selection, const QLocale& locale) {
    if (selection == "ru" || selection == "en") return selection;
    return locale.language() == QLocale::Russian ? "ru" : "en";
}
void LanguageController::setSelection(const QString& selection) {
    const auto normalized = selection == "ru" || selection == "en" ? selection : QStringLiteral("auto");
    if (selection_ == normalized) return;
    selection_ = normalized;
    settings_.setValue("ui/language", selection_);
    settings_.sync();
    apply();
    emit changed();
}
void LanguageController::apply() {
    effective_ = resolve(selection_, QLocale::system());
    QCoreApplication::removeTranslator(&english_);
    QCoreApplication::removeTranslator(&qtRussian_);
    QCoreApplication::removeTranslator(&qtQuickRussian_);
    uiEnglish.store(effective_ == "en");
    if (effective_ == "en") QCoreApplication::installTranslator(&english_);
    else {
        QCoreApplication::installTranslator(&qtRussian_);
        QCoreApplication::installTranslator(&qtQuickRussian_);
    }
}
