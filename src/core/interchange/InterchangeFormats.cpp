#include "core/interchange/InterchangeFormats.h"

#include "core/interchange/EdlReader.h"
#include "core/interchange/InterchangeReport.h"
#include "core/interchange/KdenliveReader.h"
#include "core/interchange/OtioReader.h"
#include "core/interchange/ResolveReader.h"
#include "core/interchange/VegasReader.h"
#include "core/project/Track.h"
#include "core/project/clip/Clip.h"
#include "core/interchange/premiere/MogrtReader.h"
#include "core/interchange/premiere/PrprojReader.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace drift::interchange {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Interchange", text);
}

// The registry holds the English source strings so the UI can translate them when the language
// changes; QT_TRANSLATE_NOOP marks them for lupdate.
QString untranslated(const char *text)
{
    return QString::fromUtf8(text);
}

InterchangeFormat makeFormat(const QString &id, const QString &name, const QStringList &exts, bool import,
                             const QStringList &gaps)
{
    InterchangeFormat f;
    f.id = id;
    f.displayName = name;
    f.extensions = exts;
    f.canImport = import;
    f.canExport = !import;
    f.stability = InterchangeFormat::Stability::Experimental;
    f.knownGaps = gaps;
    return f;
}

QList<InterchangeFormat> buildFormats()
{
    QList<InterchangeFormat> rows;

    rows.append(makeFormat(QStringLiteral("veg"), untranslated(QT_TRANSLATE_NOOP("Interchange", "Sony/MAGIX VEGAS Pro")), {QStringLiteral("veg")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Effects other than Levels, Color Corrector and Sharpen are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Envelopes and keyframes are not read.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Nested projects that cannot be opened are left out."))}));

    rows.append(makeFormat(QStringLiteral("prproj"), untranslated(QT_TRANSLATE_NOOP("Interchange", "Adobe Premiere Pro")),
                           {QStringLiteral("prproj"), QStringLiteral("xml")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Transitions are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Effects and keyframes are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Audio levels and opacity are not read.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Only the first sequence is imported."))}));

    rows.append(makeFormat(QStringLiteral("resolve"), untranslated(QT_TRANSLATE_NOOP("Interchange", "DaVinci Resolve / FCPXML")),
                           {QStringLiteral("drp"), QStringLiteral("fcpxml")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "The timeline is flattened to one video track and one audio track.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Effects, transitions, keyframes, retiming and audio levels are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Markers and compound or multicam clips are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "A .drp project is always opened at 1080p 24 fps.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "When the chosen timeline is not found, the default timeline is used."))}));

    rows.append(makeFormat(QStringLiteral("kdenlive"), untranslated(QT_TRANSLATE_NOOP("Interchange", "Kdenlive / Shotcut (MLT)")),
                           {QStringLiteral("kdenlive"), QStringLiteral("mlt")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Guides and markers are not read.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Only the last sequence is imported; earlier ones are ignored.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Filters and transitions are dropped, including their keyframes.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Colour, noise and count generators are left as gaps.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Volume and pan are not read."))}));

    rows.append(makeFormat(QStringLiteral("mogrt"), untranslated(QT_TRANSLATE_NOOP("Interchange", "Motion Graphics Template")), {QStringLiteral("mogrt")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Imported as a single clip inserted into the current project.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Only the properties the template exposes are carried over."))}));

    rows.append(makeFormat(QStringLiteral("edl"), untranslated(QT_TRANSLATE_NOOP("Interchange", "CMX 3600 EDL")), {QStringLiteral("edl")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Transitions are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Speed changes (M2) and retiming are dropped."))}));

    rows.append(makeFormat(QStringLiteral("otio"), untranslated(QT_TRANSLATE_NOOP("Interchange", "OpenTimelineIO")), {QStringLiteral("otio")}, true,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Marker colours are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Effects other than a constant speed change are dropped.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Nested stacks and generator references are left as gaps.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Only the first stack is read.")),
                            untranslated(QT_TRANSLATE_NOOP("Interchange", "Resolution and frame rate are guessed when the file does not say."))}));

    const QStringList exportGaps = {untranslated(QT_TRANSLATE_NOOP("Interchange", "Effects, transitions and keyframes are not written.")),
                                    untranslated(QT_TRANSLATE_NOOP("Interchange", "Speed changes and text clips are not written.")),
                                    untranslated(QT_TRANSLATE_NOOP("Interchange", "Nested sequences and other sequences are not written."))};

    QStringList fcpxmlGaps = exportGaps;
    rows.append(makeFormat(QStringLiteral("fcpxml"), untranslated(QT_TRANSLATE_NOOP("Interchange", "Final Cut Pro XML")), {QStringLiteral("fcpxml")}, false,
                           fcpxmlGaps));

    QStringList edlGaps = exportGaps;
    edlGaps.append(untranslated(QT_TRANSLATE_NOOP("Interchange", "Disabled clips are dropped.")));
    rows.append(makeFormat(QStringLiteral("edl"), untranslated(QT_TRANSLATE_NOOP("Interchange", "CMX 3600 EDL")), {QStringLiteral("edl")}, false, edlGaps));

    rows.append(makeFormat(QStringLiteral("otio"), untranslated(QT_TRANSLATE_NOOP("Interchange", "OpenTimelineIO")), {QStringLiteral("otio")}, false,
                           exportGaps));

    rows.append(makeFormat(QStringLiteral("tsv"), untranslated(QT_TRANSLATE_NOOP("Interchange", "VEGAS markers (TSV)")), {QStringLiteral("txt")}, false,
                           {untranslated(QT_TRANSLATE_NOOP("Interchange", "Only markers and regions are written; clips are not exported."))}));

    return rows;
}

} // namespace

const QList<InterchangeFormat> &interchangeFormats()
{
    static const QList<InterchangeFormat> rows = buildFormats();
    return rows;
}

QString detectImportFormat(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (!ext.isEmpty()) {
        for (const InterchangeFormat &f : interchangeFormats())
            if (f.canImport && f.extensions.contains(ext))
                return f.id;
    }

    if (vegas::isVegasProject(path))
        return QStringLiteral("veg");
    if (mogrt::isMogrtFile(path))
        return QStringLiteral("mogrt");
    if (prproj::isPremiereProject(path))
        return QStringLiteral("prproj");
    if (resolve::isResolveProject(path))
        return QStringLiteral("resolve");
    if (kdenlive::isKdenliveProject(path))
        return QStringLiteral("kdenlive");
    if (edl::isEdlTimeline(path))
        return QStringLiteral("edl");
    if (otio::isOtioTimeline(path))
        return QStringLiteral("otio");
    return {};
}

std::optional<Project> readImport(const QString &formatId, const QString &path, InterchangeReport *report,
                                  QString *error)
{
    std::optional<Project> project;
    if (formatId == QLatin1String("veg"))
        project = vegas::readProject(path, error, report);
    else if (formatId == QLatin1String("prproj"))
        project = prproj::readProject(path, error, report);
    else if (formatId == QLatin1String("resolve"))
        project = resolve::readProject(path, error, report);
    else if (formatId == QLatin1String("kdenlive"))
        project = kdenlive::readProject(path, error, report);
    else if (formatId == QLatin1String("edl"))
        project = edl::readProject(path, error, report);
    else if (formatId == QLatin1String("otio"))
        project = otio::readProject(path, error, report);
    else if (error)
        *error = formatId == QLatin1String("mogrt") ? tr("Motion graphics templates are not opened as projects.")
                                                    : tr("Unknown import format.");

    if (project && report) {
        report->formatId = formatId;
        fillSummary(*project, *report);
    }
    return project;
}

InterchangeReport analyzeExport(const Project &project, const QString &formatId)
{
    using Severity = InterchangeIssue::Severity;
    InterchangeReport report;
    report.formatId = formatId;
    const auto drop = [&](const char *category, const char *message, const QString &name = {}, TimeUs at = -1) {
        report.add(Severity::Dropped, QString::fromLatin1(category), tr(message), name, at);
    };

    if (formatId == QLatin1String("tsv")) {
        for (const Track &track : project.tracks())
            for (int i = 0; i < track.clips.size(); ++i)
                drop("clips", QT_TRANSLATE_NOOP("Interchange", "Clips and tracks are not written; only markers are exported."));
        fillSummary(project, report);
        return report;
    }

    const bool edl = formatId == QLatin1String("edl");
    for (const Track &track : project.tracks()) {
        const QString trackName = track.name;
        if (track.isAdjustment()) {
            drop("effect", QT_TRANSLATE_NOOP("Interchange", "Adjustment tracks are not written."), trackName);
            continue;
        }
        if (track.hidden) {
            drop("track", QT_TRANSLATE_NOOP("Interchange", "Hidden tracks are not written."), trackName);
            continue;
        }

        for (const Clip &clip : track.clips) {
            if (edl && !clip.enabled) {
                drop("disabled", QT_TRANSLATE_NOOP("Interchange", "Disabled clips are not written."), clip.name, clip.timelineStart);
                continue;
            }
            if (clip.type == ClipType::Text) {
                drop("text", QT_TRANSLATE_NOOP("Interchange", "Text clips are not written with their text."), clip.name, clip.timelineStart);
                continue;
            }
            if (clip.type == ClipType::Composite || !clip.sequenceId.isEmpty())
                drop("nested", QT_TRANSLATE_NOOP("Interchange", "Nested sequence clips are not written."), clip.name, clip.timelineStart);
            if (!clip.effects.isEmpty() || !clip.audioEffects.isEmpty())
                drop("effect", QT_TRANSLATE_NOOP("Interchange", "Effects are not written."), clip.name, clip.timelineStart);
            if (clip.hasSpeedCurve() || clip.reverse || !qFuzzyCompare(clip.effectiveSpeed(), 1.0))
                drop("speed", QT_TRANSLATE_NOOP("Interchange", "Speed changes are not written; the clip plays at normal speed."), clip.name,
                     clip.timelineStart);
            if (!clip.opacity.isEmpty() || !clip.positionX.isEmpty() || !clip.positionY.isEmpty()
                || !clip.scaleX.isEmpty() || !clip.scaleY.isEmpty() || !clip.rotation.isEmpty()
                || !clip.rotationX.isEmpty() || !clip.rotationY.isEmpty() || !clip.positionZ.isEmpty()
                || !clip.perspective.isEmpty() || !clip.anchorX.isEmpty() || !clip.anchorY.isEmpty()
                || !clip.anchorZ.isEmpty() || !clip.volume.isEmpty())
                drop("keyframes", QT_TRANSLATE_NOOP("Interchange", "Keyframes are not written."), clip.name, clip.timelineStart);
        }

        for (const Transition &transition : track.transitions) {
            QString name;
            TimeUs at = -1;
            for (const Clip &clip : track.clips) {
                if (clip.id == transition.toClipId) {
                    name = clip.name;
                    at = clip.timelineStart;
                }
            }
            drop("transition", QT_TRANSLATE_NOOP("Interchange", "Transitions are not written."), name, at);
        }
    }

    const QString active = project.activeSequenceId();
    int otherSequences = active.isEmpty() ? 0 : 1;
    for (const QString &id : project.sequenceIds())
        if (id != active)
            ++otherSequences;
    for (int i = 0; i < otherSequences; ++i)
        drop("sequence", QT_TRANSLATE_NOOP("Interchange", "Only the open timeline is written; other sequences are left out."));

    fillSummary(project, report);
    return report;
}

} // namespace drift::interchange
