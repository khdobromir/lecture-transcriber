#include "theme.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

namespace {
double luminance(const QColor& color) {
    const auto linear = [](double channel) { return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}
double contrast(const QColor& a, const QColor& b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
void image(const QString& path, const QColor& color) {
    QImage picture(64, 32, QImage::Format_ARGB32);
    picture.fill(color);
    QVERIFY(picture.save(path));
}
} // namespace
class ThemeTests : public QObject {
    Q_OBJECT
private slots:
    void palettesHaveUsableContrast() {
        for (const auto* seed : {"#6750a4", "#0061a4", "#386a20", "#984061", "#000000", "#ffffff", "#ff0000", "#00ff00"}) {
            for (const bool dark : {false, true}) {
                const auto roles = ThemeController::palette(QColor(seed), dark);
                for (const auto* role : {"primary", "secondary", "tertiary", "primaryContainer", "secondaryContainer", "tertiaryContainer", "error", "errorContainer", "surface"}) {
                    const auto name = QString::fromLatin1(role);
                    auto on = name;
                    on[0] = on[0].toUpper();
                    on.prepend("on");
                    QVERIFY2(contrast(roles.value(name).value<QColor>(), roles.value(on).value<QColor>()) >= 4.5, qPrintable(name + ' ' + seed));
                }
                QVERIFY(contrast(roles.value("outline").value<QColor>(), roles.value("surface").value<QColor>()) >= 3.0);
                QVERIFY(contrast(roles.value("primary").value<QColor>(), roles.value("surfaceContainer").value<QColor>()) >= 3.0);
            }
        }
    }
    void imagesAreDeterministicAndTransparentFallsBack() {
        QImage picture(800, 160, QImage::Format_ARGB32);
        picture.fill(QColor("#124ecb"));
        const auto first = ThemeController::imageSeed(picture);
        for (int index = 0; index < 5; ++index) QCOMPARE(ThemeController::imageSeed(picture), first);
        picture.fill(Qt::transparent);
        QCOMPARE(ThemeController::imageSeed(picture), QColor("#6750a4"));
        picture.fill(Qt::gray);
        QCOMPARE(ThemeController::imageSeed(picture), QColor("#6750a4"));
    }
    void supportedDecoders_data() {
        QTest::addColumn<QByteArray>("format");
        for (const auto* type : {"png", "jpeg", "webp", "bmp"}) QTest::newRow(type) << QByteArray(type);
    }
    void supportedDecoders() {
        QFETCH(QByteArray, format);
        QTemporaryDir temp;
        QImage picture(32, 64, QImage::Format_RGB32);
        picture.fill(QColor("#1265bd"));
        const auto path = temp.filePath("sample." + format);
        QVERIFY2(picture.save(path, format.constData()), format.constData());
        const auto result = ThemeController::readImage(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.seed.isValid());
    }
    void rejectsUnavailableAndOversizedInput() {
        QTemporaryDir temp;
        QVERIFY(ThemeController::readImage(temp.filePath("missing.png")).retryable);
        QFile huge(temp.filePath("huge.png"));
        QVERIFY(huge.open(QIODevice::WriteOnly));
        QVERIFY(huge.resize(64 * 1024 * 1024 + 1));
        huge.close();
        const auto result = ThemeController::readImage(huge.fileName());
        QCOMPARE(result.error, QString("limits"));
        QVERIFY(!result.retryable);
        const auto large = temp.filePath("large.png");
        QImage tooManyPixels(8192, 4096, QImage::Format_Grayscale8);
        tooManyPixels.fill(Qt::black);
        QVERIFY(tooManyPixels.save(large));
        QCOMPARE(ThemeController::readImage(large).error, QString("limits"));
        const auto gif = temp.filePath("first-frame.gif");
        QFile gifFile(gif);
        QVERIFY(gifFile.open(QIODevice::WriteOnly));
        gifFile.write(QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"));
        gifFile.close();
        QVERIFY(ThemeController::readImage(gif).seed.isValid());
        QFile corrupt(temp.filePath("bad.png"));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("broken");
        corrupt.close();
        QVERIFY(!ThemeController::readImage(corrupt.fileName()).seed.isValid());
    }
    void retriesTransientDecodeWithoutFileChange() {
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.filePath("pictures")));
        const auto path = temp.filePath("pictures/truncated.png");
        QFile truncated(path);
        QVERIFY(truncated.open(QIODevice::WriteOnly));
        truncated.write("not yet decodable");
        truncated.close();
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        ThemeController theme(settings, "", nullptr, temp.filePath("desktop"));
        theme.setImagePath(path);
        QSignalSpy changed(&theme, &ThemeController::changed);
        QTest::qWait(1000);
        const auto before = changed.count();
        QTest::qWait(5500);
        QVERIFY2(changed.count() > before, "A transient decode failure must retry even without a file event");
    }
    void settingsAndLastPaletteSurviveRestart() {
        QTemporaryDir temp;
        const auto path = temp.filePath("settings.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            ThemeController theme(settings, "", nullptr, temp.filePath("desktop"));
            theme.setMode("light");
            theme.setCustomColor(QColor("#386a20"));
            theme.setReducedMotion(true);
            QTRY_COMPARE(theme.colors(), ThemeController::palette(QColor("#386a20"), false));
        }
        QSettings settings(path, QSettings::IniFormat);
        ThemeController theme(settings, "", nullptr, temp.filePath("desktop"));
        QCOMPARE(theme.mode(), QString("light"));
        QCOMPARE(theme.source(), QString("color"));
        QVERIFY(theme.reducedMotion());
        QCOMPARE(theme.colors(), ThemeController::palette(QColor("#386a20"), false));
        theme.setMode("nonsense");
        QCOMPARE(theme.mode(), QString("light"));
        theme.setCustomColor(QColor());
        QCOMPARE(theme.customColor(), QColor("#386a20"));
    }
    void newestImageWinsAndMissingRetainsPalette() {
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.filePath("pictures")));
        const auto a = temp.filePath("pictures/a.png"), b = temp.filePath("pictures/b.png");
        image(a, QColor("#ff0000"));
        image(b, QColor("#00ff00"));
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        ThemeController theme(settings, "", nullptr, temp.filePath("desktop"));
        theme.setMode("dark");
        theme.setImagePath(a);
        theme.setImagePath(b);
        const auto expected = ThemeController::palette(ThemeController::readImage(b).seed, true);
        QTRY_COMPARE(theme.colors(), expected);
        theme.setImagePath(temp.filePath("pictures/missing.png"));
        QTest::qWait(600);
        QCOMPARE(theme.colors(), expected);
        image(temp.filePath("pictures/missing.png"), QColor("#0044ff"));
        QTRY_COMPARE(theme.colors(), ThemeController::palette(ThemeController::readImage(temp.filePath("pictures/missing.png")).seed, true));
    }
