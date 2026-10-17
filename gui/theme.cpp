#include "theme.hpp"
#include "platform.hpp"
#include "cpp/dynamiccolor/material_dynamic_colors.h"
#include "cpp/quantize/celebi.h"
#include "cpp/scheme/scheme_tonal_spot.h"
#include "cpp/score/score.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStyleHints>
#include <QtConcurrent>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace {
constexpr auto fallback = QRgb{0xff6750a4};
constexpr auto cacheVersion = "mcu-dd62982-std-rng-v1-srgb-white-128-celebi128-score1-tonal0";
QString field(const QString& text, const QString& key) {
    const QRegularExpression expression("(?:^|\\n)\\s*" + key + R"(\s*=\s*["']?([^"'\s#]+))");
    return expression.match(text).captured(1);
}
QString readText(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(65536)) : QString{};
}
#ifdef Q_OS_WIN
struct Wallpaper {
    QString path;
    QColor color;
};
Wallpaper windowsWallpaper() {
    // This call stays on the GUI thread, where COM ownership is unambiguous.
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IDesktopWallpaper* desktop = nullptr;
    Wallpaper result;
    if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&desktop)))) {
        POINT origin{};
        MONITORINFO primary{};
        primary.cbSize = sizeof(primary);
        GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &primary);
        UINT count = 0;
        desktop->GetMonitorDevicePathCount(&count);
        for (UINT index = 0; index < count; ++index) {
            LPWSTR id = nullptr;
            if (FAILED(desktop->GetMonitorDevicePathAt(index, &id))) continue;
            RECT rect{};
            if (SUCCEEDED(desktop->GetMonitorRECT(id, &rect)) && EqualRect(&rect, &primary.rcMonitor)) {
                LPWSTR path = nullptr;
                const auto status = desktop->GetWallpaper(id, &path);
                if (status == S_OK && path) result.path = QString::fromWCharArray(path);
                CoTaskMemFree(path);
            }
            CoTaskMemFree(id);
        }
        if (result.path.isEmpty()) {
            COLORREF color{};
            if (SUCCEEDED(desktop->GetBackgroundColor(&color))) result.color = QColor(GetRValue(color), GetGValue(color), GetBValue(color));
        }
        desktop->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}
#endif
ThemeResult videoFrame(const QString& path, const QString& tools, const std::shared_ptr<std::atomic<bool>>& cancelled) {
    QElapsedTimer budget;
    budget.start();
    const auto run = [&](const QString& name, const QStringList& args, QByteArray& output) {
        if (cancelled->load()) return false;
        QProcess child;
        child.setProgram(QString::fromStdString(transcribe::tool_path(name.toStdString(), transcribe::utf8_path(tools.toUtf8().toStdString()))));
        child.setArguments(args);
        child.start();
        child.closeWriteChannel();
        while (!child.waitForFinished(50)) {
            output += child.readAllStandardOutput();
            child.readAllStandardError();
            if (cancelled->load() || budget.elapsed() > 5000 || output.size() > 1024 * 1024 || child.state() == QProcess::NotRunning) {
                child.terminate();
                if (!child.waitForFinished(500)) {
                    child.kill();
                    child.waitForFinished();
                }
                return false;
            }
        }
        output += child.readAllStandardOutput();
        return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 && output.size() <= 1024 * 1024;
    };
    try {
        QByteArray metadata;
        if (!run("ffprobe", {"-v", "error", "-protocol_whitelist", "file,pipe", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "json", path}, metadata))
            return {{}, "video", true};
        const auto streams = QJsonDocument::fromJson(metadata).object().value("streams").toArray();
        if (streams.isEmpty()) return {{}, "format", false};
        const auto stream = streams.first().toObject();
        const auto width = stream.value("width").toInt(), height = stream.value("height").toInt();
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || qint64(width) * height > 32000000) return {{}, "limits", false};
        QByteArray frame;
        if (!run("ffmpeg", {"-v", "error", "-protocol_whitelist", "file,pipe", "-nostdin", "-threads", "1", "-i", path, "-frames:v", "1", "-vf", "scale=128:128:force_original_aspect_ratio=decrease", "-f", "image2pipe", "-vcodec", "png", "pipe:1"}, frame))
            return {{}, "video", true};
        const auto image = QImage::fromData(frame, "PNG");
        return image.isNull() ? ThemeResult{{}, "format", false} : ThemeResult{ThemeController::imageSeed(image), {}, false};
    } catch (const std::exception&) { return {{}, "video", true}; }
}
} // namespace

