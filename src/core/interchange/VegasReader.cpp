#include "core/interchange/VegasReader.h"

#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "engine/decode/probe/MediaProbe.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QUuid>
#include <cstring>

namespace drift::vegas {

namespace {

// 16-byte Sony/MAGIX Wave64 RIFF chunk and type GUIDs (little-endian)
static const uint8_t kGuidW64Riff[16] = {
    0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kGuidW64List[16] = {
    0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kTypeMediaPool[16] = {
    0x5b, 0x2d, 0x8f, 0xb2, 0x0f, 0x23, 0xd2, 0x11, 0x86, 0xaf, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTimeline[16] = {
    0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTrack[16] = {
    0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventContainer[16] = {
    0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEvent[16] = {
    0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTime[16] = {
    0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTake[16] = {
    0x6d, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventGroupList[16] = {
    0x18, 0x3e, 0xe7, 0xb8, 0x4c, 0x7f, 0x6b, 0x45, 0x93, 0xcd, 0x68, 0x66, 0xb9, 0x5e, 0x5f, 0x2b
};
static const uint8_t kChunkEventGroup[16] = {
    0x32, 0xe1, 0x01, 0xe9, 0x77, 0xc6, 0x04, 0x46, 0xac, 0x9d, 0xdb, 0x0b, 0x87, 0xb4, 0xee, 0xbd
};
static const uint8_t kListAudioTrack[16] = {
    0x26, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkAudioTrackProps[16] = {
    0x27, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeMarkerList[16] = {
    0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
};
static const uint8_t kGuidMarker[16] = {
    0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};

inline quint32 readU32LE(const uint8_t *p)
{
    return static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

inline quint64 readU64LE(const uint8_t *p)
{
    const quint64 lo = readU32LE(p);
    const quint64 hi = readU32LE(p + 4);
    return lo | (hi << 32);
}

inline double readDoubleLE(const uint8_t *p)
{
    const quint64 val = readU64LE(p);
    double d = 0.0;
    std::memcpy(&d, &val, sizeof(d));
    return d;
}

inline float readFloatLE(const uint8_t *p)
{
    const quint32 val = readU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &val, sizeof(f));
    return f;
}

QString readUtf16String(const uint8_t *data, qsizetype size, qsizetype offset)
{
    if (offset + 2 > size)
        return {};
    const auto *chars = reinterpret_cast<const char16_t *>(data + offset);
    const qsizetype maxChars = (size - offset) / 2;
    qsizetype len = 0;
    while (len < maxChars && chars[len] != 0) {
        ++len;
    }
    return QString::fromUtf16(chars, len);
}

struct ParsedMarker
{
    quint32 id = 0;
    TimeUs posUs = 0;
    TimeUs durUs = 0;
    bool isRegion = false;
    QString name;
};

std::optional<ParsedMarker> parseMarkerPayload(const uint8_t *p, qsizetype avail)
{
    if (avail < 28)
        return std::nullopt;

    quint32 ident = 0;
    qint64 posTicks = 0;
    qint64 lenTicks = -1;
    quint32 nameBytes = 0;
    const uint8_t *namePtr = nullptr;

    const quint32 val0 = readU32LE(p);
    const quint32 val4 = readU32LE(p + 4);
    const quint32 val8 = (avail >= 12) ? readU32LE(p + 8) : 0;

    if (val4 == 28 && avail >= 32) {
        // Format A (property format): uint32 dataSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 8);
        posTicks = static_cast<qint64>(readU64LE(p + 12));
        lenTicks = static_cast<qint64>(readU64LE(p + 20));
        nameBytes = readU32LE(p + 28);
        namePtr = p + 32;
    } else if (val8 == 28 && avail >= 36) {
        // Format B (Wave64 64-bit chunk): uint64 chunkSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 12);
        posTicks = static_cast<qint64>(readU64LE(p + 16));
        lenTicks = static_cast<qint64>(readU64LE(p + 24));
        nameBytes = readU32LE(p + 32);
        namePtr = p + 36;
    } else if (val0 == 28 && avail >= 28) {
        // Format C (direct payload): uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 4);
        posTicks = static_cast<qint64>(readU64LE(p + 8));
        lenTicks = static_cast<qint64>(readU64LE(p + 16));
        nameBytes = readU32LE(p + 24);
        namePtr = p + 28;
    } else {
        return std::nullopt;
    }

    if (posTicks < 0)
        posTicks = 0;

    ParsedMarker m;
    m.id = ident;
    m.posUs = static_cast<TimeUs>(posTicks / 10);
    if (lenTicks > 0) {
        m.isRegion = true;
        m.durUs = static_cast<TimeUs>(lenTicks / 10);
    } else {
        m.isRegion = false;
        m.durUs = 0;
    }

    if (nameBytes > 0 && namePtr && (namePtr - p) + static_cast<qsizetype>(nameBytes) <= avail) {
        m.name = readUtf16String(namePtr, nameBytes, 0).trimmed();
    }

    return m;
}

QString extractPathFromPayload(const uint8_t *data, qsizetype size)
{
    if (size < 4)
        return {};

    // 1. Search for UTF-16LE Windows drive separator ":\" (0x3a, 0x00, 0x5c, 0x00) or ":/" (0x3a, 0x00, 0x2f, 0x00)
    static const uint8_t kColonBackslash[4] = { 0x3a, 0x00, 0x5c, 0x00 };
    static const uint8_t kColonSlash[4] = { 0x3a, 0x00, 0x2f, 0x00 };
    for (qsizetype i = 0; i + 4 <= size; i += 2) {
        if (std::memcmp(data + i, kColonBackslash, 4) == 0 || std::memcmp(data + i, kColonSlash, 4) == 0) {
            const qsizetype start = (i >= 2) ? (i - 2) : i;
            const QString path = readUtf16String(data, size, start);
            if (!path.isEmpty())
                return path;
        }
    }

    // 2. Search for UTF-16LE "META:\"
    static const uint8_t kMetaPrefix[12] = { 'M', 0, 'E', 0, 'T', 0, 'A', 0, ':', 0, '\\', 0 };
    for (qsizetype i = 0; i + 12 <= size; i += 2) {
        if (std::memcmp(data + i, kMetaPrefix, 12) == 0) {
            const QString path = readUtf16String(data, size, i);
            if (!path.isEmpty())
                return path;
        }
    }

    // 3. Fallback to common VEGAS payload offsets (96, 102, 128, 134, 64)
    for (const qsizetype off : { 96, 102, 128, 134, 64 }) {
        if (off + 2 <= size) {
            const QString s = readUtf16String(data, size, off);
            if (!s.isEmpty() && (s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\'))))
                return s;
        }
    }

    return {};
}

QString cleanMediaFilename(const QString &rawPath)
{
    QString s = rawPath;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Handle META:\SubClip\{<path>}
    const int openBrace = s.indexOf(QLatin1Char('{'));
    const int closeBrace = s.indexOf(QLatin1Char('}'), openBrace + 1);
    if (openBrace >= 0 && closeBrace > openBrace) {
        const QString inside = s.mid(openBrace + 1, closeBrace - openBrace - 1);
        if (inside.contains(QLatin1Char('/')) || inside.contains(QLatin1Char('.')))
            s = inside;
    }

    QString diskName = QFileInfo(s).fileName();
    const int braceIdx = diskName.indexOf(QLatin1Char('}'));
    if (braceIdx > 0 && !diskName.startsWith(QLatin1Char('{')))
        diskName = diskName.left(braceIdx);
    return diskName;
}

QString resolveMediaFile(const QString &winPath, const QString &diskName,
                         const QString &sourceDir, const QList<QDir> &searchDirs,
                         const QHash<QString, QStringList> &indexedFiles)
{
    if (diskName.isEmpty())
        return {};

    const QString normWin = QString(winPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QFileInfo diskInfo(diskName);
    const QString baseName = diskInfo.completeBaseName();
    const QString ext = diskInfo.suffix().toLower();
    const QString movName = baseName + QStringLiteral(".mov");

    const bool isMov = (ext == QLatin1String("mov"));
    // Audio, images, or documents should not be redirected to .mov
    const bool isVideoCandidate = !isMov && (ext == QLatin1String("mts") || ext == QLatin1String("m2ts")
                                            || ext == QLatin1String("m2t") || ext == QLatin1String("ts")
                                            || ext == QLatin1String("mp4") || ext == QLatin1String("m4v")
                                            || ext == QLatin1String("avi") || ext == QLatin1String("mkv")
                                            || ext == QLatin1String("wmv") || ext == QLatin1String("flv")
                                            || ext == QLatin1String("webm") || ext == QLatin1String("vob")
                                            || ext.isEmpty());

    // Subpath relative matches from winPath
    // E.g. "G:/Against the Odds/Episode 4/Day 4/00017.MTS" -> checks "Day 4/00017.mov" vs "Day 4/00017.MTS"
    QStringList movSubpathCandidates;
    QStringList origSubpathCandidates;
    const QStringList winParts = normWin.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 1; i <= std::min(qsizetype(4), winParts.size()); ++i) {
        const QString sub = winParts.mid(winParts.size() - i).join(QLatin1Char('/'));
        origSubpathCandidates.append(sub);
        const QFileInfo subInfo(sub);
        const QString subDir = subInfo.path();
        const QString movSub = (subDir.isEmpty() || subDir == QLatin1String("."))
                               ? (subInfo.completeBaseName() + QStringLiteral(".mov"))
                               : (subDir + QLatin1Char('/') + subInfo.completeBaseName() + QStringLiteral(".mov"));
        movSubpathCandidates.append(movSub);
    }

    // 1. If video media, check for converted ProRes .mov FIRST (in same dir, Transcodes_ProRes, or matching subpaths)
    if (isVideoCandidate) {
        for (const QDir &dir : searchDirs) {
            if (!dir.exists())
                continue;
            for (const QString &movSub : movSubpathCandidates) {
                if (dir.exists(movSub))
                    return dir.filePath(movSub);
            }
            // Bare movName is valid if in sourceDir or in a Transcodes_ProRes directory
            if (dir == QDir(sourceDir) || dir.dirName() == QLatin1String("Transcodes_ProRes")) {
                if (dir.exists(movName))
                    return dir.filePath(movName);
            }
        }

        // Fast lookup from project tree index for .mov only when matching parent subdirectory
        const QString movLow = movName.toLower();
        if (indexedFiles.contains(movLow)) {
            const QStringList &candidates = indexedFiles.value(movLow);
            for (const QString &c : candidates) {
                if (winParts.size() >= 2 && c.contains(winParts.at(winParts.size() - 2), Qt::CaseInsensitive))
                    return c;
            }
        }

        // Cross-drive fallback (Jellyfish <-> HAL) for .mov
        if (!sourceDir.isEmpty()) {
            QString altSource = sourceDir;
            if (altSource.startsWith(QStringLiteral("/mnt/Jellyfish")))
                altSource.replace(0, 14, QStringLiteral("/mnt/HAL"));
            else if (altSource.startsWith(QStringLiteral("/mnt/HAL")))
                altSource.replace(0, 8, QStringLiteral("/mnt/Jellyfish"));

            if (altSource != sourceDir && QDir(altSource).exists()) {
                const QDir altDir(altSource);
                for (const QString &movSub : movSubpathCandidates) {
                    if (altDir.exists(movSub))
                        return altDir.filePath(movSub);
                }
                if (altDir == QDir(altSource) || altDir.dirName() == QLatin1String("Transcodes_ProRes")) {
                    if (altDir.exists(movName))
                        return altDir.filePath(movName);
                }
            }
        }
    }

    // 2. Direct absolute path match for original file
    const QFileInfo direct(diskName);
    if (direct.isAbsolute() && direct.exists())
        return direct.canonicalFilePath();

    const QFileInfo winInfo(normWin);
    if (winInfo.isAbsolute() && winInfo.exists())
        return winInfo.canonicalFilePath();

    // 3. Search directories for original file
    for (const QDir &dir : searchDirs) {
        if (!dir.exists())
            continue;
        for (const QString &sub : origSubpathCandidates) {
            if (dir.exists(sub))
                return dir.filePath(sub);
        }
        if (dir.exists(diskName))
            return dir.filePath(diskName);
    }

    // 4. Fast lookup from project tree index for original filename
    const QString lowName = diskName.toLower();
    if (indexedFiles.contains(lowName)) {
        const QStringList &candidates = indexedFiles.value(lowName);
        for (const QString &c : candidates) {
            if (winParts.size() >= 2 && c.contains(winParts.at(winParts.size() - 2), Qt::CaseInsensitive))
                return c;
        }
        return candidates.first();
    }

    // 5. Cross-drive fallback for original file
    if (!sourceDir.isEmpty()) {
        QString altSource = sourceDir;
        if (altSource.startsWith(QStringLiteral("/mnt/Jellyfish")))
            altSource.replace(0, 14, QStringLiteral("/mnt/HAL"));
        else if (altSource.startsWith(QStringLiteral("/mnt/HAL")))
            altSource.replace(0, 8, QStringLiteral("/mnt/Jellyfish"));

        if (altSource != sourceDir && QDir(altSource).exists()) {
            const QDir altDir(altSource);
            for (const QString &sub : origSubpathCandidates) {
                if (altDir.exists(sub))
                    return altDir.filePath(sub);
            }
            if (altDir.exists(diskName))
                return altDir.filePath(diskName);
        }
    }

    return diskName;
}

} // namespace

bool isVegasData(const QByteArray &data)
{
    if (data.size() < 40)
        return false;
    return std::memcmp(data.constData(), kGuidW64Riff, 16) == 0;
}

bool isVegasProject(const QString &filePath)
{
    if (filePath.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive))
        return true;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(40);
    return isVegasData(header);
}

std::optional<Project> readProject(const QString &filePath, QString *error,
                                   QSet<QString> visitedPaths, int depth)
{
    const QString canon = QFileInfo(filePath).canonicalFilePath();
    if (!canon.isEmpty())
        visitedPaths.insert(canon);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open file: ") + filePath;
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).canonicalPath();
    return readProjectData(data, sourceDir, error, visitedPaths, depth);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir,
                                       QString *error,
                                       QSet<QString> visitedPaths, int depth)
{
    if (data.size() < 64 || !isVegasData(data)) {
        if (error)
            *error = QStringLiteral("Not a valid Sony/MAGIX Wave64 RIFF project");
        return std::nullopt;
    }

    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    const qsizetype totalSize = data.size();

    // Chunk 0: Project settings (starts at offset 40)
    const quint64 c0Size = readU64LE(bytes + 56);
    if (c0Size < 24 || 64 + c0Size - 24 > static_cast<quint64>(totalSize)) {
        if (error)
            *error = QStringLiteral("Corrupted Wave64 project header");
        return std::nullopt;
    }

    const uint8_t *c0Payload = bytes + 64;
    quint32 sampleRate = 48000;
    if (c0Size >= 40)
        sampleRate = readU32LE(c0Payload + 12);
    if (sampleRate < 8000 || sampleRate > 192000)
        sampleRate = 48000;

    double fps = 23.976023976023978;
    if (c0Size >= 48) {
        const double readFps = readDoubleLE(c0Payload + 16);
        if (readFps >= 1.0 && readFps <= 240.0)
            fps = readFps;
    }

    int canvasWidth = 1920;
    int canvasHeight = 1080;
    if (c0Size >= 472) {
        const quint32 wCand = readU32LE(c0Payload + 440);
        const quint32 hCand = readU32LE(c0Payload + 444);
        if (wCand >= 320 && wCand <= 7680 && hCand >= 240 && hCand <= 4320) {
            canvasWidth = static_cast<int>(wCand);
            canvasHeight = static_cast<int>(hCand);
        }
    }

    Project project;
    project.setResolution(canvasWidth, canvasHeight);
    project.setFps(qRound(fps));
    project.setSampleRate(static_cast<int>(sampleRate));

    // Search directories for media resolution
    QList<QDir> searchDirs;
    if (!sourceDir.isEmpty()) {
        const QDir dir(sourceDir);
        searchDirs.append(dir);
        searchDirs.append(QDir(dir.filePath(QStringLiteral("Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../Transcodes_ProRes/") + dir.dirName())));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../../Transcodes_ProRes/") + dir.dirName())));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../../Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral(".."))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../.."))));
        searchDirs.append(QDir(QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Transcodes_ProRes/") + dir.dirName()));
        searchDirs.append(QDir(QStringLiteral("/mnt/Jellyfish/Videos/Odds/Episode 3/Remaster/Transcodes_ProRes")));
        searchDirs.append(QDir(QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Transcodes_ProRes/") + dir.dirName()));
        searchDirs.append(QDir(QStringLiteral("/mnt/HAL/Videos/Odds/Episode 3/Remaster/Transcodes_ProRes")));
    }

    // Index filenames in sourceDir and nearby project directories for fast resolution
    QHash<QString, QStringList> indexedFiles;
    if (!sourceDir.isEmpty()) {
        auto indexDirectory = [&](const QString &dirPath) {
            if (dirPath.isEmpty() || !QDir(dirPath).exists())
                return;
            QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            int count = 0;
            while (it.hasNext() && count < 10000) {
                it.next();
                ++count;
                indexedFiles[it.fileName().toLower()].append(it.filePath());
            }
        };

        indexDirectory(sourceDir);
        QDir parentDir(sourceDir);
        if (parentDir.cdUp()) {
            indexDirectory(parentDir.absolutePath());
            QDir grandParent(parentDir);
            if (grandParent.cdUp())
                indexDirectory(grandParent.absolutePath());
        }

        // Cross-drive index (Jellyfish <-> HAL)
        QString altDir = sourceDir;
        if (altDir.startsWith(QStringLiteral("/mnt/Jellyfish")))
            altDir.replace(0, 14, QStringLiteral("/mnt/HAL"));
        else if (altDir.startsWith(QStringLiteral("/mnt/HAL")))
            altDir.replace(0, 8, QStringLiteral("/mnt/Jellyfish"));
        if (altDir != sourceDir && QDir(altDir).exists()) {
            indexDirectory(altDir);
            QDir altParent(altDir);
            if (altParent.cdUp())
                indexDirectory(altParent.absolutePath());
        }
    }

    // 1. Parse Media Pool
    struct MediaEntry
    {
        QString rawFilename;
        QString resolvedPath;
        QString assetId;
        QString sequenceId;
        bool isNestedProject = false;
    };
    QMap<quint32, MediaEntry> mediaPool;

    qsizetype pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeMediaPool, 16) == 0) {
            qsizetype cpos = pos + 40;
            const qsizetype cend = pos + static_cast<qsizetype>(chunkSize);
            while (cpos + 24 <= cend) {
                const uint8_t *cptr = bytes + cpos;
                const quint64 csize = readU64LE(cptr + 16);
                if (csize < 24 || cpos + static_cast<qsizetype>(csize) > cend)
                    break;

                const uint8_t *payload = cptr + 24;
                const qsizetype payloadSize = static_cast<qsizetype>(csize - 24);
                if (payloadSize >= 64) {
                    const quint32 mid = readU32LE(payload + 60);
                    const QString winPath = extractPathFromPayload(payload, payloadSize);
                    QString diskName = cleanMediaFilename(winPath);
                    if (diskName.isEmpty() || diskName.startsWith(QLatin1Char('{')))
                        diskName = QStringLiteral("media_%1").arg(mid);

                    const QString resolved = resolveMediaFile(winPath, diskName, sourceDir, searchDirs, indexedFiles);
                    const QString assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);

                    const bool isVeg = resolved.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive);
                    if (isVeg && QFile::exists(resolved) && depth < 8) {
                        const QString canon = QFileInfo(resolved).canonicalFilePath();
                        if (!canon.isEmpty() && !visitedPaths.contains(canon)) {
                            QSet<QString> childVisited = visitedPaths;
                            childVisited.insert(canon);
                            QString nestedError;
                            auto optNested = readProject(resolved, &nestedError, childVisited, depth + 1);
                            if (optNested.has_value() && !optNested->tracks().isEmpty()) {
                                drift::Project nestedProj = std::move(*optNested);
                                const QString seqId = project.addSequence(nestedProj.tracks());

                                for (const auto &nAsset : nestedProj.assets()) {
                                    if (!project.assets().contains(nAsset.id)) {
                                        project.assets().insert(nAsset.id, nAsset);
                                        project.assetOrder().append(nAsset.id);
                                    }
                                }

                                for (const QString &subSeqId : nestedProj.sequenceIds()) {
                                    if (!project.hasSequence(subSeqId)) {
                                        project.addSequence(nestedProj.sequenceTracks(subSeqId), subSeqId);
                                    }
                                }

                                MediaAsset asset;
                                asset.id = assetId;
                                asset.kind = MediaKind::Composite;
                                asset.sequenceId = seqId;
                                asset.name = QFileInfo(resolved).completeBaseName();
                                asset.path = resolved;
                                asset.durationUs = nestedProj.sequenceDurationUs({});
                                asset.width = nestedProj.width() > 0 ? nestedProj.width() : canvasWidth;
                                asset.height = nestedProj.height() > 0 ? nestedProj.height() : canvasHeight;
                                asset.fps = nestedProj.fps() > 0 ? nestedProj.fps() : fps;
                                asset.editFriendly = true;

                                project.assets().insert(asset.id, asset);
                                project.assetOrder().append(asset.id);

                                MediaEntry entry;
                                entry.rawFilename = diskName;
                                entry.resolvedPath = resolved;
                                entry.assetId = assetId;
                                entry.sequenceId = seqId;
                                entry.isNestedProject = true;
                                mediaPool.insert(mid, entry);

                                cpos += (csize + 7) & ~7;
                                continue;
                            }
                        }
                    }

                    MediaEntry entry;
                    entry.rawFilename = diskName;
                    entry.resolvedPath = resolved;
                    entry.assetId = assetId;
                    mediaPool.insert(mid, entry);

                    MediaAsset asset;
                    asset.id = assetId;
                    asset.path = resolved;
                    asset.name = QFileInfo(resolved).fileName();

                    const QFileInfo resolvedInfo(resolved);
                    if (resolvedInfo.exists() && resolvedInfo.isFile()) {
                        const MediaInfo probed = MediaProbe::probe(resolved);
                        if (probed.ok) {
                            bool hasVideo = false;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    hasVideo = true;
                                    break;
                                }
                            }
                            asset.kind = hasVideo ? MediaKind::Video : MediaKind::Audio;
                            asset.durationUs = probed.durationUs;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    asset.width = st.width;
                                    asset.height = st.height;
                                    asset.fps = st.fps;
                                    asset.codecName = st.codecName;
                                    if (MediaProbe::isEditFriendlyCodec(st.codecName)
                                        || resolved.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)) {
                                        asset.editFriendly = true;
                                        asset.frameRateKnown = true;
                                        asset.variableFrameRate = false;
                                    }
                                } else if (st.type == StreamInfo::Type::Audio) {
                                    asset.channels = st.channels;
                                    asset.sampleRate = st.sampleRate;
                                    asset.hasAudio = true;
                                    asset.hasAudioKnown = true;
                                }
                            }
                        }
                    } else {
                        const QString ext = resolvedInfo.suffix().toLower();
                        if (ext == QLatin1String("mov") || ext == QLatin1String("mp4")
                            || ext == QLatin1String("mts")
                            || winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive)) {
                            asset.kind = MediaKind::Video;
                            asset.width = canvasWidth;
                            asset.height = canvasHeight;
                            asset.fps = fps;
                            asset.editFriendly = ext == QLatin1String("mov");
                            if (winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive))
                                asset.name = QStringLiteral("Titles & Text");
                        } else {
                            asset.kind = MediaKind::Audio;
                        }
                    }

                    // Extract embedded source markers from media payload if present
                    qsizetype mScan = 0;
                    while (mScan + 16 <= payloadSize) {
                        const void *found = std::memchr(payload + mScan, kGuidMarker[0], payloadSize - mScan);
                        if (!found)
                            break;
                        const qsizetype midx = static_cast<const uint8_t *>(found) - payload;
                        if (midx + 16 > payloadSize)
                            break;
                        if (std::memcmp(payload + midx, kGuidMarker, 16) == 0) {
                            auto optMarker = parseMarkerPayload(payload + midx + 16, payloadSize - (midx + 16));
                            if (optMarker.has_value() && !optMarker->name.isEmpty()
                                && !optMarker->name.startsWith(QLatin1String("Tempo: "))) {
                                SourceMarker sm;
                                sm.id = QString::number(optMarker->id);
                                sm.timeUs = optMarker->posUs;
                                sm.name = optMarker->name;
                                asset.markers.append(sm);
                            }
                        }
                        mScan = midx + 1;
                    }

                    project.assets().insert(asset.id, asset);
                    project.assetOrder().append(asset.id);
                }
                cpos += (csize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    // 2. Parse Event Group List (kTypeEventGroupList)
    QList<QList<quint32>> eventGroups;
    pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeEventGroupList, 16) == 0) {
            qsizetype gpos = pos + 40;
            const qsizetype gend = pos + static_cast<qsizetype>(chunkSize);
            while (gpos + 24 <= gend) {
                const uint8_t *gptr = bytes + gpos;
                const quint64 gsize = readU64LE(gptr + 16);
                if (gsize < 24 || gpos + static_cast<qsizetype>(gsize) > gend)
                    break;

                if (std::memcmp(gptr, kChunkEventGroup, 16) == 0) {
                    const uint8_t *gpayload = gptr + 24;
                    const qsizetype gpayloadSize = static_cast<qsizetype>(gsize - 24);
                    if (gpayloadSize >= 16) {
                        const quint32 eventCount = readU32LE(gpayload + 8);
                        if (eventCount > 0 && gpayloadSize >= static_cast<qsizetype>(16 + eventCount * 8)) {
                            QList<quint32> groupEvents;
                            groupEvents.reserve(eventCount);
                            for (quint32 i = 0; i < eventCount; ++i) {
                                const quint32 evId = readU32LE(gpayload + 16 + i * 8);
                                groupEvents.append(evId);
                            }
                            eventGroups.append(groupEvents);
                        }
                    }
                }
                gpos += (gsize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    // 3. Parse Timeline Tracks & Events
    project.tracks().clear();
    QHash<quint32, QPair<int, int>> eventIdToClipLoc;
    pos = 40;
    int trackNumber = 1;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeTimeline, 16) == 0) {
            qsizetype tpos = pos + 40;
            const qsizetype tend = pos + static_cast<qsizetype>(chunkSize);
            while (tpos + 40 <= tend) {
                const quint64 tsize = readU64LE(bytes + tpos + 16);
                if (tsize < 40 || tpos + static_cast<qsizetype>(tsize) > tend)
                    break;

                if (std::memcmp(bytes + tpos, kGuidW64List, 16) == 0
                    && std::memcmp(bytes + tpos + 24, kTypeTrack, 16) == 0) {
                    // Track Header
                    TrackType trackType = TrackType::Video;
                    bool trackMuted = false;
                    bool trackSolo = false;
                    QString trackCustomName;

                    if (tpos + 64 <= tend) {
                        const quint64 tcsize = readU64LE(bytes + tpos + 56);
                        if (tcsize >= 24 && tpos + 40 + static_cast<qsizetype>(tcsize) <= tend) {
                            const uint8_t *cpayload = bytes + tpos + 64;
                            const qsizetype cpayloadSize = static_cast<qsizetype>(tcsize - 24);

                            if (cpayloadSize >= 8) {
                                const quint32 flags = readU32LE(cpayload + 4);
                                trackMuted = (flags & 0x01) != 0;
                                trackSolo = (flags & 0x02) != 0;
                            }
                            if (cpayloadSize >= 20) {
                                const quint32 nameBytes = readU32LE(cpayload + 16);
                                if (nameBytes > 0 && cpayloadSize >= 56 + static_cast<qsizetype>(nameBytes)) {
                                    const qsizetype strLen = std::min<qsizetype>(cpayloadSize - 56, nameBytes);
                                    trackCustomName = readUtf16String(cpayload + 56, strLen, 0).trimmed();
                                }
                            }
                            if (cpayloadSize >= 36) {
                                const quint32 trackTypeCode = readU32LE(cpayload + 32);
                                trackType = (trackTypeCode == 2) ? TrackType::Video : TrackType::Audio;
                            }
                        }
                    }

                    Track track;
                    track.type = trackType;
                    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    track.number = trackNumber++;
                    track.muted = trackMuted;
                    track.solo = trackSolo;
                    if (!trackCustomName.isEmpty()) {
                        track.name = trackCustomName;
                    } else {
                        track.name = QStringLiteral("Track %1 (%2)")
                                          .arg(track.number)
                                          .arg(trackType == TrackType::Video ? QStringLiteral("video")
                                                                             : QStringLiteral("audio"));
                    }

                    qsizetype evpos = tpos + 40;
                    const qsizetype evend = tpos + static_cast<qsizetype>(tsize);
                    while (evpos + 40 <= evend) {
                        const quint64 evsize = readU64LE(bytes + evpos + 16);
                        if (evsize < 40 || evpos + static_cast<qsizetype>(evsize) > evend)
                            break;

                        if (std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                            && std::memcmp(bytes + evpos + 24, kTypeEventContainer, 16) == 0) {
                            qsizetype epos = evpos + 40;
                            const qsizetype eend = evpos + static_cast<qsizetype>(evsize);
                            while (epos + 40 <= eend) {
                                const quint64 esize = readU64LE(bytes + epos + 16);
                                if (esize < 40 || epos + static_cast<qsizetype>(esize) > eend)
                                    break;

                                if (std::memcmp(bytes + epos, kGuidW64List, 16) == 0
                                    && std::memcmp(bytes + epos + 24, kTypeEvent, 16) == 0) {
                                    qsizetype cpos = epos + 40;
                                    const qsizetype cend = epos + static_cast<qsizetype>(esize);
                                    quint64 startTicks = 0;
                                    quint64 durTicks = 0;
                                    quint64 fadeInTicks = 0;
                                    quint64 fadeOutTicks = 0;
                                    quint64 srcInTicks = 0;
                                    quint32 mid = 0;
                                    quint32 eventId = 0;

                                    while (cpos + 24 <= cend) {
                                        const uint8_t *cptr = bytes + cpos;
                                        const quint64 csize = readU64LE(cptr + 16);
                                        if (csize < 24 || cpos + static_cast<qsizetype>(csize) > cend)
                                            break;

                                        const uint8_t *cpayload = cptr + 24;
                                        const qsizetype cpayloadSize = static_cast<qsizetype>(csize - 24);

                                        if (std::memcmp(cptr, kChunkEventTime, 16) == 0) {
                                            if (cpayloadSize >= 32) {
                                                startTicks = readU64LE(cpayload + 16);
                                                durTicks = readU64LE(cpayload + 24);
                                            }
                                            if (cpayloadSize >= 68) {
                                                eventId = readU32LE(cpayload + 64);
                                            }
                                            if (cpayloadSize >= 152) {
                                                fadeInTicks = readU64LE(cpayload + 136);
                                                fadeOutTicks = readU64LE(cpayload + 144);
                                            }
                                        } else if (std::memcmp(cptr, kChunkEventTake, 16) == 0) {
                                            if (cpayloadSize >= 40) {
                                                srcInTicks = readU64LE(cpayload + 16);
                                                mid = readU32LE(cpayload + 36);
                                            }
                                        }
                                        cpos += (csize + 7) & ~7;
                                    }

                                    if (durTicks > 0) {
                                        Clip clip;
                                        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

                                        bool isNested = false;
                                        QString nestedSeqId;
                                        if (mediaPool.contains(mid)) {
                                            const MediaEntry &m = mediaPool[mid];
                                            clip.assetId = m.assetId;
                                            clip.path = m.resolvedPath;
                                            clip.name = QFileInfo(m.resolvedPath).fileName();
                                            if (m.isNestedProject && !m.sequenceId.isEmpty()) {
                                                isNested = true;
                                                nestedSeqId = m.sequenceId;
                                                clip.name = QFileInfo(m.resolvedPath).completeBaseName();
                                            }
                                        } else {
                                            clip.name = QStringLiteral("clip_%1").arg(track.clips.size());
                                        }

                                        if (isNested) {
                                            if (trackType == TrackType::Video) {
                                                clip.type = ClipType::Composite;
                                                clip.sequenceId = nestedSeqId;
                                            } else {
                                                clip.type = ClipType::Audio;
                                                clip.sequenceId = nestedSeqId;
                                            }
                                        } else {
                                            clip.type = (trackType == TrackType::Video) ? ClipType::Video
                                                                                        : ClipType::Audio;
                                        }

                                        // Convert 100ns ticks to microseconds
                                        clip.timelineStart = static_cast<TimeUs>(startTicks / 10);
                                        clip.timelineDuration = static_cast<TimeUs>(durTicks / 10);
                                        clip.srcIn = static_cast<TimeUs>(srcInTicks / 10);
                                        clip.srcOut = static_cast<TimeUs>((srcInTicks + durTicks) / 10);
                                        clip.fadeInUs = static_cast<TimeUs>(fadeInTicks / 10);
                                        clip.fadeOutUs = static_cast<TimeUs>(fadeOutTicks / 10);

                                        track.clips.append(clip);
                                        if (eventId > 0) {
                                            eventIdToClipLoc.insert(eventId, qMakePair(project.tracks().size(), track.clips.size() - 1));
                                        }
                                    }
                                }
                                epos += (esize + 7) & ~7;
                            }
                        } else if (evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                                   && std::memcmp(bytes + evpos + 24, kListAudioTrack, 16) == 0) {
                            qsizetype apos = evpos + 40;
                            const qsizetype aend = evpos + static_cast<qsizetype>(evsize);
                            while (apos + 24 <= aend) {
                                const uint8_t *aptr = bytes + apos;
                                const quint64 asize = readU64LE(aptr + 16);
                                if (asize < 24 || apos + static_cast<qsizetype>(asize) > aend)
                                    break;

                                if (std::memcmp(aptr, kChunkAudioTrackProps, 16) == 0) {
                                    const uint8_t *apayload = aptr + 24;
                                    const qsizetype apayloadSize = static_cast<qsizetype>(asize - 24);
                                    if (apayloadSize >= 16) {
                                        const float vol = readFloatLE(apayload + 12);
                                        if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                            track.volume = static_cast<double>(vol);
                                    }
                                    if (apayloadSize >= 20) {
                                        float pan = readFloatLE(apayload + 16);
                                        if (pan == 0.0f && apayloadSize >= 24)
                                            pan = readFloatLE(apayload + 20);
                                        if (!std::isnan(pan) && !std::isinf(pan))
                                            track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                                    }
                                }
                                apos += (asize + 7) & ~7;
                            }
                        } else if (std::memcmp(bytes + evpos, kChunkAudioTrackProps, 16) == 0) {
                            const uint8_t *apayload = bytes + evpos + 24;
                            const qsizetype apayloadSize = static_cast<qsizetype>(evsize - 24);
                            if (apayloadSize >= 16) {
                                const float vol = readFloatLE(apayload + 12);
                                if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                    track.volume = static_cast<double>(vol);
                            }
                            if (apayloadSize >= 20) {
                                float pan = readFloatLE(apayload + 16);
                                if (pan == 0.0f && apayloadSize >= 24)
                                    pan = readFloatLE(apayload + 20);
                                if (!std::isnan(pan) && !std::isinf(pan))
                                    track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                            }
                        }
                        evpos += (evsize + 7) & ~7;
                    }

                    project.tracks().append(track);
                }
                tpos += (tsize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    if (project.tracks().isEmpty()) {
        if (error)
            *error = QStringLiteral("No timeline tracks found in VEGAS project");
        return std::nullopt;
    }

    // Link clips via native VEGAS event groups (kTypeEventGroupList)
    for (const QList<quint32> &group : eventGroups) {
        if (group.size() < 2)
            continue;

        int matchedCount = 0;
        for (quint32 evId : group) {
            if (eventIdToClipLoc.contains(evId))
                ++matchedCount;
        }
        if (matchedCount < 2)
            continue;

        QString commonLinkId;
        bool hasVideo = false;
        bool hasAudio = false;

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                const Clip &c = project.tracks()[loc.first].clips[loc.second];
                if (!c.linkId.isEmpty()) {
                    commonLinkId = c.linkId;
                    break;
                }
            }
        }

        if (commonLinkId.isEmpty()) {
            commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                Clip &c = project.tracks()[loc.first].clips[loc.second];
                c.linkId = commonLinkId;
                if (project.tracks()[loc.first].type == TrackType::Video)
                    hasVideo = true;
                else if (project.tracks()[loc.first].type == TrackType::Audio)
                    hasAudio = true;
            }
        }

        if (hasVideo && hasAudio) {
            for (quint32 evId : group) {
                if (!eventIdToClipLoc.contains(evId))
                    continue;
                const auto loc = eventIdToClipLoc.value(evId);
                if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                    if (project.tracks()[loc.first].type == TrackType::Video) {
                        project.tracks()[loc.first].clips[loc.second].suppressEmbeddedAudio = true;
                    }
                }
            }
        }
    }

    // Fallback: Symmetrically link companion video and audio clips sharing the same asset and timeline span
    for (Track &vTrack : project.tracks()) {
        if (vTrack.type != TrackType::Video)
            continue;
        for (Clip &vClip : vTrack.clips) {
            if (!vClip.linkId.isEmpty() || vClip.assetId.isEmpty())
                continue;
            for (Track &aTrack : project.tracks()) {
                if (aTrack.type != TrackType::Audio)
                    continue;
                for (Clip &aClip : aTrack.clips) {
                    if (!aClip.linkId.isEmpty() || aClip.assetId != vClip.assetId)
                        continue;
                    if (qAbs(aClip.timelineStart - vClip.timelineStart) <= 1000 &&
                        qAbs(aClip.timelineDuration - vClip.timelineDuration) <= 1000) {
                        const QString commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        vClip.linkId = commonLinkId;
                        aClip.linkId = commonLinkId;
                        if (vClip.type == ClipType::Composite)
                            vClip.suppressEmbeddedAudio = true;
                        break;
                    }
                }
                if (!vClip.linkId.isEmpty())
                    break;
            }
        }
    }

    // 4. Parse Timeline Markers and Regions (kTypeMarkerList and kGuidMarker)
    if (depth == 0) {
        qsizetype markerListStart = -1;
        qsizetype markerListEnd = -1;

        qsizetype lpos = 40;
        while (lpos + 24 <= totalSize) {
            const uint8_t *lptr = bytes + lpos;
            const quint64 chunkSize = readU64LE(lptr + 16);
            if (chunkSize < 24 || lpos + static_cast<qsizetype>(chunkSize) > totalSize)
                break;

            if (std::memcmp(lptr, kGuidW64List, 16) == 0 && lpos + 40 <= totalSize
                && std::memcmp(lptr + 24, kTypeMarkerList, 16) == 0) {
                markerListStart = lpos + 40;
                markerListEnd = lpos + static_cast<qsizetype>(chunkSize);
                break;
            }
            lpos += (chunkSize + 7) & ~7;
        }

        qsizetype scanPos = 40;
        while (scanPos + 16 <= totalSize) {
            const void *found = std::memchr(bytes + scanPos, kGuidMarker[0], totalSize - scanPos);
            if (!found)
                break;
            const qsizetype idx = static_cast<const uint8_t *>(found) - bytes;
            if (idx + 16 > totalSize)
                break;

            if (std::memcmp(bytes + idx, kGuidMarker, 16) == 0) {
                const bool inMarkerList = (markerListStart >= 0 && idx >= markerListStart && idx < markerListEnd);
                auto optMarker = parseMarkerPayload(bytes + idx + 16, totalSize - (idx + 16));
                if (optMarker.has_value()) {
                    const ParsedMarker &m = *optMarker;
                    const bool isTempo = m.name.startsWith(QLatin1String("Tempo: "));
                    if (!isTempo && (inMarkerList || !m.name.isEmpty())) {
                        if (m.isRegion) {
                            const QString baseLabel = m.name.isEmpty()
                                ? QStringLiteral("Region %1").arg(m.id)
                                : m.name;
                            Bookmark bmIn;
                            bmIn.timeUs = m.posUs;
                            bmIn.label = baseLabel;
                            project.bookmarks().append(bmIn);

                            if (m.durUs > 0) {
                                Bookmark bmOut;
                                bmOut.timeUs = m.posUs + m.durUs;
                                bmOut.label = QStringLiteral("%1 (Out)").arg(baseLabel);
                                project.bookmarks().append(bmOut);
                            }
                        } else {
                            Bookmark bm;
                            bm.timeUs = m.posUs;
                            bm.label = m.name.isEmpty()
                                ? QStringLiteral("Marker %1").arg(m.id)
                                : m.name;
                            project.bookmarks().append(bm);
                        }
                    }
                }
            }
            scanPos = idx + 1;
        }

        if (!project.bookmarks().isEmpty()) {
            QList<Bookmark> uniqueBookmarks;
            for (const Bookmark &bm : project.bookmarks()) {
                bool exists = false;
                for (const Bookmark &u : uniqueBookmarks) {
                    if (u.timeUs == bm.timeUs && u.label == bm.label) {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    uniqueBookmarks.append(bm);
            }
            std::sort(uniqueBookmarks.begin(), uniqueBookmarks.end(),
                      [](const Bookmark &a, const Bookmark &b) {
                          if (a.timeUs != b.timeUs)
                              return a.timeUs < b.timeUs;
                          return a.label < b.label;
                      });
            project.bookmarks() = std::move(uniqueBookmarks);
        }

        for (const QString &seqId : project.sequenceIds()) {
            if (!project.openSequenceTabs().contains(seqId))
                project.openSequenceTabs().append(seqId);
        }
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::vegas