#ifndef Q_OS_WIN
    void videoProbeIsCancelledOnTeardown() {
        QTemporaryDir temp;
        const auto tools = temp.filePath("tools");
        QVERIFY(QDir().mkpath(tools));
        const auto started = temp.filePath("started");
        QFile script(tools + "/ffprobe");
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(("#!/bin/sh\necho ready > '" + started + "'\nexec /usr/bin/sleep 30\n").toUtf8());
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QFile video(temp.filePath("local.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("fixture");
        video.close();
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto theme = std::make_unique<ThemeController>(settings, tools, nullptr, temp.filePath("desktop"));
        theme->setImagePath(video.fileName());
        QTRY_VERIFY(QFileInfo::exists(started));
        QElapsedTimer elapsed;
        elapsed.start();
        theme.reset();
        QVERIFY2(elapsed.elapsed() < 2000, "Superseded video probes must not wait for the full process timeout");
    }
    void wallpaperSymlinkReplacementAndTargetChanges() {
        QTemporaryDir temp;
        const auto root = temp.filePath("desktop");
        QVERIFY(QDir().mkpath(root + "/theme"));
        image(root + "/theme/a.png", QColor("#ef3211"));
        image(root + "/theme/b.png", QColor("#14ba56"));
        QVERIFY(QFile::link(root + "/theme/a.png", root + "/background"));
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        ThemeController theme(settings, "", nullptr, root);
        theme.setMode("light");
        QTRY_COMPARE(theme.colors(), ThemeController::palette(ThemeController::readImage(root + "/theme/a.png").seed, false));
        QVERIFY(QFile::remove(root + "/background"));
        QVERIFY(QFile::link(root + "/theme/b.png", root + "/background"));
        QTRY_COMPARE(theme.colors(), ThemeController::palette(ThemeController::readImage(root + "/theme/b.png").seed, false));
        image(root + "/theme/b.png", QColor("#652add"));
        QTRY_COMPARE(theme.colors(), ThemeController::palette(ThemeController::readImage(root + "/theme/b.png").seed, false));
    }
#endif
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    ThemeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_theme.moc"
