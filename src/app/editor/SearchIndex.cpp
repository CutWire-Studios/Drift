#include "app/editor/SearchIndex.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

struct ActionFlag
{
    const char *id;
    const char *property;
};

const ActionFlag kActionFlags[] = {
    {"undo", "undoAvailable"},
    {"redo", "redoAvailable"},
    {"merge", "mergeAvailable"},
    {"separateAudio", "separateAudioAvailable"},
    {"transformTogether", "transformTogetherAvailable"},
    {"unlink", "unlinkAvailable"},
    {"link", "linkAvailable"},
};

const char *const kClipActions[] = {
    "delete", "copy", "cut", "duplicate", "copyEffects", "pasteEffects",
    "deleteLeft", "deleteRight", "nudgeLeft", "nudgeRight", "speedUp", "speedDown",
};

struct AddonPlace
{
    const char *kind;
    const char *target;
};

const AddonPlace kAddonPlaces[] = {
    {"effects", "panel:assets.effects"},
    {"transitions", "panel:assets.transitions"},
    {"audio-effects", "panel:assets.sounds"},
    {"effect-templates", "panel:assets.templates"},
    {"looks", "panel:looks"},
    {"stickers", "panel:assets.stickers"},
    {"fonts", "panel:properties.text#text-font"},
    {"whisper-model", "panel:assets.subtitles"},
    {"tts-model", "panel:assets.speech"},
    {"sam2-model", "panel:assets.masks"},
    {"object-model", "panel:assets.scenes"},
    {"denoise-model", "panel:properties.audio#audio-remove-noise"},
    {"face-model", "panel:properties.effects#effects-face-tracking"},
    {"face-props", "panel:properties.effects#effects-face-tracking"},
    {"depth-model", "panel:assets.effects"},
    {"restore-model", "panel:assets.media"},
};

QVariant prop(QObject *obj, const char *name)
{
    return obj ? obj->property(name) : QVariant();
}

int intProp(QObject *obj, const char *name)
{
    return obj ? obj->property(name).toInt() : -1;
}

QVariantList listOf(QObject *obj, const char *name)
{
    return prop(obj, name).toList();
}

QVariantList listCall(QObject *obj, const char *method)
{
    QVariantList out;
    if (obj)
        QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(QVariantList, out));
    return out;
}

bool boolCall(QObject *obj, const char *method)
{
    bool out = false;
    if (obj)
        QMetaObject::invokeMethod(obj, method, Q_RETURN_ARG(bool, out));
    return out;
}

QString firstNonEmpty(const QVariantMap &map, const char *first, const char *second)
{
    const QString value = map.value(QLatin1String(first)).toString();
    return value.isEmpty() ? map.value(QLatin1String(second)).toString() : value;
}

QString translated(const QJsonObject &object, const char *key)
{
    const QByteArray context = object.value(QLatin1String("context")).toString().toUtf8();
    const QByteArray source = object.value(QLatin1String(key)).toString().toUtf8();
    return QCoreApplication::translate(context.constData(), source.constData());
}

} // namespace

SearchIndex::SearchIndex(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SearchIndex::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_visible.size();
}

QVariant SearchIndex::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size())
        return {};
    const Row &row = m_entries.at(m_visible.at(index.row())).row;
    switch (role) {
    case KindRole:
        return row.kind;
    case LabelRole:
        return row.label;
    case SubRole:
        return row.sub;
    case HintRole:
        return row.hint;
    case ShortcutRole:
        return row.shortcut;
    case EnabledRole:
        return row.enabled;
    case TargetRole:
        return row.target;
    }
    return {};
}

QHash<int, QByteArray> SearchIndex::roleNames() const
{
    return {
        {KindRole, "kind"},
        {LabelRole, "label"},
        {SubRole, "sub"},
        {HintRole, "hint"},
        {ShortcutRole, "shortcut"},
        {EnabledRole, "enabled"},
        {TargetRole, "target"},
    };
}

