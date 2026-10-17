#pragma once
#include <QColor>
#include <QAbstractNativeEventFilter>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

struct ThemeResult {
    ThemeResult(QColor value = {}, QString message = {}, bool retry = false, QString key = {})
        : seed(value), error(std::move(message)), retryable(retry), cacheKey(std::move(key)) {}
    QColor seed;
    QString error;
    bool retryable = false;
    QString cacheKey;
};

class ThemeController : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    QML_NAMED_ELEMENT(ThemeController)
    // NOLINTNEXTLINE(performance-enum-size): Qt registration marker is SDK-generated and never stored in objects.
    QML_UNCREATABLE("Owned by Backend")
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QColor customColor READ customColor WRITE setCustomColor NOTIFY changed)
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY changed)
public:
    explicit ThemeController(QSettings& settings, const QString& tools, QObject* parent = nullptr,
                             QString omarchyRoot = {});
    ~ThemeController() override;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override;
    QString mode() const { return mode_; }
    QString source() const { return source_; }
    QColor customColor() const { return custom_; }
    QString imagePath() const { return image_; }
    bool reducedMotion() const { return reduced_; }
    bool dark() const;
    QVariantMap colors() const { return dark() ? darkColors_ : lightColors_; }
    QString status() const;
    bool analyzing() const { return watcher_.isRunning(); }
    void setMode(const QString& value);
    void setSource(const QString& value);
    void setCustomColor(const QColor& value);
    void setImagePath(const QString& value);
    void setReducedMotion(bool value);
    Q_INVOKABLE void chooseImage(const QUrl& url);
    Q_INVOKABLE void refresh();
    static QVariantMap palette(const QColor& seed, bool dark);
    static QColor imageSeed(QImage image);
    static ThemeResult readImage(const QString& path);
    static ThemeResult decodeImage(const QByteArray& bytes);
signals:
    void changed();

private:
    QSettings& settings_;
    QString tools_, omarchy_, mode_, source_, image_, error_, signature_;
    QColor custom_, seed_;
    bool reduced_ = false, pending_ = false, stopped_ = false;
    quint64 generation_ = 0, runningGeneration_ = 0;
    int retryStep_ = 0;
    QVariantMap lightColors_, darkColors_;
    QFileSystemWatcher files_;
    QTimer debounce_, poll_, retry_;
    QFutureWatcher<ThemeResult> watcher_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    void schedule();
    void resolve();
    void applySeed(const QColor& color);
    void save();
    void watch(const QString& path);
};
