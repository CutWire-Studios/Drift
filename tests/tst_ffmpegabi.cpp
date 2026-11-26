#include <QtTest>

#include <QMediaFormat>
#include <QRegularExpression>
#include <QSet>

extern "C" {
#include <libavcodec/avcodec.h>
}

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif

// Drift links FFmpeg itself and also loads Qt Multimedia's FFmpeg backend for audio output. If
// QT_VERSION and FFMPEG_TAG drift apart, the plugin pulls in a second libavcodec with another
// soname and the process ends up with two FFmpeg ABIs.
static QStringList loadedModules()
{
    QStringList out;
#if defined(Q_OS_WIN)
    HMODULE mods[4096];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        const DWORD n = qMin<DWORD>(needed / sizeof(HMODULE), DWORD(std::size(mods)));
        for (DWORD i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(mods[i], path, MAX_PATH))
                out << QString::fromWCharArray(path);
        }
    }
#elif defined(Q_OS_MACOS)
    for (uint32_t i = 0; i < _dyld_image_count(); ++i)
        out << QString::fromUtf8(_dyld_get_image_name(i));
#else
    dl_iterate_phdr([](dl_phdr_info *info, size_t, void *data) {
        static_cast<QStringList *>(data)->append(QString::fromUtf8(info->dlpi_name));
        return 0;
    }, &out);
#endif
    return out;
}

class TestFFmpegAbi : public QObject
{
    Q_OBJECT

private slots:
    void qtBackendSharesDriftsLibavcodec()
    {
        // Asking for decode formats makes Qt load its media backend plugin.
        QVERIFY(!QMediaFormat().supportedFileFormats(QMediaFormat::Decode).isEmpty());

        const int driftMajor = LIBAVCODEC_VERSION_MAJOR;
        QCOMPARE(int(AV_VERSION_MAJOR(avcodec_version())), driftMajor);

        // libavcodec.so.61 / libavcodec.61.dylib / avcodec-61.dll
        static const QRegularExpression avcodec(QStringLiteral("avcodec(?:\\.so)?[.-](\\d+)"));
        bool backendLoaded = false;
        QSet<int> majors;
        QStringList avcodecPaths;
        for (const QString &path : loadedModules()) {
            const QString name = QFileInfo(path).fileName();
            if (name.contains(QLatin1String("ffmpegmediaplugin")))
                backendLoaded = true;
            const auto m = avcodec.match(name);
            if (m.hasMatch()) {
                majors.insert(m.captured(1).toInt());
                avcodecPaths << path;
            }
        }

        QVERIFY2(backendLoaded, "Qt Multimedia did not load its FFmpeg backend");
        QVERIFY2(majors == QSet<int>{driftMajor},
                 qPrintable(QStringLiteral("Drift is built against libavcodec %1 but the process has: %2. "
                                           "QT_VERSION and FFMPEG_TAG must use the same FFmpeg branch "
                                           "(doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html).")
                                .arg(driftMajor)
                                .arg(avcodecPaths.join(QLatin1String(", ")))));
    }
};

QTEST_GUILESS_MAIN(TestFFmpegAbi)
#include "tst_ffmpegabi.moc"
