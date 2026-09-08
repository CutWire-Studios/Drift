#include "EdlReader.h"

#include "Clip.h"
#include "MediaAsset.h"
#include "Project.h"
#include "Track.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QtMath>

namespace drift::edl {

namespace {

TimeUs parseTimecode(const QString &tc, double fps)
{
    const QString clean = tc.trimmed();
    QStringList parts = clean.split(QLatin1Char(':'));
    if (parts.size() < 4)
        parts = clean.split(QLatin1Char(';'));
    if (parts.size() < 4)
        return 0;

    const qint64 h = parts[0].toLongLong();
    const qint64 m = parts[1].toLongLong();
    const qint64 s = parts[2].toLongLong();
    const qint64 f = parts[3].toLongLong();

    const double effectiveFps = fps > 0.0 ? fps : 24.0;
    const double totalFrames = (h * 3600.0 + m * 60.0 + s) * effectiveFps + static_cast<double>(f);
    return static_cast<TimeUs>(llround((totalFrames * 1000000.0) / effectiveFps));
}

QString normalizePath(const QString &rawPath)
{
    if (rawPath.isEmpty())
        return {};

    QString p = rawPath.trimmed();
    if (p.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        QUrl url(p);
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            p = local;
        else
            p = p.mid(7);
    }

    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p;
}

QString resolveMediaPath(const QString &path, const QString &sourceDir)
{
    const QString norm = normalizePath(path);
    if (norm.isEmpty())
        return {};

    if (QFileInfo::exists(norm))
        return norm;

    if (!sourceDir.isEmpty()) {
        const QString candidate = QDir(sourceDir).filePath(QFileInfo(norm).fileName());
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return norm;
}

struct EdlEvent
{
    QString eventNum;
    QString reel;
    QString channel; // V, A, A1, A2, AA, B, etc.
    QString transition;
    TimeUs srcIn = 0;
    TimeUs srcOut = 0;
    TimeUs recIn = 0;
    TimeUs recOut = 0;
    QString clipName;
    QString filePath;
};

struct EdlLocator
{
    TimeUs time = 0;
    QString color;
    QString text;
};

} // namespace

bool isEdlTimeline(const QString &filePath)
{
    if (filePath.endsWith(QLatin1String(".edl"), Qt::CaseInsensitive))
        return true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray header = file.read(2048);
    return isEdlData(header);
}

bool isEdlData(const QByteArray &data)
{
    if (data.isEmpty())
        return false;

    const QString text = QString::fromUtf8(data.left(2048));
    if (text.contains(QLatin1String("TITLE:"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("FCM:"), Qt::CaseInsensitive)) {
        return true;
    }

    // Check for CMX 3600 event pattern: line starting with digits followed by spaces and reel
    static const QRegularExpression eventPattern(QStringLiteral("(?m)^\\s*\\d{3,}\\s+\\S+\\s+[VA]"));
    return eventPattern.match(text).hasMatch();
}

std::optional<Project> readProject(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(filePath);
        return std::nullopt;
    }

    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).absolutePath();
    return readProjectData(data, sourceDir, error);
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error)
{
    if (data.isEmpty()) {
        if (error)
            *error = QObject::tr("File is empty");
        return std::nullopt;
    }

    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));

    QString projectName = QStringLiteral("EDL Sequence");
    double fps = 24.0;
    bool isDropFrame = false;

    QList<EdlEvent> events;
    QList<EdlLocator> locators;

    static const QRegularExpression tcRegex(QStringLiteral("^\\d{2}:\\d{2}:\\d{2}[:;]\\d{2}$"));

    for (int lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        QString line = lines.at(lineIdx).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1String("TITLE:"), Qt::CaseInsensitive)) {
            projectName = line.mid(6).trimmed();
            continue;
        }

        if (line.startsWith(QLatin1String("FCM:"), Qt::CaseInsensitive)) {
            const QString fcm = line.mid(4).trimmed().toUpper();
            if (fcm.contains(QLatin1String("DROP FRAME")) && !fcm.contains(QLatin1String("NON-DROP"))) {
                isDropFrame = true;
                fps = 29.97;
            } else if (fcm.contains(QLatin1String("NON-DROP"))) {
                isDropFrame = false;
                if (fps == 29.97)
                    fps = 30.0;
            }
            continue;
        }

        // Comment lines
        if (line.startsWith(QLatin1Char('*'))) {
            const QString comment = line.mid(1).trimmed();

            if (comment.startsWith(QLatin1String("FPS:"), Qt::CaseInsensitive)) {
                const double parsedFps = comment.mid(4).trimmed().toDouble();
                if (parsedFps > 0.0)
                    fps = parsedFps;
            } else if (comment.startsWith(QLatin1String("TIMEBASE:"), Qt::CaseInsensitive)) {
                const double parsedFps = comment.mid(9).trimmed().toDouble();
                if (parsedFps > 0.0)
                    fps = parsedFps;
            } else if (comment.startsWith(QLatin1String("FROM CLIP NAME:"), Qt::CaseInsensitive)) {
                if (!events.isEmpty())
                    events.last().clipName = comment.mid(15).trimmed();
            } else if (comment.startsWith(QLatin1String("TO CLIP NAME:"), Qt::CaseInsensitive)) {
                if (!events.isEmpty() && events.last().clipName.isEmpty())
                    events.last().clipName = comment.mid(13).trimmed();
            } else if (comment.startsWith(QLatin1String("SOURCE FILE:"), Qt::CaseInsensitive)) {
                if (!events.isEmpty())
                    events.last().filePath = comment.mid(12).trimmed();
            } else if (comment.startsWith(QLatin1String("MEDIA FILE:"), Qt::CaseInsensitive)) {
                if (!events.isEmpty())
                    events.last().filePath = comment.mid(11).trimmed();
            } else if (comment.startsWith(QLatin1String("LOC:"), Qt::CaseInsensitive)) {
                const QString locStr = comment.mid(4).trimmed();
                const QStringList locParts = locStr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (!locParts.isEmpty()) {
                    EdlLocator loc;
                    loc.time = parseTimecode(locParts[0], fps);
                    if (locParts.size() > 1)
                        loc.color = locParts[1];
                    if (locParts.size() > 2)
                        loc.text = locParts.mid(2).join(QLatin1Char(' '));
                    locators.append(loc);
                }
            }
            continue;
        }

        // Parse Event Row
        // e.g. "001  AX       V     C        00:00:00:00 00:00:05:00 01:00:00:00 01:00:05:00"
        const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 6)
            continue;

        // Find timecode tokens
        QStringList tcTokens;
        for (const QString &tok : tokens) {
            if (tcRegex.match(tok).hasMatch())
                tcTokens.append(tok);
        }

        if (tcTokens.size() >= 4) {
            EdlEvent ev;
            ev.eventNum = tokens[0];
            ev.reel = tokens.size() > 1 ? tokens[1] : QString();
            ev.channel = tokens.size() > 2 ? tokens[2].toUpper() : QStringLiteral("V");
            ev.transition = tokens.size() > 3 ? tokens[3].toUpper() : QStringLiteral("C");

            ev.srcIn = parseTimecode(tcTokens[0], fps);
            ev.srcOut = parseTimecode(tcTokens[1], fps);
            ev.recIn = parseTimecode(tcTokens[2], fps);
            ev.recOut = parseTimecode(tcTokens[3], fps);

            events.append(ev);
        }
    }

    if (events.isEmpty()) {
        if (error)
            *error = QObject::tr("No edit events found in EDL");
        return std::nullopt;
    }

    Project project;
    project.setName(projectName);
    project.setResolution(1920, 1080);
    project.setFps(qMax(1, qRound(fps)));

    // Normalize timeline offset if record start time is offset (e.g. 01:00:00:00)
    TimeUs minRecIn = events.first().recIn;
    for (const auto &ev : events) {
        if (ev.recIn < minRecIn)
            minRecIn = ev.recIn;
    }

    const TimeUs offsetUs = (minRecIn >= 600000000LL) ? minRecIn : 0LL; // 10 minutes or more -> rebase to 0

    // Collect Video and Audio events
    QList<EdlEvent> videoEvents;
    QList<EdlEvent> audioEvents;

    for (auto ev : events) {
        if (offsetUs > 0) {
            ev.recIn = qMax(0LL, ev.recIn - offsetUs);
            ev.recOut = qMax(0LL, ev.recOut - offsetUs);
        }

        if (ev.channel.startsWith(QLatin1Char('V')) || ev.channel == QLatin1String("B") || ev.channel == QLatin1String("VA")) {
            videoEvents.append(ev);
        }
        if (ev.channel.startsWith(QLatin1Char('A')) || ev.channel == QLatin1String("B") || ev.channel == QLatin1String("VA")) {
            audioEvents.append(ev);
        }
    }

    QMap<QString, QString> mediaUrlToAssetId;

    const auto buildClipsForEvents = [&](const QList<EdlEvent> &evList, TrackType tType) -> QList<Clip> {
        QList<Clip> clips;
        for (const auto &ev : evList) {
            TimeUs durUs = ev.recOut - ev.recIn;
            if (durUs <= 0)
                durUs = ev.srcOut - ev.srcIn;
            if (durUs <= 0)
                durUs = 5000000LL;

            Clip clip;
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.name = !ev.clipName.isEmpty() ? ev.clipName : (!ev.reel.isEmpty() ? ev.reel : QStringLiteral("Event %1").arg(ev.eventNum));
            clip.timelineStart = ev.recIn;
            clip.timelineDuration = durUs;
            clip.srcIn = ev.srcIn;
            clip.srcOut = ev.srcIn + durUs;
            clip.type = (tType == TrackType::Audio) ? ClipType::Audio : ClipType::Video;

            QString mediaPath = ev.filePath;
            if (mediaPath.isEmpty() && !ev.clipName.isEmpty())
                mediaPath = ev.clipName;

            if (!mediaPath.isEmpty()) {
                const QString resolved = resolveMediaPath(mediaPath, sourceDir);
                QString assetId;
                if (mediaUrlToAssetId.contains(resolved)) {
                    assetId = mediaUrlToAssetId.value(resolved);
                } else {
                    assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    MediaAsset asset;
                    asset.id = assetId;
                    asset.name = QFileInfo(resolved).fileName();
                    if (asset.name.isEmpty())
                        asset.name = clip.name;
                    asset.path = resolved;
                    asset.kind = (tType == TrackType::Audio) ? MediaKind::Audio : MediaKind::Video;
                    project.addAsset(asset);
                    mediaUrlToAssetId.insert(resolved, assetId);
                }
                clip.assetId = assetId;
            }

            clips.append(clip);
        }
        return clips;
    };

    if (!videoEvents.isEmpty() || !audioEvents.isEmpty()) {
        project.tracks().clear();
    }

    if (!videoEvents.isEmpty()) {
        Track vTrack;
        vTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        vTrack.type = TrackType::Video;
        vTrack.name = QStringLiteral("V1");
        vTrack.clips = buildClipsForEvents(videoEvents, TrackType::Video);
        project.tracks().append(vTrack);
    }

    if (!audioEvents.isEmpty()) {
        Track aTrack;
        aTrack.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        aTrack.type = TrackType::Audio;
        aTrack.name = QStringLiteral("A1");
        aTrack.clips = buildClipsForEvents(audioEvents, TrackType::Audio);
        project.tracks().append(aTrack);
    }

    if (project.tracks().isEmpty()) {
        project.resetToDefaultTimeline();
    }

    project.ensureTrackIds();

    // Add locators/bookmarks
    for (const auto &loc : locators) {
        Bookmark bm;
        bm.timeUs = (offsetUs > 0 && loc.time >= offsetUs) ? (loc.time - offsetUs) : loc.time;
        bm.label = loc.text.isEmpty() ? QStringLiteral("Locator") : loc.text;
        project.bookmarks().append(bm);
    }

    return project;
}

} // namespace drift::edl
