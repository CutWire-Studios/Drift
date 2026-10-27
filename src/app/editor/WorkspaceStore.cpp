#include "app/editor/WorkspaceStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto kListKey = "ui/workspaces/list";
constexpr auto kActiveKey = "ui/workspaces/active";
constexpr auto kDefaultId = "edit";
constexpr auto kPresetDir = ":/workspaces";

struct PanelType
{
    const char *type;
    const char *label;
};

enum class Group { Panel, Assets, Properties };

const PanelType kPanelTypes[] = {
    {"assets", QT_TRANSLATE_NOOP("WorkspaceStore", "Assets")},
    {"properties", QT_TRANSLATE_NOOP("WorkspaceStore", "Properties")},
    {"preview", QT_TRANSLATE_NOOP("WorkspaceStore", "Preview")},
    {"timeline", QT_TRANSLATE_NOOP("WorkspaceStore", "Timeline")},
    {"audioMixer", QT_TRANSLATE_NOOP("WorkspaceStore", "Audio Mixer")},
    {"sourceViewer", QT_TRANSLATE_NOOP("WorkspaceStore", "Source Viewer")},
    {"sourceLab.bin", QT_TRANSLATE_NOOP("WorkspaceStore", "Sources")},
    {"sourceLab.viewer", QT_TRANSLATE_NOOP("WorkspaceStore", "Source Lab Viewer")},
    {"sourceLab.trim", QT_TRANSLATE_NOOP("WorkspaceStore", "Source Lab Trim")},
    {"sourceLab.ranges", QT_TRANSLATE_NOOP("WorkspaceStore", "Ranges & Markers")},
    {"sourceLab.process", QT_TRANSLATE_NOOP("WorkspaceStore", "Process")},
    {"sourceLab.output", QT_TRANSLATE_NOOP("WorkspaceStore", "Output")},
    {"subtitles", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitle cues")},
    {"subtitleLane", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitle lane")},
    {"subtitleText", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitle text")},
    {"subtitleTiming", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitle timing")},
    {"undoHistory", QT_TRANSLATE_NOOP("WorkspaceStore", "History")},
    {"agentLog", QT_TRANSLATE_NOOP("WorkspaceStore", "Agent Log")},
    {"recorder", QT_TRANSLATE_NOOP("WorkspaceStore", "Recorder")},
    {"denoise", QT_TRANSLATE_NOOP("WorkspaceStore", "Noise Removal")},
    {"color", QT_TRANSLATE_NOOP("WorkspaceStore", "Color")},
    {"looks", QT_TRANSLATE_NOOP("WorkspaceStore", "Looks")},
    {"scopes", QT_TRANSLATE_NOOP("WorkspaceStore", "Scopes")},
    {"keyframes", QT_TRANSLATE_NOOP("WorkspaceStore", "Keyframes")},
    {"masking", QT_TRANSLATE_NOOP("WorkspaceStore", "Masking")},
    {"tracking", QT_TRANSLATE_NOOP("WorkspaceStore", "Tracking")},
};

// Tab ids and labels mirror AssetsPanel.qml / PropertiesPanel.qml.
const PanelType kAssetTabTypes[] = {
    {"assets.media", QT_TRANSLATE_NOOP("WorkspaceStore", "Media")},
    {"assets.market", QT_TRANSLATE_NOOP("WorkspaceStore", "Market")},
    {"assets.text", QT_TRANSLATE_NOOP("WorkspaceStore", "Text")},
    {"assets.subtitles", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitles")},
    {"assets.speech", QT_TRANSLATE_NOOP("WorkspaceStore", "Speech")},
    {"assets.stickers", QT_TRANSLATE_NOOP("WorkspaceStore", "Stickers")},
    {"assets.shapes", QT_TRANSLATE_NOOP("WorkspaceStore", "Shapes")},
    {"assets.masks", QT_TRANSLATE_NOOP("WorkspaceStore", "Masks")},
    {"assets.scenes", QT_TRANSLATE_NOOP("WorkspaceStore", "Scenes")},
    {"assets.effects", QT_TRANSLATE_NOOP("WorkspaceStore", "Effects")},
    {"assets.templates", QT_TRANSLATE_NOOP("WorkspaceStore", "Templates")},
    {"assets.transitions", QT_TRANSLATE_NOOP("WorkspaceStore", "Transitions")},
    {"assets.sounds", QT_TRANSLATE_NOOP("WorkspaceStore", "Audio FX")},
};

const PanelType kPropertiesTabTypes[] = {
    {"properties.general", QT_TRANSLATE_NOOP("WorkspaceStore", "General")},
    {"properties.text", QT_TRANSLATE_NOOP("WorkspaceStore", "Text")},
    {"properties.shape", QT_TRANSLATE_NOOP("WorkspaceStore", "Shape")},
    {"properties.vector", QT_TRANSLATE_NOOP("WorkspaceStore", "Motion")},
    {"properties.model3d", QT_TRANSLATE_NOOP("WorkspaceStore", "3D Model")},
    {"properties.pdf", QT_TRANSLATE_NOOP("WorkspaceStore", "PDF")},
    {"properties.subtitles", QT_TRANSLATE_NOOP("WorkspaceStore", "Subtitles")},
    {"properties.transform", QT_TRANSLATE_NOOP("WorkspaceStore", "Transform")},
    {"properties.camera", QT_TRANSLATE_NOOP("WorkspaceStore", "Camera")},
    {"properties.stabilize", QT_TRANSLATE_NOOP("WorkspaceStore", "Stabilization")},
    {"properties.animation", QT_TRANSLATE_NOOP("WorkspaceStore", "Animation")},
    {"properties.audio", QT_TRANSLATE_NOOP("WorkspaceStore", "Audio")},
    {"properties.speed", QT_TRANSLATE_NOOP("WorkspaceStore", "Speed")},
    {"properties.blending", QT_TRANSLATE_NOOP("WorkspaceStore", "Blending")},
    {"properties.masks", QT_TRANSLATE_NOOP("WorkspaceStore", "Masks")},
    {"properties.effects", QT_TRANSLATE_NOOP("WorkspaceStore", "Effects")},
    {"properties.audioEffects", QT_TRANSLATE_NOOP("WorkspaceStore", "Audio FX")},
    {"properties.transition", QT_TRANSLATE_NOOP("WorkspaceStore", "Transition")},
};

bool isKnownType(const QString &type)
{
    const auto known = [&](const PanelType &p) { return type == QLatin1String(p.type); };
    return std::any_of(std::begin(kPanelTypes), std::end(kPanelTypes), known)
           || std::any_of(std::begin(kAssetTabTypes), std::end(kAssetTabTypes), known)
           || std::any_of(std::begin(kPropertiesTabTypes), std::end(kPropertiesTabTypes), known);
}

bool isSplit(const QVariantMap &node)
{
    return node.contains(QStringLiteral("split"));
}

QVariantList equalSizes(int count)
{
    QVariantList sizes;
    for (int i = 0; i < count; ++i)
        sizes << 1.0 / count;
    return sizes;
}

QVariantList normalizedSizes(const QVariantList &sizes, int count)
{
    if (sizes.size() != count)
        return equalSizes(count);
    double sum = 0;
    for (const QVariant &v : sizes) {
        const double s = v.toDouble();
        if (!std::isfinite(s) || s <= 0)
            return equalSizes(count);
        sum += s;
    }
    QVariantList out;
    for (const QVariant &v : sizes)
        out << v.toDouble() / sum;
    return out;
}

// An empty map means the node holds nothing and should be dropped by its parent.
QVariantMap normalizedNode(const QVariantMap &node)
{
    if (isSplit(node)) {
        QVariantList children;
        const QVariantList raw = node.value(QStringLiteral("children")).toList();
        QVariantList rawSizes = node.value(QStringLiteral("sizes")).toList();
        QVariantList keptSizes;
        for (int i = 0; i < raw.size(); ++i) {
            const QVariantMap child = normalizedNode(raw.at(i).toMap());
            if (child.isEmpty())
                continue;
            children << child;
            // A dropped child takes its size with it; if the sizes did not line up to
            // begin with they are replaced below anyway.
            if (rawSizes.size() == raw.size())
                keptSizes << rawSizes.at(i);
        }
        if (children.isEmpty())
            return {};
        if (children.size() == 1)
            return children.first().toMap();
        QVariantMap out = node;
        out[QStringLiteral("children")] = children;
        out[QStringLiteral("sizes")] = normalizedSizes(keptSizes, children.size());
        const QString dir = node.value(QStringLiteral("split")).toString();
        out[QStringLiteral("split")] = dir == QLatin1String("v") ? dir : QStringLiteral("h");
        return out;
    }

    QVariantList panels;
    for (const QVariant &p : node.value(QStringLiteral("panels")).toList()) {
        if (!p.toMap().value(QStringLiteral("type")).toString().isEmpty())
            panels << p;
    }
    if (panels.isEmpty())
        return {};
    QVariantMap out = node;
    out[QStringLiteral("panels")] = panels;
    out[QStringLiteral("active")] = std::clamp(node.value(QStringLiteral("active")).toInt(), 0,
                                               int(panels.size()) - 1);
    return out;
}

QVariantMap normalizedWorkspace(const QVariantMap &ws)
{
    QVariantMap out = ws;
    QVariantList windows;
    for (const QVariant &w : ws.value(QStringLiteral("windows")).toList()) {
        QVariantMap window = w.toMap();
        window[QStringLiteral("root")] = normalizedNode(window.value(QStringLiteral("root")).toMap());
        windows << window;
    }
    out[QStringLiteral("windows")] = windows;
    return out;
}

bool findWindow(const QVariantMap &ws, const QString &windowId, QVariantList &windows, int &index)
{
    windows = ws.value(QStringLiteral("windows")).toList();
    for (index = 0; index < windows.size(); ++index) {
        if (windows.at(index).toMap().value(QStringLiteral("id")).toString() == windowId)
            return true;
    }
    return false;
}

bool setNodeSizes(QVariantMap &node, const QVariantList &path, int depth, const QVariantList &sizes)
{
    if (!isSplit(node))
        return false;
    QVariantList children = node.value(QStringLiteral("children")).toList();
    if (depth == path.size()) {
        if (sizes.size() != children.size())
            return false;
        node[QStringLiteral("sizes")] = normalizedSizes(sizes, children.size());
        return true;
    }
    const int index = path.at(depth).toInt();
    if (index < 0 || index >= children.size())
        return false;
    QVariantMap child = children.at(index).toMap();
    if (!setNodeSizes(child, path, depth + 1, sizes))
        return false;
    children[index] = child;
    node[QStringLiteral("children")] = children;
    return true;
}

QString validateNode(const QVariantMap &node, QSet<QString> &seen)
{
    if (node.isEmpty())
        return WorkspaceStore::tr("A window has no panels.");

    if (isSplit(node)) {
        const QVariantList children = node.value(QStringLiteral("children")).toList();
        if (children.size() < 2)
            return WorkspaceStore::tr("A split needs at least two sections.");
        for (const QVariant &child : children) {
            const QString err = validateNode(child.toMap(), seen);
            if (!err.isEmpty())
                return err;
        }
        return {};
    }

    const QVariantList panels = node.value(QStringLiteral("panels")).toList();
    if (panels.isEmpty())
        return WorkspaceStore::tr("A section needs at least one panel.");
    const int active = node.value(QStringLiteral("active")).toInt();
    if (active < 0 || active >= panels.size())
        return WorkspaceStore::tr("A section's active tab is out of range.");
    for (const QVariant &p : panels) {
        const QString type = p.toMap().value(QStringLiteral("type")).toString();
        if (!isKnownType(type))
            return WorkspaceStore::tr("Unknown panel type \"%1\".").arg(type);
        if (seen.contains(type))
            return WorkspaceStore::tr("The \"%1\" panel is used more than once.").arg(type);
        seen.insert(type);
    }
    return {};
}

QVariantMap parseJson(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object().toVariantMap();
}

} // namespace

WorkspaceStore::WorkspaceStore(QObject *parent)
    : QObject(parent)
{
    loadPresets();
    if (!QSettings().contains(QLatin1String(kListKey)))
        migrateOldFractions();
    loadUser();
    migrateCutTrim();
    migrateSourceLabBin();
    m_activeId = QSettings().value(QLatin1String(kActiveKey), QLatin1String(kDefaultId)).toString();
    fixActive();
}

void WorkspaceStore::loadPresets()
{
    QVector<QVariantMap> loaded;
    const QStringList files = QDir(QLatin1String(kPresetDir)).entryList({QStringLiteral("*.json")});
    for (const QString &name : files) {
        QFile file(QLatin1String(kPresetDir) + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QVariantMap ws = normalizedWorkspace(parseJson(file.readAll()));
        if (ws.value(QStringLiteral("id")).toString().isEmpty() || !validate(ws).isEmpty())
            continue;
        ws[QStringLiteral("builtin")] = true;
        loaded.append(ws);
    }
    std::sort(loaded.begin(), loaded.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const int oa = a.value(QStringLiteral("order")).toInt();
        const int ob = b.value(QStringLiteral("order")).toInt();
        return oa != ob ? oa < ob : a.value(QStringLiteral("id")).toString()
                                        < b.value(QStringLiteral("id")).toString();
    });
    for (const QVariantMap &ws : loaded) {
        const QString id = ws.value(QStringLiteral("id")).toString();
        m_presets.insert(id, ws);
        m_presetOrder << id;
    }
}

void WorkspaceStore::loadUser()
{
    m_user.clear();
    const QByteArray raw = QSettings().value(QLatin1String(kListKey)).toString().toUtf8();
    const QJsonArray array = QJsonDocument::fromJson(raw).array();
    QSet<QString> ids;
    for (const QJsonValue &v : array) {
        QVariantMap ws = normalizedWorkspace(v.toObject().toVariantMap());
        const QString id = ws.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || ids.contains(id) || !validate(ws).isEmpty())
            continue;
        ids.insert(id);
        ws[QStringLiteral("builtin")] = m_presets.contains(id);
        m_user << ws;
    }
}

void WorkspaceStore::persist() const
{
    QJsonArray array;
    for (const QVariant &v : m_user)
        array.append(QJsonObject::fromVariantMap(v.toMap()));
    QSettings().setValue(QLatin1String(kListKey),
                         QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

// cut-trim was renamed to source-lab. Its user overrides would otherwise linger as orphan
// workspaces, and a saved active id would point at nothing.
void WorkspaceStore::migrateCutTrim()
{
    QSettings settings;
    const QString flag = QStringLiteral("ui/workspaces/migratedSourceLab");
    if (settings.value(flag).toBool())
        return;
    settings.setValue(flag, true);

    const QString oldId = QStringLiteral("cut-trim");
    const auto isOld = [&](const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString() == oldId; };
    const auto end = std::remove_if(m_user.begin(), m_user.end(), isOld);
    if (end != m_user.end()) {
        m_user.erase(end, m_user.end());
        persist();
    }
    if (settings.value(QLatin1String(kActiveKey)).toString() == oldId)
        settings.setValue(QLatin1String(kActiveKey), QStringLiteral("source-lab"));
}

// Source Lab used the generic media bin until it got its own sourceLab.bin panel; a saved
// override of the preset would keep the old panel.
void WorkspaceStore::migrateSourceLabBin()
{
    QSettings settings;
    const QString flag = QStringLiteral("ui/workspaces/migratedSourceLabBin");
    if (settings.value(flag).toBool())
        return;
    settings.setValue(flag, true);

    const QString id = QStringLiteral("source-lab");
    const auto isLab = [&](const QVariant &v) { return v.toMap().value(QStringLiteral("id")).toString() == id; };
    const auto end = std::remove_if(m_user.begin(), m_user.end(), isLab);
    if (end != m_user.end()) {
        m_user.erase(end, m_user.end());
        persist();
    }
}

void WorkspaceStore::migrateOldFractions()
{
    QSettings settings;
    const auto read = [&](const QString &key) {
        bool ok = false;
        const double v = settings.value(QStringLiteral("ui/layout/") + key).toDouble(&ok);
        return ok && v > 0 && v < 1 ? v : 0.0;
    };

    QVariantList overrides;
    const auto migrate = [&](const QString &layout, const QString &id, bool portrait) {
        const double assets = read(layout + QStringLiteral("/assets"));
        const double properties = read(layout + QStringLiteral("/properties"));
        const double editorRow = read(layout + QStringLiteral("/editorRow"));
        const double preview = portrait ? read(layout + QStringLiteral("/preview")) : 0.0;
        if (assets == 0 && properties == 0 && editorRow == 0 && preview == 0)
            return;
        if (!m_presets.contains(id))
            return;

        QVariantMap ws = m_presets.value(id);
        QVariantMap root = ws.value(QStringLiteral("windows")).toList().first().toMap()
                               .value(QStringLiteral("root")).toMap();
        // Both layouts have the vertical split holding the editor row and timeline:
        // at the root in landscape, one level down in the portrait editing stack.
        const QVariantList stackPath = portrait ? QVariantList{0} : QVariantList{};
        const QVariantList rowPath = portrait ? QVariantList{0, 0} : QVariantList{0};

        if (editorRow > 0)
            setNodeSizes(root, stackPath, 0, {editorRow, 1 - editorRow});
        if (portrait) {
            if (preview > 0)
                setNodeSizes(root, {}, 0, {1 - preview, preview});
            if (assets > 0 && properties > 0)
                setNodeSizes(root, rowPath, 0, {assets, properties});
        } else {
            const double a = assets > 0 ? assets : 0.25;
            const double p = properties > 0 ? properties : 0.25;
            if (a + p < 0.9)
                setNodeSizes(root, rowPath, 0, {a, 1 - a - p, p});
        }

        QVariantMap window = ws.value(QStringLiteral("windows")).toList().first().toMap();
        window[QStringLiteral("root")] = root;
        ws[QStringLiteral("windows")] = QVariantList{window};
        ws[QStringLiteral("modified")] = true;
        overrides << ws;
    };
    migrate(QStringLiteral("landscape"), QStringLiteral("edit"), false);
    migrate(QStringLiteral("portrait"), QStringLiteral("edit-portrait"), true);

    QJsonArray array;
    for (const QVariant &v : overrides)
        array.append(QJsonObject::fromVariantMap(v.toMap()));
    settings.setValue(QLatin1String(kListKey),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkspaceStore::fixActive()
{
    if (!exists(m_activeId))
        m_activeId = QLatin1String(kDefaultId);
}

bool WorkspaceStore::exists(const QString &id) const
{
    return m_presets.contains(id) || userIndex(id) >= 0;
}

int WorkspaceStore::userIndex(const QString &id) const
{
    for (int i = 0; i < m_user.size(); ++i) {
        if (m_user.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

QVariantMap WorkspaceStore::effective(const QString &id) const
{
    const int i = userIndex(id);
    if (i >= 0)
        return m_user.at(i).toMap();
    return m_presets.value(id);
}

QString WorkspaceStore::newId() const
{
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("custom-%1").arg(n);
        if (!exists(id))
            return id;
    }
}

void WorkspaceStore::store(const QVariantMap &ws)
{
    const QString id = ws.value(QStringLiteral("id")).toString();
    const int i = userIndex(id);
    if (i >= 0)
        m_user[i] = ws;
    else
        m_user << ws;
    persist();
}

QVariantList WorkspaceStore::workspaces() const
{
    QVariantList out;
    for (const QString &id : m_presetOrder)
        out << effective(id);
    for (const QVariant &v : m_user) {
        if (!m_presets.contains(v.toMap().value(QStringLiteral("id")).toString()))
            out << v;
    }
    return out;
}

void WorkspaceStore::setActiveId(const QString &id)
{
    if (id == m_activeId || !exists(id))
        return;
    m_activeId = id;
    QSettings().setValue(QLatin1String(kActiveKey), id);
    emit activeIdChanged();
}

QVariantList WorkspaceStore::panelTypes() const
{
    QVariantList out;
    const auto add = [&](const PanelType &p, Group group) {
        const QString label = tr(p.label);
        QVariantMap entry{{QStringLiteral("type"), QLatin1String(p.type)},
                          {QStringLiteral("label"), label}};
        switch (group) {
        case Group::Panel:
            entry[QStringLiteral("group")] = QStringLiteral("panel");
            entry[QStringLiteral("fullLabel")] = label;
            break;
        case Group::Assets:
            entry[QStringLiteral("group")] = QStringLiteral("assets");
            entry[QStringLiteral("tab")] = QString::fromLatin1(p.type).mid(7);
            entry[QStringLiteral("fullLabel")] = tr("%1 · Library").arg(label);
            break;
        case Group::Properties:
            entry[QStringLiteral("group")] = QStringLiteral("properties");
            entry[QStringLiteral("tab")] = QString::fromLatin1(p.type).mid(11);
            entry[QStringLiteral("fullLabel")] = tr("%1 · Inspector").arg(label);
            break;
        }
        out << entry;
    };
    for (const PanelType &p : kPanelTypes)
        add(p, Group::Panel);
    for (const PanelType &p : kAssetTabTypes)
        add(p, Group::Assets);
    for (const PanelType &p : kPropertiesTabTypes)
        add(p, Group::Properties);
    return out;
}

QVariantMap WorkspaceStore::workspace(const QString &id) const
{
    return effective(id);
}

QString WorkspaceStore::validate(const QVariantMap &ws) const
{
    const QVariantList windows = ws.value(QStringLiteral("windows")).toList();
    if (windows.isEmpty())
        return tr("A workspace needs at least one window.");
    if (windows.first().toMap().value(QStringLiteral("id")).toString() != QLatin1String("main"))
        return tr("The first window must be the main window.");
    QSet<QString> seen;
    for (const QVariant &w : windows) {
        const QString err = validateNode(w.toMap().value(QStringLiteral("root")).toMap(), seen);
        if (!err.isEmpty())
            return err;
    }
    return {};
}

QString WorkspaceStore::saveWorkspace(const QVariantMap &input)
{
    QVariantMap ws = normalizedWorkspace(input);
    const QString err = validate(ws);
    if (!err.isEmpty())
        return err;

    QString id = ws.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = newId();
    ws[QStringLiteral("id")] = id;
    if (ws.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        ws[QStringLiteral("name")] = tr("Workspace");

    if (m_presets.contains(id)) {
        const QVariantMap preset = m_presets.value(id);
        ws[QStringLiteral("builtin")] = true;
        ws[QStringLiteral("modified")] = true;
        ws[QStringLiteral("order")] = preset.value(QStringLiteral("order"));
        if (preset.contains(QStringLiteral("portraitAlternative")))
            ws[QStringLiteral("portraitAlternative")] = preset.value(QStringLiteral("portraitAlternative"));
        else
            ws.remove(QStringLiteral("portraitAlternative"));
    } else {
        ws[QStringLiteral("builtin")] = false;
        ws.remove(QStringLiteral("modified"));
        ws.remove(QStringLiteral("order"));
    }

    store(ws);
    emit workspacesChanged();
    if (id == m_activeId)
        emit activeWorkspaceEdited();
    return {};
}

QString WorkspaceStore::duplicate(const QString &id)
{
    if (!exists(id))
        return {};
    QVariantMap ws = effective(id);
    const QString copy = newId();
    ws[QStringLiteral("id")] = copy;
    ws[QStringLiteral("name")] = tr("%1 copy").arg(ws.value(QStringLiteral("name")).toString());
    ws[QStringLiteral("builtin")] = false;
    ws.remove(QStringLiteral("modified"));
    ws.remove(QStringLiteral("order"));
    ws.remove(QStringLiteral("portraitAlternative"));
    store(ws);
    emit workspacesChanged();
    return copy;
}

bool WorkspaceStore::remove(const QString &id)
{
    const int i = userIndex(id);
    if (i < 0 || m_presets.contains(id))
        return false;
    m_user.removeAt(i);
    persist();
    if (id == m_activeId) {
        m_activeId = QLatin1String(kDefaultId);
        QSettings().setValue(QLatin1String(kActiveKey), m_activeId);
        emit activeIdChanged();
    }
    emit workspacesChanged();
    return true;
}

void WorkspaceStore::resetPreset(const QString &id)
{
    const int i = userIndex(id);
    if (i < 0 || !m_presets.contains(id))
        return;
    m_user.removeAt(i);
    persist();
    emit workspacesChanged();
    if (id == m_activeId)
        emit activeWorkspaceEdited();
}

void WorkspaceStore::rename(const QString &id, const QString &name)
{
    if (!exists(id) || name.trimmed().isEmpty())
        return;
    QVariantMap ws = effective(id);
    ws[QStringLiteral("name")] = name.trimmed();
    if (m_presets.contains(id))
        ws[QStringLiteral("modified")] = true;
    store(ws);
    emit workspacesChanged();
}

bool WorkspaceStore::editQuietly(const QString &id, const std::function<bool(QVariantMap &)> &edit)
{
    if (!exists(id))
        return false;
    QVariantMap ws = effective(id);
    if (!edit(ws))
        return false;
    if (m_presets.contains(id))
        ws[QStringLiteral("modified")] = true;
    store(ws);
    return true;
}

void WorkspaceStore::setSizes(const QString &id, const QString &windowId,
                              const QVariantList &nodePath, const QVariantList &sizes)
{
    editQuietly(id, [&](QVariantMap &ws) {
        QVariantList windows;
        int index = 0;
        if (!findWindow(ws, windowId, windows, index))
            return false;
        QVariantMap window = windows.at(index).toMap();
        QVariantMap root = window.value(QStringLiteral("root")).toMap();
        if (!setNodeSizes(root, nodePath, 0, sizes))
            return false;
        window[QStringLiteral("root")] = root;
        windows[index] = window;
        ws[QStringLiteral("windows")] = windows;
        return true;
    });
}

void WorkspaceStore::setWindowGeometry(const QString &id, const QString &windowId,
                                       const QRect &geometry, const QString &screenName)
{
    editQuietly(id, [&](QVariantMap &ws) {
        QVariantList windows;
        int index = 0;
        if (!findWindow(ws, windowId, windows, index))
            return false;
        QVariantMap window = windows.at(index).toMap();
        window[QStringLiteral("geometry")] = QVariantList{geometry.x(), geometry.y(),
                                                          geometry.width(), geometry.height()};
        window[QStringLiteral("screen")] = screenName;
        windows[index] = window;
        ws[QStringLiteral("windows")] = windows;
        return true;
    });
}