QVariantMap ThemeController::palette(const QColor& seed, bool dark) {
    namespace m = material_color_utilities;
    const m::SchemeTonalSpot scheme(m::Hct(seed.rgba()), dark, 0.0);
    using C = m::MaterialDynamicColors;
    QVariantMap result;
    const auto add = [&](const QString& name, m::DynamicColor color) { result.insert(name, QColor::fromRgba(color.GetArgb(scheme))); };
    add("primary", C::Primary());
    add("onPrimary", C::OnPrimary());
    add("primaryContainer", C::PrimaryContainer());
    add("onPrimaryContainer", C::OnPrimaryContainer());
    add("secondary", C::Secondary());
    add("onSecondary", C::OnSecondary());
    add("secondaryContainer", C::SecondaryContainer());
    add("onSecondaryContainer", C::OnSecondaryContainer());
    add("tertiary", C::Tertiary());
    add("onTertiary", C::OnTertiary());
    add("tertiaryContainer", C::TertiaryContainer());
    add("onTertiaryContainer", C::OnTertiaryContainer());
    add("surface", C::Surface());
    add("onSurface", C::OnSurface());
    add("surfaceContainerLow", C::SurfaceContainerLow());
    add("surfaceContainer", C::SurfaceContainer());
    add("surfaceContainerHigh", C::SurfaceContainerHigh());
    add("onSurfaceVariant", C::OnSurfaceVariant());
    add("outline", C::Outline());
    add("outlineVariant", C::OutlineVariant());
    add("error", C::Error());
    add("onError", C::OnError());
    add("errorContainer", C::ErrorContainer());
    add("onErrorContainer", C::OnErrorContainer());
    return result;
}
QColor ThemeController::imageSeed(QImage image) {
    namespace m = material_color_utilities;
    if (image.isNull()) return QColor::fromRgba(fallback);
    if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(QColorSpace::SRgb);
    QImage opaque(image.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    {
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
    }
    opaque = opaque.scaled(opaque.size().boundedTo(QSize(128, 128)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    std::vector<m::Argb> pixels;
    pixels.reserve(static_cast<std::size_t>(opaque.width() * opaque.height()));
    for (int y = 0; y < opaque.height(); ++y)
        for (int x = 0; x < opaque.width(); ++x) pixels.push_back(opaque.pixel(x, y));
    const auto quantized = m::QuantizeCelebi(pixels, 128);
    m::ScoreOptions options;
    options.desired = 1;
    options.fallback_color_argb = static_cast<int>(fallback);
    const auto scored = m::RankedSuggestions(quantized.color_to_count, options);
    return QColor::fromRgba(scored.empty() ? fallback : scored.front());
}
ThemeResult ThemeController::readImage(const QString& path) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return {{}, "unavailable", true};
    if (info.size() > 64 * 1024 * 1024) return {{}, "limits", false};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, "unavailable", true};
    const auto bytes = file.read(64 * 1024 * 1024 + 1);
    if (bytes.size() > 64 * 1024 * 1024) return {{}, "limits", false};
    return decodeImage(bytes);
}
ThemeResult ThemeController::decodeImage(const QByteArray& bytes) {
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format.isEmpty()) return {{}, "decode", true};
    if (!QList<QByteArray>{"png", "jpeg", "jpg", "webp", "gif", "bmp"}.contains(format)) return {{}, "format", false};
    const auto size = reader.size();
    if (!size.isValid()) return {{}, "decode", true};
    if (size.width() > 16384 || size.height() > 16384 || qint64(size.width()) * size.height() > 32000000) return {{}, "limits", false};
    reader.setScaledSize(size.scaled(128, 128, Qt::KeepAspectRatio));
    const auto image = reader.read();
    if (image.isNull()) return {{}, "decode", true};
    return {imageSeed(image), {}, false};
}