void SearchIndex::setSources(QObject *editor, QObject *workspaces, QObject *addons, QObject *themes)
{
    m_editor = editor;
    m_workspaces = workspaces;
    m_addons = addons;
    m_themes = themes;
}

void SearchIndex::loadStaticJson(const QByteArray &json)
{
    m_static = QJsonDocument::fromJson(json).object();
    m_staticLoaded = true;
}

// Exact > prefix > word start > substring > in-order letters. -1 is no match.
int SearchIndex::score(const QString &s, const QString &q)
{
    if (s.isEmpty())
        return -1;
    if (s == q)
        return 1000;
    const qsizetype at = s.indexOf(q);
    if (at == 0)
        return 800 - int(s.size());
    if (at > 0) {
        const QChar before = s.at(at - 1);
        const bool wordStart = before.isSpace() || before == QLatin1Char('-') || before == QLatin1Char('_')
                               || before == QLatin1Char('(') || before == QChar(u'›')
                               || before == QLatin1Char('/');
        return (wordStart ? 600 : 400) - int(at);
    }
    qsizetype pos = 0;
    qsizetype gaps = 0;
    for (qsizetype i = 0; i < q.size(); ++i) {
        const qsizetype found = s.indexOf(q.at(i), pos);
        if (found < 0)
            return -1;
        gaps += found - pos;
        pos = found + 1;
    }
    return std::max(1, 200 - int(gaps) * 5);
}

