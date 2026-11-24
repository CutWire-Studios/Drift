#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/mcp/McpController.h"
#include "core/text/TitleTemplate.h"

QVariantList AppController::titleTemplates() const
{
    QVariantList out;
    for (const drift::TitleTemplate &tpl : drift::TitleTemplateCatalog::instance().templates()) {
        QVariantList fields;
        for (const drift::TitleTemplateField &f : tpl.fields()) {
            fields.append(QVariantMap{{QStringLiteral("key"), f.key},
                                      {QStringLiteral("label"), f.label},
                                      {QStringLiteral("text"), f.text}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), tpl.id},
                               {QStringLiteral("label"), tpl.label},
                               {QStringLiteral("category"), tpl.category},
                               {QStringLiteral("duration"), tpl.durationSec},
                               {QStringLiteral("fields"), fields},
                               {QStringLiteral("elements"), tpl.elements.toVariantList()}});
    }
    return out;
}

QVariantMap AppController::insertTitleTemplate(const QString &templateId, double atSeconds, const QVariantMap &texts)
{
    const std::optional<drift::TitleTemplate> tpl = drift::TitleTemplateCatalog::instance().templateForId(templateId);
    if (!tpl) {
        return QVariantMap{{QStringLiteral("error"), QStringLiteral("not_found")},
                           {QStringLiteral("message"), tr("Unknown title template")}};
    }

    QMap<QString, QString> overrides;
    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());

    const drift::Project before = m_project;
    const drift::TimeUs start = atSeconds < 0.0 ? m_playheadUs : drift::secondsToUs(atSeconds);
    QList<drift::Clip> clips = drift::instantiateTitleTemplate(*tpl, m_project.width(), m_project.height(), start, overrides);

    m_project.ensureTrackIds();
    const QList<QPair<QString, int>> selection = captureSelectionByTrackId();
    QList<drift::Track> &tracks = m_project.tracks();

    // Everything goes above the first video track (and its effect lanes), back to front, each
    // element on a free track of its own kind above the one before it.
    int ceiling = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks.at(i).type != drift::TrackType::Video)
            continue;
        const QList<int> lanes = drift::adjustmentLaneIndexes(m_project, i);
        ceiling = lanes.isEmpty() ? i : lanes.first();
        break;
    }

    QStringList ids;
    QString firstText;
    for (const drift::Clip &clip : std::as_const(clips)) {
        const drift::TrackType type = drift::trackTypeForClipType(clip.type);
        int target = -1;
        for (int i = ceiling - 1; i >= 0; --i) {
            const drift::Track &t = tracks.at(i);
            if (t.isAdjustmentLane() || t.locked || t.type != type || !t.allowsClipType(clip.type))
                continue;
            const bool collides = std::any_of(t.clips.cbegin(), t.clips.cend(), [&](const drift::Clip &c) {
                return clip.timelineStart < c.timelineEnd() && c.timelineStart < clip.timelineEnd();
            });
            if (!collides) {
                target = i;
                break;
            }
        }
        if (target < 0) {
            tracks.insert(ceiling, drift::Track{.type = type});
            target = ceiling;
        }
        ceiling = target;
        tracks[target].clips.append(clip);
        ids.append(clip.id);
        if (firstText.isEmpty() && clip.type == drift::ClipType::Text)
            firstText = clip.id;
    }
    normalizeAdjustmentLanes(m_project);
    restoreSelectionByTrackId(selection);

    const QString undoText = tr("Insert %1").arg(tpl->label);
    pushProjectEdit(before, undoText);
    finishEdit(undoText);

    const QPair<int, int> at = m_mcpController->locateClip(firstText.isEmpty() ? ids.first() : firstText);
    if (at.first >= 0)
        selectClip(at.first, at.second);

    return QVariantMap{{QStringLiteral("clips"), ids},
                       {QStringLiteral("start"), drift::usToSeconds(start)},
                       {QStringLiteral("duration"), tpl->durationSec},
                       {QStringLiteral("text_clip"), firstText}};
}

void AppController::addTitleTemplate(const QString &templateId)
{
    const QVariantMap result = insertTitleTemplate(templateId, -1.0, {});
    if (result.contains(QStringLiteral("error"))) {
        setLastMessage(result.value(QStringLiteral("message")).toString(), QStringLiteral("warning"));
        return;
    }
    const QString textClip = result.value(QStringLiteral("text_clip")).toString();
    const QPair<int, int> at = m_mcpController->locateClip(textClip);
    if (at.first < 0)
        return;
    const drift::TimeUs start = drift::secondsToUs(result.value(QStringLiteral("start")).toDouble());
    if (m_playheadUs < start || m_playheadUs >= start + drift::secondsToUs(result.value(QStringLiteral("duration")).toDouble()))
        setPlayheadSeconds(drift::usToSeconds(start));
    emit inlineTextEditRequested(at.first, at.second);
}