ThemeController::ThemeController(QSettings& settings, const QString& tools, QObject* parent, QString omarchyRoot)
    : QObject(parent), settings_(settings), tools_(tools), omarchy_(omarchyRoot.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : std::move(omarchyRoot)) {
    // This is a decoder allocation ceiling, not a claim about total process RSS.
    QImageReader::setAllocationLimit(128);
#ifdef Q_OS_WIN
    qGuiApp->installNativeEventFilter(this);
#endif
    mode_ = settings_.value("ui/appearance/mode", "system").toString();
    if (mode_ != "light" && mode_ != "dark") mode_ = "system";
    source_ = settings_.value("ui/appearance/source", "wallpaper").toString();
    if (source_ != "image" && source_ != "color") source_ = "wallpaper";
    image_ = settings_.value("ui/appearance/image").toString();
    custom_ = QColor(settings_.value("ui/appearance/color", "#6750a4").toString());
    if (!custom_.isValid()) custom_ = QColor::fromRgba(fallback);
    reduced_ = settings_.value("ui/appearance/reducedMotion", false).toBool();
    auto last = QColor(settings_.value("ui/appearance/lastSeed", "#6750a4").toString());
    if (!last.isValid()) last = QColor::fromRgba(fallback);
    applySeed(last);
    debounce_.setSingleShot(true);
    debounce_.setInterval(300);
    retry_.setSingleShot(true);
    poll_.setInterval(30000);
    connect(&debounce_, &QTimer::timeout, this, &ThemeController::resolve);
    connect(&retry_, &QTimer::timeout, this, [this] { ++generation_; signature_.clear(); debounce_.start(); });
    connect(&poll_, &QTimer::timeout, this, &ThemeController::resolve);
    connect(&files_, &QFileSystemWatcher::fileChanged, this, [this] { schedule(); });
    connect(&files_, &QFileSystemWatcher::directoryChanged, this, [this] { schedule(); });
    connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { emit changed(); });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) { if (state == Qt::ApplicationActive) resolve(); });
    connect(&watcher_, &QFutureWatcher<ThemeResult>::finished, this, [this] {
        if (stopped_) return;
        const auto result = watcher_.result();
        if (runningGeneration_ == generation_) {
            error_ = result.error;
            if (result.seed.isValid()) {
                applySeed(result.seed);
                retryStep_ = 0;
                if (!result.cacheKey.isEmpty()) {
                    auto entries = settings_.value("ui/appearance/cache").toMap();
                    if (entries.size() >= 16 && !entries.contains(result.cacheKey)) entries.erase(entries.begin());
                    entries.insert(result.cacheKey, result.seed.name());
                    settings_.setValue("ui/appearance/cache", entries);
                    settings_.sync();
                }
            } else if (result.retryable) {
                const int delays[] = {5000, 30000, 60000};
                retry_.start(delays[std::min(retryStep_++, 2)]);
            }
        }
        emit changed();
        if (pending_) {
            pending_ = false;
            resolve();
        }
    });
    poll_.start();
    debounce_.start();
}
bool ThemeController::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (event->message == WM_SETTINGCHANGE || event->message == WM_DISPLAYCHANGE || event->message == WM_THEMECHANGED) schedule();
#else
    Q_UNUSED(message)