void SearchIndex::rebuild()
{
    if (!m_staticLoaded) {
        QFile file(QStringLiteral(":/search-index.json"));
        loadStaticJson(file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
    }
    m_visible.clear();
    m_entries.clear();
    addActions();
    addCommands();
    addWorkspaces();
    addPanels();
    addEditorCatalogs();
    addAddons();
    addStatic();
    setQuery(m_query);
}

void SearchIndex::setQuery(const QString &query)
{
    m_query = query;
    const QString q = query.trimmed().toLower();
    QVector<int> visible;
    if (q.isEmpty()) {
        for (int i = 0; i < m_entries.size(); ++i) {
            const Row &row = m_entries.at(i).row;
            if (row.kind == QLatin1String("command") && row.target.startsWith(QLatin1String("workspace:")))
                visible.append(i);
        }
    } else {
        const QStringList words = q.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        struct Hit
        {
            int index;
            double score;
        };
        QVector<Hit> hits;
        for (int i = 0; i < m_entries.size(); ++i) {
            const Entry &entry = m_entries.at(i);
            double best = std::max({double(score(entry.lowerLabel, q)),
                                    score(entry.lowerKeywords, q) * 0.5,
                                    score(entry.lowerSub, q) * 0.4});
            // "blur gaussian" or "transform opacity": every word somewhere in the row.
            if (best < 0 && words.size() > 1) {
                const bool all = std::all_of(words.cbegin(), words.cend(),
                                             [&](const QString &w) { return entry.lowerAll.contains(w); });
                if (all)
                    best = 300;
            }
            if (best < 0)
                continue;
            if (entry.row.enabled && !entry.row.target.isEmpty())
                best += 50;
            hits.append({i, best});
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
        if (hits.size() > 50)
            hits.resize(50);
        for (const Hit &hit : std::as_const(hits))
            visible.append(hit.index);
    }
    beginResetModel();
    m_visible = visible;
    endResetModel();
    emit countChanged();
}

bool SearchIndex::activate(int row)
{
    if (row < 0 || row >= m_visible.size())
        return false;
    // Copied: a navigate handler may rebuild and invalidate the reference.
    const Row picked = m_entries.at(m_visible.at(row)).row;
    if (!picked.enabled)
        return false;
    if (picked.target.startsWith(QLatin1String("apply:")))
        runApply(picked.target.mid(6));
    else
        emit navigateRequested(picked.target);
    return true;
}

// Selection is read here, not when the row was built, so a stale row applies to what is selected now.
void SearchIndex::runApply(const QString &payload)
{
    const int colon = payload.indexOf(QLatin1Char(':'));
    const QString kind = payload.left(colon);
    const QString id = payload.mid(colon + 1);
    const int track = intProp(m_editor, "selectedTrack");
    const int clip = intProp(m_editor, "selectedClip");

    if (kind == QLatin1String("effect")) {
        if (clip < 0)
            QMetaObject::invokeMethod(m_editor, "addAdjustmentClipWithEffect", Q_ARG(QString, id), Q_ARG(int, -1),
                                      Q_ARG(double, -1.0));
        else
            QMetaObject::invokeMethod(m_editor, "addEffect", Q_ARG(int, track), Q_ARG(int, clip), Q_ARG(QString, id));
    } else if (kind == QLatin1String("template")) {
        QMetaObject::invokeMethod(m_editor, "applyEffectTemplate", Q_ARG(int, track), Q_ARG(int, clip),
                                  Q_ARG(QString, id));
    } else if (kind == QLatin1String("stack")) {
        QMetaObject::invokeMethod(m_editor, "applyEffectPreset", Q_ARG(int, track), Q_ARG(int, clip),
                                  Q_ARG(QString, id));
    } else if (kind == QLatin1String("audioEffect")) {
        QMetaObject::invokeMethod(m_editor, "addAudioEffect", Q_ARG(int, track), Q_ARG(int, clip), Q_ARG(QString, id));
    } else if (kind == QLatin1String("transition")) {
        const double duration = prop(prop(m_editor, "preferences").value<QObject *>(), "transitionDurationSeconds").toDouble();
        QMetaObject::invokeMethod(m_editor, "addTransition", Q_ARG(int, track), Q_ARG(int, clip), Q_ARG(QString, id),
                                  Q_ARG(double, duration));
    } else if (kind == QLatin1String("mask")) {
        QMetaObject::invokeMethod(m_editor, "addMaskToClip", Q_ARG(int, track), Q_ARG(int, clip), Q_ARG(QString, id));
    } else if (kind == QLatin1String("shape")) {
        QMetaObject::invokeMethod(m_editor, "addShapeClip", Q_ARG(QString, id), Q_ARG(double, -1.0));
    } else if (kind == QLatin1String("sticker")) {
        QMetaObject::invokeMethod(m_editor, "addStickerClip", Q_ARG(QString, id), Q_ARG(double, -1.0));
    } else if (kind == QLatin1String("textStyle")) {
        QMetaObject::invokeMethod(m_editor, "addTextClip", Q_ARG(QString, QString()), Q_ARG(double, -1.0),
                                  Q_ARG(QString, id));
    }
}

void SearchIndex::addRow(Row row, const QString &keywords)
{
    Entry entry;
    entry.lowerLabel = row.label.toLower();
    entry.lowerKeywords = keywords.toLower();
    entry.lowerSub = row.sub.toLower();
    entry.lowerAll = (row.label + QLatin1Char(' ') + keywords + QLatin1Char(' ') + row.sub).toLower();
    entry.row = std::move(row);
    m_entries.append(std::move(entry));
}

void SearchIndex::addActions()
{
    if (!m_editor)
        return;
    for (const QVariant &v : listOf(m_editor, "actions")) {
        const QVariantMap action = v.toMap();
        const QString id = action.value(QLatin1String("id")).toString();
        if (id == QLatin1String("search"))
            continue;
        const bool enabled = actionEnabled(id);
        addRow({QStringLiteral("action"), action.value(QLatin1String("label")).toString(),
                enabled ? QString() : tr("Not available now"), {},
                action.value(QLatin1String("shortcut")).toString(),
                QStringLiteral("action:") + id, enabled},
               id);
    }
}

bool SearchIndex::actionEnabled(const QString &id) const
{
    for (const ActionFlag &flag : kActionFlags) {
        if (id == QLatin1String(flag.id))
            return prop(m_editor, flag.property).toBool();
    }
    if (id == QLatin1String("pasteAttributes"))
        return boolCall(m_editor, "canPasteAttributes");
    const bool needsClip = std::any_of(std::begin(kClipActions), std::end(kClipActions),
                                       [&](const char *action) { return id == QLatin1String(action); });
    if (needsClip)
        return intProp(m_editor, "selectedClip") >= 0;
    return true;
}

void SearchIndex::addCommands()
{
    const auto command = [this](const QString &label, const QString &keywords, const QString &target) {
        addRow({QStringLiteral("command"), label, {}, {}, {}, target, true}, keywords);
    };
    command(tr("Export video"), QStringLiteral("render save mp4"), QStringLiteral("dialog:export"));
    command(tr("Downloads"), QStringLiteral("download link"), QStringLiteral("dialog:downloads"));
    command(tr("Language…"), QStringLiteral("translate locale"), QStringLiteral("dialog:language"));
    command(tr("Video size and layout"), QStringLiteral("canvas resolution aspect ratio"),
            QStringLiteral("dialog:videoSize"));
    command(tr("Switch to mobile layout…"), QStringLiteral("phone shell"), QStringLiteral("dialog:mobileShell"));
    command(tr("Settings"), QStringLiteral("preferences options"), QStringLiteral("dialog:settings"));
    command(tr("Extras"), QStringLiteral("add-ons addons packs"), QStringLiteral("dialog:extras"));
    command(tr("Add-on manager"), QStringLiteral("addons install packs extras"),
            QStringLiteral("dialog:addonManager"));
    command(tr("Debug info…"), QStringLiteral("diagnostics"), QStringLiteral("dialog:debugInfo"));
    command(tr("Light theme"), QStringLiteral("appearance theme"), QStringLiteral("theme:light"));
    command(tr("Dark theme"), QStringLiteral("appearance theme"), QStringLiteral("theme:dark"));
    command(tr("System theme"), QStringLiteral("appearance theme"), QStringLiteral("theme:system"));
    for (const QVariant &v : listOf(m_themes, "themes")) {
        const QVariantMap theme = v.toMap();
        if (theme.value(QLatin1String("builtIn")).toBool())
            continue;
        const QString name = theme.value(QLatin1String("name")).toString();
        command(tr("Theme: %1").arg(name), QStringLiteral("appearance theme ") + name,
                QStringLiteral("theme:") + theme.value(QLatin1String("id")).toString());
    }
}

void SearchIndex::addWorkspaces()
{
    const QString active = prop(m_workspaces, "activeId").toString();
    for (const QVariant &v : listOf(m_workspaces, "workspaces")) {
        const QVariantMap ws = v.toMap();
        const QString id = ws.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("command"), tr("Workspace: %1").arg(ws.value(QLatin1String("name")).toString()),
                id == active ? tr("Current") : QString(), {}, {}, QStringLiteral("workspace:") + id, true},
               QStringLiteral("workspace layout ") + id);
    }
}

