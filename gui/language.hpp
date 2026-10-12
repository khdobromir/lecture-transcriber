#pragma once
#include <QObject>
#include <QSettings>
#include <QTranslator>
#include <QLocale>
#include <QtQml/qqmlregistration.h>

class LanguageController : public QObject {
    Q_OBJECT
    // NOLINTNEXTLINE(performance-enum-size): Qt generates this registration marker; it is not stored in objects.
    QML_ANONYMOUS
    Q_PROPERTY(QString selection READ selection WRITE setSelection NOTIFY changed)
    Q_PROPERTY(QString effectiveLanguage READ effectiveLanguage NOTIFY changed)
public:
    explicit LanguageController(QSettings& settings, QObject* parent = nullptr);
    ~LanguageController() override;
    QString selection() const { return selection_; }
    QString effectiveLanguage() const { return effective_; }
    static QString resolve(const QString& selection, const QLocale& locale);
    void setSelection(const QString& selection);
signals:
    void changed();

private:
    QSettings& settings_;
    QTranslator english_, qtRussian_, qtQuickRussian_;
    QString selection_, effective_;
    void apply();
};