#endif
    return false;
}
ThemeController::~ThemeController() {
#ifdef Q_OS_WIN
    qGuiApp->removeNativeEventFilter(this);
#endif
    stopped_ = true;
    if (cancelled_) cancelled_->store(true);
    watcher_.waitForFinished();
}
void ThemeController::save() {
    settings_.setValue("ui/appearance/mode", mode_);
    settings_.setValue("ui/appearance/source", source_);
    settings_.setValue("ui/appearance/color", custom_.name());
    settings_.setValue("ui/appearance/image", image_);
    settings_.setValue("ui/appearance/reducedMotion", reduced_);
    settings_.sync();
}
void ThemeController::setMode(const QString& value) {
    if ((value != "system" && value != "light" && value != "dark") || value == mode_) return;
    mode_ = value;
    save();
    emit changed();
}
void ThemeController::setSource(const QString& value) {
    if ((value != "wallpaper" && value != "image" && value != "color") || value == source_) return;
    source_ = value;
    save();
    schedule();
}
void ThemeController::setCustomColor(const QColor& value) {
    if (!value.isValid()) return;
    custom_ = value;
    source_ = "color";
    save();
    schedule();
}
void ThemeController::setImagePath(const QString& value) {
    image_ = value;
    source_ = "image";
    save();
    schedule();
}
void ThemeController::chooseImage(const QUrl& url) {
    if (url.isLocalFile()) setImagePath(url.toLocalFile());
}
void ThemeController::setReducedMotion(bool value) {
    if (value == reduced_) return;
    reduced_ = value;
    save();
    emit changed();
}
void ThemeController::applySeed(const QColor& color) {
    if (seed_ == color && !lightColors_.isEmpty()) return;
    seed_ = color;
    lightColors_ = palette(seed_, false);
    darkColors_ = palette(seed_, true);
    settings_.setValue("ui/appearance/lastSeed", seed_.name());
    settings_.sync();
}
bool ThemeController::dark() const {
    if (mode_ != "system") return mode_ == "dark";
    const auto system = qGuiApp->styleHints()->colorScheme();
    if (system != Qt::ColorScheme::Unknown) return system == Qt::ColorScheme::Dark;
    const auto text = readText(omarchy_ + "/theme/colors.toml");
    auto mode = field(text, "mode");
    if (mode.isEmpty()) mode = field(text, "theme_type");
    if (mode == "light" || mode == "dark") return mode == "dark";
    if (QFileInfo::exists(omarchy_ + "/theme/light.mode")) return false;
    const QRegularExpression background(R"((?:^|\n)\s*(?:background|bg)\s*=\s*["'](#[0-9a-fA-F]{6}))");
    const QColor color(background.match(text).captured(1));
    return !color.isValid() || color.red() + color.green() + color.blue() <= 382;
}
QString ThemeController::status() const {
    if (error_ == "limits") return tr("Изображение превышает ограничения размера. Сохранена предыдущая палитра.");
    if (error_ == "unsupported") return tr("Автоматические обои недоступны. Выберите картинку или цвет.");
    if (!error_.isEmpty()) return tr("Не удалось прочитать фон. Сохранена предыдущая палитра; можно повторить.");
    if (analyzing()) return tr("Подбираем цвета…");
    return tr("Цвета обрабатываются только на этом компьютере.");
}
void ThemeController::schedule() {
    ++generation_;
    signature_.clear();
    retry_.stop();
    retryStep_ = 0;
    if (cancelled_) cancelled_->store(true);
    debounce_.start();
    emit changed();
}
void ThemeController::refresh() { schedule(); }
void ThemeController::watch(const QString& path) {
    QStringList candidates;
    if (QFileInfo::exists(omarchy_)) candidates = {omarchy_, QFileInfo(omarchy_).absolutePath(), omarchy_ + "/theme", omarchy_ + "/theme/colors.toml", omarchy_ + "/theme/light.mode"};
    if (!path.isEmpty()) {
        candidates.append(path);
        candidates.append(QFileInfo(path).absolutePath());
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty()) {
            candidates.append(canonical);
            candidates.append(QFileInfo(canonical).absolutePath());
        }
    }
    QStringList wanted;
    for (const auto& item : candidates)
        if (QFileInfo::exists(item)) wanted.append(item);
    wanted.removeDuplicates();
    const auto old = files_.files() + files_.directories();
    for (const auto& item : old)
        if (!wanted.contains(item)) files_.removePath(item);
    for (const auto& item : wanted)
        if (!old.contains(item)) files_.addPath(item);
}
void ThemeController::resolve() {
    emit changed();
    QString path = image_;
    if (source_ == "wallpaper") {
#ifdef Q_OS_WIN
        const auto wallpaper = windowsWallpaper();
        path = wallpaper.path;
        if (path.isEmpty() && wallpaper.color.isValid()) {
            ++generation_;
            applySeed(wallpaper.color);
            error_.clear();
            emit changed();
            return;
        }
#else
        path = omarchy_ + "/background";
#endif
    }
    watch(source_ == "color" ? QString{} : path);
    if (source_ == "color") {
        applySeed(custom_);
        error_.clear();
        emit changed();
        return;
    }
    if (source_ == "wallpaper" && !QFileInfo::exists(path)) {
        error_ = "unsupported";
        signature_.clear();
        retry_.start(30000);
        emit changed();
        return;
    }
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        error_ = "unavailable";
        signature_.clear();
        retry_.start(5000);
        emit changed();
        return;
    }
    path = info.absoluteFilePath();
    const auto signature = path + info.canonicalFilePath() + QString::number(info.size()) + QString::number(info.lastModified().toMSecsSinceEpoch());
    if (watcher_.isRunning()) {
        if (signature != signature_) {
            ++generation_;
            if (cancelled_) cancelled_->store(true);
            pending_ = true;
        }
        return;
    }
    if (signature == signature_ && error_.isEmpty()) return;
    signature_ = signature;
    runningGeneration_ = generation_;
    const auto entries = settings_.value("ui/appearance/cache").toMap();
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancellation = cancelled_;
    const auto tools = tools_;
    watcher_.setFuture(QtConcurrent::run([path, tools, cancellation, entries] {
        try {
            if (cancellation->load()) return ThemeResult{};
            const auto suffix = QFileInfo(path).suffix().toLower();
            if (QStringList{"mp4", "m4v", "mov", "webm", "mkv", "avi"}.contains(suffix)) return videoFrame(path, tools, cancellation);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return ThemeResult{{}, "unavailable", true};
            if (file.size() > 64 * 1024 * 1024) return ThemeResult{{}, "limits", false};
            QByteArray bytes;
            while (!file.atEnd()) {
                if (cancellation->load()) return ThemeResult{};
                const auto block = file.read(65536);
                if (block.isEmpty() && file.error() != QFileDevice::NoError) return ThemeResult{{}, "unavailable", true};
                bytes += block;
                if (bytes.size() > 64 * 1024 * 1024) return ThemeResult{{}, "limits", false};
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(QByteArray(cacheVersion));
            hash.addData(bytes);
            const auto key = QString::fromLatin1(hash.result().toHex());
            const QColor cached(entries.value(key).toString());
            if (cached.isValid()) return ThemeResult{cached, {}, false, key};
            auto result = decodeImage(bytes);
            result.cacheKey = key;
            return result;
        } catch (const std::exception&) { return ThemeResult{{}, "decode", true}; }
    }));
    emit changed();
}