void SearchIndex::addPanels()
{
    m_panelLabels.clear();
    for (const QVariant &v : listOf(m_workspaces, "panelTypes")) {
        const QVariantMap panel = v.toMap();
        const QString type = panel.value(QLatin1String("type")).toString();
        const QString label = panel.value(QLatin1String("label")).toString();
        const QString fullLabel = panel.value(QLatin1String("fullLabel")).toString();
        m_panelLabels.insert(type, fullLabel);
        addRow({QStringLiteral("panel"), label, fullLabel == label ? QString() : fullLabel, {}, {},
                QStringLiteral("panel:") + type, true},
               type);
    }
}

void SearchIndex::addEditorCatalogs()
{
    if (!m_editor)
        return;
    const int track = intProp(m_editor, "selectedTrack");
    const int clip = intProp(m_editor, "selectedClip");
    const bool hasClip = clip >= 0;
    const QString clipKind = hasClip ? prop(m_editor, "selectedClipData").toMap().value(QLatin1String("kind")).toString()
                                     : QString();
    const QString applyHint = tr("Apply to selected clip");
    const QString selectClip = tr("Select a clip first");
    const QString addHint = tr("Add at playhead");

    for (const QVariant &v : listCall(m_editor, "effectCatalog")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("effect"), firstNonEmpty(e, "displayName", "label"),
                e.value(QLatin1String("categoryLabel")).toString(),
                hasClip ? applyHint : tr("Add as adjustment clip"), {},
                QStringLiteral("apply:effect:") + id, true},
               id);
    }

    for (const QVariant &v : listCall(m_editor, "effectTemplateCatalog")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("template"), firstNonEmpty(e, "displayName", "label"),
                e.value(QLatin1String("categoryLabel")).toString(), hasClip ? applyHint : selectClip, {},
                QStringLiteral("apply:template:") + id, hasClip},
               id);
    }

    for (const QVariant &v : listCall(m_editor, "userEffectPresets")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("stack"), e.value(QLatin1String("label")).toString(),
                e.value(QLatin1String("labels")).toStringList().join(QStringLiteral(", ")),
                hasClip ? applyHint : selectClip, {}, QStringLiteral("apply:stack:") + id, hasClip},
               QStringLiteral("preset"));
    }

    for (const QVariant &v : listCall(m_editor, "audioEffectCatalog")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("audioEffect"), firstNonEmpty(e, "displayName", "label"),
                e.value(QLatin1String("categoryLabel")).toString(), hasClip ? applyHint : selectClip, {},
                QStringLiteral("apply:audioEffect:") + id, hasClip},
               id);
    }

    // A transition goes on the cut after the selected clip.
    bool canTransition = false;
    QSet<QString> fitting;
    if (hasClip) {
        QMetaObject::invokeMethod(m_editor, "hasTransitionPartner", Q_RETURN_ARG(bool, canTransition),
                                  Q_ARG(int, track), Q_ARG(int, clip));
    }
    if (canTransition) {
        QVariantList kinds;
        QMetaObject::invokeMethod(m_editor, "transitionKindsForTrack", Q_RETURN_ARG(QVariantList, kinds),
                                  Q_ARG(int, track));
        for (const QVariant &k : std::as_const(kinds))
            fitting.insert(k.toMap().value(QLatin1String("kind")).toString());
    }
    for (const QVariant &v : listCall(m_editor, "transitionKinds")) {
        const QVariantMap e = v.toMap();
        const QString kind = e.value(QLatin1String("kind")).toString();
        const bool enabled = canTransition && fitting.contains(kind);
        const QString hint = enabled ? tr("Add after selected clip")
                                     : (canTransition ? tr("Not available on this track")
                                                      : tr("Select the clip before a cut"));
        addRow({QStringLiteral("transition"), e.value(QLatin1String("label")).toString(), {}, hint, {},
                QStringLiteral("apply:transition:") + kind, enabled},
               kind);
    }

    // Same rule as MasksTab: audio has no picture, text and subtitles are styled, not masked.
    const bool maskable = hasClip && clipKind != QLatin1String("audio") && clipKind != QLatin1String("text")
                          && clipKind != QLatin1String("subtitle");
    for (const QVariant &v : listCall(m_editor, "maskCatalog")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("mask"), e.value(QLatin1String("label")).toString(), {},
                maskable ? applyHint : tr("Select a video, image, text or shape clip"), {},
                QStringLiteral("apply:mask:") + id, maskable},
               id);
    }

    for (const QVariant &v : listCall(m_editor, "builtinShapes")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("shape"), e.value(QLatin1String("label")).toString(), {}, addHint, {},
                QStringLiteral("apply:shape:") + id, true},
               id);
    }

    for (const QVariant &v : listCall(m_editor, "builtinStickers")) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("sticker"), e.value(QLatin1String("label")).toString(), {}, addHint, {},
                QStringLiteral("apply:sticker:") + id, true},
               id);
    }

    QVariantList textStyles = listCall(m_editor, "textPresets");
    textStyles += listCall(m_editor, "userTextPresets");
    for (const QVariant &v : std::as_const(textStyles)) {
        const QVariantMap e = v.toMap();
        const QString id = e.value(QLatin1String("id")).toString();
        addRow({QStringLiteral("textStyle"), e.value(QLatin1String("label")).toString(), {}, addHint, {},
                QStringLiteral("apply:textStyle:") + id, true},
               QStringLiteral("text title"));
    }
}

void SearchIndex::addAddons()
{
    for (const QVariant &v : listOf(m_addons, "catalog")) {
        const QVariantMap e = v.toMap();
        const QString state = e.value(QLatin1String("state")).toString();
        if (state != QLatin1String("installed") && state != QLatin1String("update-available"))
            continue;
        const QString kind = e.value(QLatin1String("kind")).toString();
        QString target = QStringLiteral("dialog:addonManager?kind=") + kind;
        QString sub = tr("Extras");
        for (const AddonPlace &place : kAddonPlaces) {
            if (kind == QLatin1String(place.kind)) {
                target = QLatin1String(place.target);
                sub = panelLabel(target.section(QLatin1Char('#'), 0, 0).mid(6));
                break;
            }
        }
        addRow({QStringLiteral("addon"), e.value(QLatin1String("name")).toString(), sub, {}, {}, target, true},
               kind + QLatin1Char(' ') + e.value(QLatin1String("description")).toString());
    }
}

QString SearchIndex::panelLabel(const QString &type) const
{
    return m_panelLabels.value(type, type);
}

void SearchIndex::addStatic()
{
    const int clip = intProp(m_editor, "selectedClip");
    const QStringList caps = prop(m_editor, "selectedClipData").toMap().value(QLatin1String("caps")).toStringList();

    QHash<QString, QString> pageTitles;
    for (const QJsonValue &v : m_static.value(QLatin1String("settingsPages")).toArray()) {
        const QJsonObject page = v.toObject();
        pageTitles.insert(page.value(QLatin1String("id")).toString(), translated(page, "source"));
    }
    QHash<QString, QString> tabLabels;
    for (const QJsonValue &v : m_static.value(QLatin1String("propertiesTabs")).toArray()) {
        const QJsonObject tab = v.toObject();
        tabLabels.insert(tab.value(QLatin1String("id")).toString(), translated(tab, "source"));
    }

    for (const QJsonValue &v : m_static.value(QLatin1String("entries")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString target = e.value(QLatin1String("target")).toString();
        const QString ref = target.section(QLatin1Char('#'), 0, 0);
        const QString label = translated(e, "source");
        QString keywords = e.value(QLatin1String("keywords")).toString();
        if (e.contains(QLatin1String("descriptionSource")))
            keywords += QLatin1Char(' ') + translated(e, "descriptionSource");

        QStringList path;
        if (ref.startsWith(QLatin1String("settings:"))) {
            const QString page = ref.mid(QStringLiteral("settings:").size());
            path << tr("Settings") << pageTitles.value(page, page);
        } else if (ref.startsWith(QLatin1String("panel:"))) {
            path << panelLabel(ref.mid(QStringLiteral("panel:").size()));
        }
        path << label;

        bool enabled = true;
        QString hint;
        const QString prefix = QStringLiteral("panel:properties.");
        if (ref.startsWith(prefix)) {
            bool ok = clip >= 0;
            for (const QJsonValue &need : e.value(QLatin1String("needs")).toArray()) {
                if (!caps.contains(need.toString()))
                    ok = false;
            }
            enabled = ok;
            if (!ok) {
                const QString tab = tabLabels.value(ref.mid(prefix.size()), label);
                hint = clip >= 0 ? QCoreApplication::translate("PropertiesPanel", "%1 doesn't apply to this clip").arg(tab)
                                 : QCoreApplication::translate("PropertiesPanel", "Select a clip to edit its %1").arg(tab);
            }
        }
        addRow({QStringLiteral("control"), label, path.join(QStringLiteral(" › ")), hint, {}, target, enabled},
               keywords);
    }

    for (const QJsonValue &v : m_static.value(QLatin1String("settingsPages")).toArray()) {
        const QJsonObject page = v.toObject();
        addRow({QStringLiteral("control"), translated(page, "source"), tr("Settings"), {}, {},
                QStringLiteral("settings:") + page.value(QLatin1String("id")).toString(), true},
               page.value(QLatin1String("keywords")).toString());
    }
}
