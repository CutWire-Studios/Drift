#include "app/forge/ForgeController.h"

#include "app/editor/AppController.h"
#include "app/editor/WorkspaceStore.h"
#include "app/forge/ForgeDraft.h"
#include "app/forge/PackageInstaller.h"
#include "app/online/addons/AddonManager.h"
#include "engine/addons/AddonPackage.h"
#include "engine/compositor/FrameCompositor.h"
#include "engine/gpu/effects/GpuEffectExecutor.h"
#include "engine/packages/animations/AnimationPackageLoader.h"
#include "engine/packages/effects/EffectPackageLoader.h"
#include "engine/packages/transitions/TransitionPackageLoader.h"
#include "app/forge/compiler/Manifest.h"
#include "app/forge/compiler/PackageFiles.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/GraphOps.h"
#include "app/forge/edit/ParamOps.h"
#include "app/forge/nodes/Registry.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "engine/packages/PackageThumbnails.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <optional>

using namespace drift::forge;

namespace {

constexpr int kBurstMs = 400;
constexpr drift::TimeUs kSampleSpanUs = 4'000'000;
constexpr drift::TimeUs kLoopPadUs = 300'000;
constexpr drift::TimeUs kDefaultTransitionUs = 1'000'000;
const QString kWorkspaceId = QStringLiteral("forge");

QString rootDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// Each kind starts as a small working example rather than a bare pass-through, so a new document
// already shows how inputs, nodes and sliders fit together.
ForgeDoc seedDoc(Kind kind)
{
    ForgeDoc d = emptyDoc(kind);
    const KindInfo &info = kindInfo(kind);
    auto add = [&](const QString &type, double x, double y) {
        OpResult r = addNode(d, type, x, y);
        d = r.doc;
        return r.created.value(0);
    };
    auto wire = [&](const QString &from, const QString &fromSocket, const QString &to, const QString &toSocket) {
        d = drift::forge::connect(d, from, fromSocket, to, toSocket).doc;
    };
    auto slider = [&](const QString &node, const QString &input) {
        const OpResult r = expose(d, node, input);
        if (r.ok())
            d = r.doc;
    };
    const QString image = QStringLiteral("image");
    const QString value = QStringLiteral("value");

    switch (kind) {
    case Kind::Transition: {
        // Crossfade whose timing runs through an ease curve.
        const QString from = add(QStringLiteral("from"), 40, 40);
        const QString to = add(QStringLiteral("to"), 40, 200);
        const QString progress = add(QStringLiteral("progress"), 40, 360);
        const QString ease = add(QStringLiteral("ease"), 300, 360);
        const QString mix = add(QStringLiteral("mix"), 560, 160);
        const QString out = add(info.outputNode, 820, 160);
        wire(progress, value, ease, QStringLiteral("t"));
        wire(from, image, mix, QStringLiteral("a"));
        wire(to, image, mix, QStringLiteral("b"));
        wire(ease, value, mix, QStringLiteral("amount"));
        wire(mix, image, out, image);
        break;
    }
    case Kind::Animation: {
        // Eased fade from transparent to the clip; played out, it fades away.
        const QString video = add(QStringLiteral("video"), 40, 60);
        const QString progress = add(QStringLiteral("progress"), 40, 260);
        const QString ease = add(QStringLiteral("ease"), 300, 260);
        const QString mix = add(QStringLiteral("mix"), 560, 120);
        const QString out = add(info.outputNode, 820, 120);
        d = setData(d, ease, QStringLiteral("curve"), QStringLiteral("out"));
        d = setInput(d, mix, QStringLiteral("a"), QJsonArray{0, 0, 0, 0});
        wire(progress, value, ease, QStringLiteral("t"));
        wire(video, image, mix, QStringLiteral("b"));
        wire(ease, value, mix, QStringLiteral("amount"));
        wire(mix, image, out, image);
        break;
    }
    default: {
        // Colour wash with its colour and strength on sliders.
        const QString video = add(QStringLiteral("video"), 40, 160);
        const QString tint = add(QStringLiteral("tint"), 300, 160);
        const QString out = add(info.outputNode, 560, 160);
        wire(video, image, tint, image);
        wire(tint, image, out, image);
        slider(tint, QStringLiteral("color"));
        slider(tint, QStringLiteral("amount"));
        break;
    }
    }
    return d;
}

bool reaches(const ForgeDoc &doc, const QString &start, const QString &goal)
{
    QStringList stack{start};
    QSet<QString> seen;
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        if (id == goal)
            return true;
        if (seen.contains(id))
            continue;
        seen.insert(id);
        for (const ForgeEdge &e : doc.edges)
            if (e.from == id)
                stack << e.to;
    }
    return false;
}

const drift::Clip *clipAt(const drift::Project &p, int track, int clip)
{
    if (track < 0 || track >= p.tracks().size())
        return nullptr;
    const drift::Track &t = p.tracks().at(track);
    return clip >= 0 && clip < t.clips.size() ? &t.clips.at(clip) : nullptr;
}

QString clipLabel(const drift::Clip &c)
{
    return c.name.isEmpty() ? c.id.left(8) : c.name;
}

}

ForgeController::ForgeController(AppController *app, WorkspaceStore *workspaces, QObject *parent)
    : NodeGraphController(parent)
    , m_app(app)
    , m_workspaces(workspaces)
{
    m_undo.setUndoLimit(300);
    connect(&m_undo, &QUndoStack::indexChanged, this, &NodeGraphController::historyChanged);

    m_stageTimer.setSingleShot(true);
    m_stageTimer.setInterval(150);
    connect(&m_stageTimer, &QTimer::timeout, this, &ForgeController::stageNow);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(700);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_selectionTimer.setSingleShot(true);
    connect(&m_selectionTimer, &QTimer::timeout, this, [this] {
        if (m_follow && m_active)
            useSelection();
    });
    m_sampleTimer.setSingleShot(true);
    m_sampleTimer.setInterval(120);
    connect(&m_sampleTimer, &QTimer::timeout, this, [this] {
        if (m_active && !m_target.valid())
            applyPatch();
    });

    m_doc = seedDoc(Kind::Effect);
    refresh(true);
    refreshRecent();

    connect(m_workspaces, &WorkspaceStore::activeIdChanged, this,
            [this] { setActive(m_workspaces->activeId() == kWorkspaceId); });
    connect(m_app, &AppController::selectionChanged, this, [this] { m_selectionTimer.start(0); });
    connect(m_app, &AppController::selectedTransitionDataChanged, this, [this] { m_selectionTimer.start(0); });
    connect(m_app, &AppController::playheadSecondsChanged, this, [this] {
        if (m_active && !m_target.valid() && !m_app->playing())
            m_sampleTimer.start();
    });
    connect(m_app->playback(), &PlaybackEngine::playingChanged, this, [this] {
        if (!m_app->playback()->isPlaying())
            stopLoop();
    });
    if (m_app->addonManager())
        connect(m_app->addonManager(), &AddonManager::kindChanged, this, [this] { m_canEdit.clear(); });
    setActive(m_workspaces->activeId() == kWorkspaceId);
}

ForgeController::~ForgeController()
{
    if (m_dirty)
        save();
    if (m_active)
        clearPreview();
}

QString ForgeController::kind() const
{
    return kindInfo(m_doc.kind).name;
}

void ForgeController::setKind(const QString &name)
{
    Kind k;
    if (!kindFromName(name, &k) || k == Kind::Audio || k == m_doc.kind)
        return;
    newDoc(name);
}

QVariantMap ForgeController::meta() const
{
    const ForgeMeta &m = m_doc.meta;
    QVariantMap out{{"id", m.id},   {"displayName", m.displayName}, {"category", m.category},
                    {"description", m.description}, {"author", m.author}, {"version", m.version}};
    out["directions"] = m.directions ? QVariant(*m.directions) : QVariant(QStringList());
    out["defaultDurationMs"] = m.defaultDurationMs.value_or(0);
    return out;
}

QStringList ForgeController::categories() const
{
    return kindInfo(m_doc.kind).categories;
}

QVariantList ForgeController::params() const
{
    QVariantList out;
    for (const ParamDef &p : m_doc.params) {
        QVariantMap m = paramToJson(p).toVariantMap();
        m["used"] = paramUsers(m_doc, p.identifier);
        out.append(m);
    }
    return out;
}

QVariantMap ForgeController::previewValues() const
{
    QVariantMap out;
    for (const ParamDef &p : m_doc.params)
        out[p.identifier] = (m_values.contains(p.identifier) ? m_values.value(p.identifier) : p.defaultValue).toVariant();
    return out;
}

QStringList ForgeController::presetNames() const
{
    QStringList out;
    for (const Preset &p : m_doc.presets.value_or(QList<Preset>()))
        out << p.name;
    return out;
}

QVariantList ForgeController::compileErrors() const
{
    QVariantList out;
    for (const CompileError &e : m_result.errors)
        out.append(QVariantMap{{"node", e.node}, {"message", e.message}});
    for (const QString &s : m_stageErrors)
        out.append(QVariantMap{{"node", QString()}, {"message", s}});
    return out;
}

QVariantMap ForgeController::previewTarget() const
{
    if (!m_previewTarget)
        return {};
    return {{"node", m_previewTarget->first}, {"output", m_previewTarget->second}};
}

QList<NodeGraphController::CatalogEntry> ForgeController::catalog() const
{
    return catalogEntries(m_doc.kind);
}

void ForgeController::commit(const ForgeDoc &next, const QString &text, const QString &key, bool structural)
{
    m_recompile = structural;
    m_undo.push(new DocSnapshotCommand(m_doc, next, text, key, [this](const ForgeDoc &d) { applyDoc(d); }));
}

void ForgeController::applyDoc(const ForgeDoc &doc)
{
    m_doc = doc;
    const bool recompile = m_recompile;
    m_recompile = true;
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
    refresh(recompile);
    scheduleSave();
}

QString ForgeController::burstKey(const QString &base)
{
    if (base != m_lastBurstBase || !m_burstClock.isValid() || m_burstClock.elapsed() > kBurstMs)
        ++m_burst;
    m_lastBurstBase = base;
    m_burstClock.restart();
    return base + QLatin1Char('#') + QString::number(m_burst);
}

void ForgeController::refresh(bool recompile)
{
    if (recompile) {
        if (m_previewTarget && !m_doc.node(m_previewTarget->first)) {
            m_previewTarget.reset();
            emit previewTargetChanged();
        }
        for (auto it = m_values.begin(); it != m_values.end();) {
            if (m_doc.param(it.key()))
                ++it;
            else
                it = m_values.erase(it);
        }
        CompileOptions opts;
        opts.mode = CompileOptions::Preview;
        opts.target = m_previewTarget;
        m_result = compile(m_doc, opts);
        m_passes.clear();
        for (const CompiledPass &p : m_result.passes)
            m_passes.append(QVariantMap{{"file", p.file}, {"source", p.source}});
        m_stageErrors.clear();
    }
    nodes()->setNodes(nodeItems(m_doc, m_result.nodeErrors));
    edges()->setEdges(edgeItems(m_doc));
    if (recompile) {
        emit metaChanged();
        emit paramsChanged();
        emit previewValuesChanged();
        emit compiled();
        schedulePreview();
    }
    emit historyChanged();
}

void ForgeController::moveNodes(const QStringList &ids, qreal dx, qreal dy)
{
    ForgeDoc next = m_doc;
    for (const QString &id : ids)
        if (const ForgeNode *n = m_doc.node(id))
            next = moveNode(next, id, n->x + dx, n->y + dy);
    commit(next, tr("Move nodes"), QStringLiteral("move#%1").arg(m_moveSerial), false);
}

void ForgeController::endMove()
{
    ++m_moveSerial;
}

bool ForgeController::canConnect(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                 const QString &toPort) const
{
    const ForgeNode *a = m_doc.node(fromNode);
    const ForgeNode *b = m_doc.node(toNode);
    if (!a || !b || a == b)
        return false;
    const NodeDef *da = nodeDef(a->type);
    const NodeDef *db = nodeDef(b->type);
    if (!da || !db || !da->outputDef(fromPort) || !db->input(toPort))
        return false;
    return !reaches(m_doc, toNode, fromNode);
}

bool ForgeController::connectPorts(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                   const QString &toPort)
{
    const OpResult r = drift::forge::connect(m_doc, fromNode, fromPort, toNode, toPort);
    if (!r.ok()) {
        emit message(r.error);
        return false;
    }
    commit(r.doc, tr("Connect"));
    return true;
}

void ForgeController::disconnectInput(const QString &node, const QString &port)
{
    QStringList ids;
    for (const ForgeEdge &e : m_doc.edges)
        if (e.to == node && e.toSocket == port)
            ids << e.id;
    if (!ids.isEmpty())
        commit(drift::forge::disconnect(m_doc, ids), tr("Disconnect"));
}

void ForgeController::deleteNodes(const QStringList &ids)
{
    const ForgeDoc next = removeNodes(m_doc, ids);
    if (next.nodes.size() != m_doc.nodes.size())
        commit(next, tr("Delete nodes"));
}

void ForgeController::deleteSelection()
{
    deleteNodes(selection());
}

void ForgeController::duplicateSelection()
{
    if (selection().isEmpty())
        return;
    const OpResult r = duplicateNodes(m_doc, selection());
    if (!r.ok())
        return;
    commit(r.doc, tr("Duplicate nodes"));
    setSelection(r.created);
}

void ForgeController::copySelection()
{
    m_clipboard = selection();
}

void ForgeController::paste(const QPointF &at)
{
    QStringList ids;
    for (const QString &id : std::as_const(m_clipboard))
        if (m_doc.node(id))
            ids << id;
    if (ids.isEmpty())
        return;
    const OpResult r = duplicateNodes(m_doc, ids);
    if (!r.ok())
        return;
    double minX = 1e9, minY = 1e9;
    for (const QString &id : r.created) {
        minX = std::min(minX, r.doc.node(id)->x);
        minY = std::min(minY, r.doc.node(id)->y);
    }
    ForgeDoc next = r.doc;
    for (const QString &id : r.created) {
        const ForgeNode *n = next.node(id);
        next = moveNode(next, id, n->x - minX + at.x(), n->y - minY + at.y());
    }
    commit(next, tr("Paste nodes"));
    setSelection(r.created);
}

void ForgeController::undo()
{
    m_recompile = true;
    m_undo.undo();
}

void ForgeController::redo()
{
    m_recompile = true;
    m_undo.redo();
}

QString ForgeController::addNode(const QString &type, const QPointF &at)
{
    const OpResult r = drift::forge::addNode(m_doc, type, at.x(), at.y());
    if (!r.ok()) {
        emit message(r.error);
        return {};
    }
    commit(r.doc, tr("Add node"));
    return r.created.value(0);
}

QString ForgeController::addNodeConnected(const QString &type, const QPointF &at, const QString &fromNode,
                                          const QString &fromPort, bool fromIsOutput)
{
    OpResult r = drift::forge::addNode(m_doc, type, at.x(), at.y());
    if (!r.ok()) {
        emit message(r.error);
        return {};
    }
    const QString created = r.created.value(0);
    const ForgeNode *src = m_doc.node(fromNode);
    const NodeDef *srcDef = src ? nodeDef(src->type) : nullptr;
    QString portType;
    if (srcDef) {
        if (fromIsOutput) {
            if (const OutputDef *o = srcDef->outputDef(fromPort))
                portType = socketTypeName(o->type);
        } else if (const InputDef *i = srcDef->input(fromPort)) {
            portType = socketTypeName(i->type);
        }
    }
    for (const CatalogEntry &e : catalog()) {
        if (e.type != type)
            continue;
        const QString socket = firstCompatibleSocket(e, portType, fromIsOutput);
        if (socket.isEmpty())
            break;
        const OpResult wired = fromIsOutput ? drift::forge::connect(r.doc, fromNode, fromPort, created, socket)
                                            : drift::forge::connect(r.doc, created, socket, fromNode, fromPort);
        if (wired.ok())
            r.doc = wired.doc;
        break;
    }
    commit(r.doc, tr("Add node"));
    return created;
}

void ForgeController::setInputValue(const QString &node, const QString &input, const QVariant &value, bool coalesce)
{
    const QString key = coalesce ? burstKey(QStringLiteral("val:%1:%2").arg(node, input)) : QString();
    commit(setInput(m_doc, node, input, QJsonValue::fromVariant(value)), tr("Edit value"), key);
}

QVariantList ForgeController::nodeContextActions(const QString &id) const
{
    QVariantList out;
    const ForgeNode *n = m_doc.node(id);
    const NodeDef *def = n ? nodeDef(n->type) : nullptr;
    if (!def)
        return out;
    auto entry = [&](const QString &action, const QString &label, bool enabled = true) {
        out.append(QVariantMap{{"id", action}, {"label", label}, {"enabled", enabled}});
    };
    for (const InputDef &in : def->inputs) {
        bool connected = false;
        for (const ForgeEdge &e : m_doc.edges)
            connected = connected || (e.to == id && e.toSocket == in.id);
        if (isParamRef(n->inputs.value(in.id)))
            entry(QStringLiteral("unexpose:") + in.id, tr("Unexpose %1").arg(in.label));
        else if (!connected && !in.noExpose && !in.clock && in.type != SocketType::Color)
            entry(QStringLiteral("expose:") + in.id, tr("Expose %1 as slider").arg(in.label));
        else if (!connected && in.widget == QLatin1String("swatch"))
            entry(QStringLiteral("expose:") + in.id, tr("Expose %1 as slider").arg(in.label));
    }
    for (const OptionDef &o : def->options) {
        if (!o.exposable())
            continue;
        if (isParamRef(n->data.value(o.id)))
            entry(QStringLiteral("unexposeOpt:") + o.id, tr("Unexpose %1").arg(o.label));
        else
            entry(QStringLiteral("exposeOpt:") + o.id, tr("Expose %1 as slider").arg(o.label));
    }
    bool wired = false;
    for (const ForgeEdge &e : m_doc.edges)
        wired = wired || e.from == id || e.to == id;
    entry(QStringLiteral("disconnectAll"), tr("Disconnect all"), wired);
    for (const OutputDef &o : def->outputs)
        entry(QStringLiteral("previewNode:") + o.id,
              def->outputs.size() > 1 ? tr("Preview %1").arg(o.label) : tr("Preview this node"));
    if (m_previewTarget)
        entry(QStringLiteral("previewOutput"), tr("Preview output"));
    return out;
}

void ForgeController::triggerNodeAction(const QString &id, const QString &action)
{
    const int colon = action.indexOf(QLatin1Char(':'));
    const QString verb = colon < 0 ? action : action.left(colon);
    const QString arg = colon < 0 ? QString() : action.mid(colon + 1);
    if (verb == QLatin1String("expose"))
        exposeInput(id, arg);
    else if (verb == QLatin1String("unexpose"))
        unexposeInput(id, arg);
    else if (verb == QLatin1String("exposeOpt"))
        exposeOption(id, arg);
    else if (verb == QLatin1String("unexposeOpt"))
        unexposeOption(id, arg);
    else if (verb == QLatin1String("previewNode"))
        previewNode(id, arg);
    else if (verb == QLatin1String("previewOutput"))
        previewOutput();
    else if (verb == QLatin1String("disconnectAll")) {
        QStringList ids;
        for (const ForgeEdge &e : m_doc.edges)
            if (e.from == id || e.to == id)
                ids << e.id;
        if (!ids.isEmpty())
            commit(drift::forge::disconnect(m_doc, ids), tr("Disconnect"));
    }
}

QVariantMap ForgeController::nodeInfo(const QString &id) const
{
    const ForgeNode *n = m_doc.node(id);
    const NodeDef *def = n ? nodeDef(n->type) : nullptr;
    if (!def)
        return {};
    const int row = nodes()->rowOfNode(id);
    return {{"id", id},
            {"type", def->type},
            {"title", def->label},
            {"category", def->category},
            {"description", def->description},
            {"deletable", !def->output},
            {"inputs", row >= 0 ? nodes()->get(row).value("inputs") : QVariant(QVariantList())}};
}

QVariantList ForgeController::nodeOptions(const QString &id) const
{
    return nodeOptionList(m_doc, id);
}

void ForgeController::setNodeOption(const QString &node, const QString &option, const QVariant &value, bool coalesce)
{
    const QString key = coalesce ? burstKey(QStringLiteral("opt:%1:%2").arg(node, option)) : QString();
    commit(setData(m_doc, node, option, QJsonValue::fromVariant(value)), tr("Edit option"), key);
}

void ForgeController::exposeInput(const QString &node, const QString &input)
{
    const OpResult r = expose(m_doc, node, input);
    if (!r.ok())
        emit message(r.error);
    else
        commit(r.doc, tr("Expose as slider"));
}

void ForgeController::unexposeInput(const QString &node, const QString &input)
{
    commit(unexpose(m_doc, node, input), tr("Unexpose slider"));
}

void ForgeController::exposeOption(const QString &node, const QString &option)
{
    const OpResult r = drift::forge::exposeOption(m_doc, node, option);
    if (!r.ok())
        emit message(r.error);
    else
        commit(r.doc, tr("Expose as slider"));
}

void ForgeController::unexposeOption(const QString &node, const QString &option)
{
    commit(drift::forge::unexposeOption(m_doc, node, option), tr("Unexpose slider"));
}

void ForgeController::updateParam(const QString &id, const QVariantMap &patch, bool coalesce)
{
    const OpResult r = drift::forge::updateParam(m_doc, id, QJsonObject::fromVariantMap(patch));
    if (!r.ok()) {
        emit message(r.error);
        return;
    }
    const QString renamed = patch.value(QStringLiteral("identifier")).toString();
    if (!renamed.isEmpty() && renamed != id && m_values.contains(id))
        m_values.insert(renamed, m_values.take(id));
    if (patch.contains(QStringLiteral("default")))
        m_values.remove(id);
    const QString key = coalesce ? burstKey(QStringLiteral("param:%1:%2").arg(id, patch.keys().join(','))) : QString();
    commit(r.doc, tr("Edit slider"), key);
}

void ForgeController::moveParam(const QString &id, int delta)
{
    commit(drift::forge::moveParam(m_doc, id, delta), tr("Reorder sliders"));
}

void ForgeController::removeParam(const QString &id)
{
    commit(drift::forge::removeParam(m_doc, id), tr("Remove slider"));
}

void ForgeController::setPreviewValue(const QString &name, const QVariant &value)
{
    m_values.insert(name, QJsonValue::fromVariant(value));
    emit previewValuesChanged();
    applyPatch();
}

void ForgeController::resetPreviewValues()
{
    m_values.clear();
    emit previewValuesChanged();
    applyPatch();
}

void ForgeController::addPreset(const QString &name)
{
    QJsonObject values;
    const QVariantMap current = previewValues();
    for (auto it = current.begin(); it != current.end(); ++it)
        values.insert(it.key(), QJsonValue::fromVariant(it.value()));
    commit(savePreset(m_doc, name, values), tr("Save preset"));
}

void ForgeController::applyPreset(const QString &name)
{
    for (const Preset &p : m_doc.presets.value_or(QList<Preset>())) {
        if (p.name != name)
            continue;
        m_values.clear();
        for (auto it = p.values.begin(); it != p.values.end(); ++it)
            m_values.insert(it.key(), it.value());
        emit previewValuesChanged();
        applyPatch();
        return;
    }
}

void ForgeController::deletePreset(const QString &name)
{
    commit(drift::forge::deletePreset(m_doc, name), tr("Delete preset"));
}

QString ForgeController::setMeta(const QString &key, const QVariant &value)
{
    ForgeDoc next = m_doc;
    ForgeMeta &m = next.meta;
    if (key == QLatin1String("id")) {
        static const QRegularExpression valid(QStringLiteral("^[a-z0-9][a-z0-9_.]*$"));
        const QString id = value.toString();
        if (!valid.match(id).hasMatch())
            return tr("Use lowercase letters, digits, underscores and dots, starting with a letter or digit.");
        m.id = id;
    } else if (key == QLatin1String("displayName")) {
        m.displayName = value.toString();
    } else if (key == QLatin1String("category")) {
        m.category = value.toString();
    } else if (key == QLatin1String("description")) {
        m.description = value.toString();
    } else if (key == QLatin1String("author")) {
        m.author = value.toString();
    } else if (key == QLatin1String("version")) {
        m.version = value.toString();
    } else if (key == QLatin1String("directions")) {
        m.directions = value.toStringList();
    } else if (key == QLatin1String("defaultDurationMs")) {
        const double ms = value.toDouble();
        m.defaultDurationMs = ms > 0 ? std::optional<double>(ms) : std::nullopt;
    } else {
        return tr("Unknown field.");
    }
    commit(next, tr("Edit details"), burstKey(QStringLiteral("meta:") + key));
    return {};
}

void ForgeController::replaceDoc(const ForgeDoc &doc)
{
    m_undo.clear();
    m_values.clear();
    m_previewTarget.reset();
    m_clipboard.clear();
    setSelection({});
    m_stagedKey.clear();
    if (!m_stagedKind.isEmpty() && m_stagedKind != kindInfo(doc.kind).name)
        clearDraft(m_stagedKind);
    m_stagedKind.clear();
    const bool kindChange = doc.kind != m_doc.kind;
    m_doc = doc;
    m_recompile = true;
    m_dirty = false;
    emit dirtyChanged();
    emit previewTargetChanged();
    if (kindChange)
        emit kindChanged();
    if (m_target.valid() || m_active)
        useSelection();
    refresh(true);
}

void ForgeController::newDoc(const QString &name)
{
    Kind k;
    if (!kindFromName(name, &k) || k == Kind::Audio)
        return;
    if (m_dirty)
        save();
    replaceDoc(seedDoc(k));
    refreshRecent();
}

void ForgeController::activateWorkspace()
{
    m_workspaces->setActiveId(kWorkspaceId);
}

void ForgeController::createNew(const QString &name)
{
    newDoc(name);
    activateWorkspace();
}

bool ForgeController::canEdit(const QString &kindName, const QString &id)
{
    const QString key = kindName + QLatin1Char(':') + id;
    const auto cached = m_canEdit.constFind(key);
    if (cached != m_canEdit.constEnd())
        return *cached;
    bool found = false;
    const auto pk = drift::forge::packageKind(kindName);
    if (pk && kindName != QLatin1String("audio_effect")) {
        const QString dir = drift::forge::packageDirForId(*pk, id);
        found = drift::forge::isUserDir(*pk, dir) && QFile::exists(QDir(dir).filePath(QStringLiteral("forge.json")));
    }
    m_canEdit.insert(key, found);
    return found;
}

bool ForgeController::openPackage(const QString &kindName, const QString &id)
{
    if (!canEdit(kindName, id))
        return false;
    const auto pk = drift::forge::packageKind(kindName);
    QFile file(QDir(drift::forge::packageDirForId(*pk, id)).filePath(QStringLiteral("forge.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QString error;
    ForgeDoc doc = ForgeDoc::fromJsonText(file.readAll(), &error);
    if (!error.isEmpty() || doc.kind == Kind::Audio || kindInfo(doc.kind).name != kindName) {
        emit message(error.isEmpty() ? tr("This package was not made in Forge.") : error);
        return false;
    }
    doc.meta.id = id;
    if (m_dirty)
        save();
    replaceDoc(doc);
    m_savedId = id;
    activateWorkspace();
    return true;
}

QString ForgeController::stageForExport(const PackageContents &contents, const QString &dir) const
{
    QJsonObject trusted;
    for (const PackageFile &f : contents.files)
        trusted.insert(f.path, QString::fromLatin1(f.data.toBase64()));
    const QJsonObject error = drift::forge::stage(*drift::forge::packageKind(kind()), dir, contents.manifest,
                                                  QJsonObject{{"trusted_files", trusted}});
    return error.isEmpty() ? QString() : error.value("detail").toString(error.value("error").toString());
}

QImage ForgeController::frameAt(drift::TimeUs timeUs, int side) const
{
    const drift::Project project = m_app->project()->detachedCopy();
    FrameCompositor::RenderOptions options;
    options.previewScale = std::min(1.0, 512.0 / std::max(1, project.width()));
    FrameCompositor compositor;
    compositor.setProject(&project);
    QImage image = compositor.compositeAt(timeUs, options).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        return {};
    const int edge = std::min(image.width(), image.height());
    image = image.copy((image.width() - edge) / 2, (image.height() - edge) / 2, edge, edge);
    return image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QByteArray ForgeController::thumbnailPng(const PackageContents &contents, const CompileResult &compiled)
{
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return {};
    const QString dir = tmp.filePath(QStringLiteral("pkg"));
    if (!stageForExport(contents, dir).isEmpty())
        return {};

    QString error;
    drift::GpuEffectDefinition gpu;
    QString keyPrefix;
    switch (m_doc.kind) {
    case Kind::Effect: gpu = EffectPackageLoader::loadPackage(dir, &error).gpu; break;
    case Kind::Transition:
        gpu = TransitionPackageLoader::loadPackage(dir, &error).gpu;
        keyPrefix = QLatin1String(kTransitionCacheKeyPrefix);
        break;
    case Kind::Animation:
        gpu = AnimationPackageLoader::loadPackage(dir, &error).gpu;
        keyPrefix = QLatin1String(kAnimationCacheKeyPrefix);
        break;
    default: return {};
    }
    if (!error.isEmpty() || !gpu.valid)
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QJsonDocument(contents.manifest).toJson(QJsonDocument::Compact));
    for (const PackageFile &f : contents.files)
        hash.addData(f.data);
    const QString key = keyPrefix + QStringLiteral("forge.export.") + QString::fromLatin1(hash.result().toHex());
    const QMap<QString, QVariant> params = previewParams(m_doc, compiled, m_values);

    const drift::TimeUs playhead = drift::secondsToUs(m_app->playheadSeconds());
    QImage source;
    QImage to;
    if (m_target.valid()) {
        if (m_doc.kind == Kind::Transition) {
            const drift::TimeUs margin = 33'000;
            source = frameAt(std::max<drift::TimeUs>(0, m_target.windowStart - margin), 128);
            to = frameAt(m_target.windowEnd + margin, 128);
        } else {
            const bool inside = playhead >= m_target.clipStart && playhead < m_target.clipEnd;
            source = frameAt(inside ? playhead : (m_target.clipStart + m_target.clipEnd) / 2,
                             m_doc.kind == Kind::Effect ? 256 : 128);
        }
    }

    QImage image;
    switch (m_doc.kind) {
    case Kind::Effect: image = renderEffectThumbnail(key, gpu, params, QSize(256, 256), source); break;
    case Kind::Transition: image = renderTransitionStrip(key, gpu, params, QSize(128, 128), 12, source, to); break;
    default: image = renderAnimationStrip(key, gpu, params, QSize(128, 128), 12, false, source); break;
    }
    if (image.isNull())
        return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

bool ForgeController::buildPackage(PackageContents *out, QString *error)
{
    if (m_doc.meta.id.isEmpty()) {
        ForgeDoc next = m_doc;
        next.meta.id = exportId(m_doc);
        commit(next, tr("Name the package"));
    }
    if (const QString problem = exportProblem(m_doc); !problem.isEmpty()) {
        *error = problem;
        return false;
    }
    const CompileResult compiled = compile(m_doc);
    if (!compiled.ok) {
        QStringList messages;
        for (const CompileError &e : compiled.errors)
            messages << e.message;
        *error = messages.join(QLatin1Char('\n'));
        return false;
    }
    *out = packageContents(m_doc, compiled);
    const QByteArray png = thumbnailPng(*out, compiled);
    if (!png.isEmpty())
        out->files.append({kindInfo(m_doc.kind).previewFile, png});
    return true;
}

QVariantMap ForgeController::install(bool overwrite)
{
    const auto fail = [](const QString &text, const char *code = "error") {
        return QVariantMap{{"ok", false}, {"error", text}, {"code", QString::fromLatin1(code)}};
    };
    PackageContents contents;
    QString error;
    if (!buildPackage(&contents, &error))
        return fail(error, "invalid");
    const drift::forge::PackageKind pk = *drift::forge::packageKind(kind());
    const QString id = m_doc.meta.id;
    const QString target = QDir(drift::forge::userRoot(pk)).filePath(id);
    if (!overwrite && QFileInfo::exists(target))
        return fail(tr("%1 is already installed.").arg(m_doc.meta.displayName), "exists");

    QJsonObject trusted;
    for (const PackageFile &f : contents.files)
        trusted.insert(f.path, QString::fromLatin1(f.data.toBase64()));
    const QJsonObject reply = drift::forge::install(
        m_app, pk, target, id, QJsonObject{{"manifest", contents.manifest}, {"trusted_files", trusted}});
    if (!reply.value("ok").toBool())
        return fail(reply.value("detail").toString(reply.value("error").toString()), "install");
    m_canEdit.clear();
    QVariantMap result{{"ok", true}, {"id", id}};
    if (reply.contains("warning"))
        result.insert("warning", reply.value("warning").toString());
    return result;
}

QVariantMap ForgeController::exportDriftfx(const QUrl &url)
{
    const auto fail = [](const QString &text) { return QVariantMap{{"ok", false}, {"error", text}}; };
    PackageContents contents;
    QString error;
    if (!buildPackage(&contents, &error))
        return fail(error);
    QTemporaryDir tmp;
    const QString dir = tmp.filePath(m_doc.meta.id);
    if (!tmp.isValid() || !(error = stageForExport(contents, dir)).isEmpty())
        return fail(error);
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.endsWith(QLatin1String(".driftfx")))
        path += QLatin1String(".driftfx");
    const QJsonObject meta{
        {"id", QStringLiteral("user.") + m_doc.meta.id},
        {"name", m_doc.meta.displayName},
        {"version", m_doc.meta.version.isEmpty() ? QStringLiteral("1.0.0") : m_doc.meta.version},
        {"author", m_doc.meta.author},
        {"description", m_doc.meta.description},
        {"details", QString()},
        {"license", QString()},
        {"generator", QStringLiteral("drift-forge")},
    };
    if (!drift::addon::writeUserPackage(dir, drift::forge::packageKind(kind())->subdir, meta, path, &error))
        return fail(error);
    return {{"ok", true}, {"path", path}};
}

QVariantMap ForgeController::exportFolder(const QUrl &url)
{
    const auto fail = [](const QString &text) { return QVariantMap{{"ok", false}, {"error", text}}; };
    PackageContents contents;
    QString error;
    if (!buildPackage(&contents, &error))
        return fail(error);
    const QString root = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString dir = QDir(root).filePath(m_doc.meta.id);
    const QString manifest = drift::forge::packageKind(kind())->manifest;
    if (QFileInfo::exists(dir) && !QFile::exists(QDir(dir).filePath(manifest)))
        return fail(tr("%1 already exists and is not a package folder.").arg(dir));
    if (!(error = stageForExport(contents, dir)).isEmpty())
        return fail(error);
    return {{"ok", true}, {"path", dir}};
}

QString ForgeController::projectDir(const QString &id) const
{
    return rootDir() + QStringLiteral("/forge-projects/") + id;
}

bool ForgeController::open(const QString &pathOrId)
{
    const QString path = QFileInfo(pathOrId).isFile() ? pathOrId : projectDir(pathOrId) + QStringLiteral("/forge.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message(tr("Cannot open %1").arg(path));
        return false;
    }
    QString error;
    const ForgeDoc doc = ForgeDoc::fromJsonText(file.readAll(), &error);
    if (!error.isEmpty() || doc.kind == Kind::Audio) {
        emit message(error.isEmpty() ? tr("Audio effects are not supported yet.") : error);
        return false;
    }
    if (m_dirty)
        save();
    replaceDoc(doc);
    m_savedId = doc.meta.id;
    return true;
}

void ForgeController::scheduleSave()
{
    m_saveTimer.start();
}

bool ForgeController::save()
{
    m_saveTimer.stop();
    const QString dir = projectDir(m_doc.meta.id);
    if (!QDir().mkpath(dir))
        return false;
    QFile file(dir + QStringLiteral("/forge.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(m_doc.toJson()).toJson(QJsonDocument::Compact));
    file.close();
    if (!m_savedId.isEmpty() && m_savedId != m_doc.meta.id)
        QDir(projectDir(m_savedId)).removeRecursively();
    m_savedId = m_doc.meta.id;
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
    refreshRecent();
    return true;
}

void ForgeController::refreshRecent()
{
    struct Entry
    {
        QDateTime modified;
        QVariantMap data;
    };
    QList<Entry> found;
    const QDir root(rootDir() + QStringLiteral("/forge-projects"));
    for (const QString &id : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(root.filePath(id + QStringLiteral("/forge.json")));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        if (o.value("kind").toString() == QLatin1String("audio"))
            continue;
        const QDateTime modified = QFileInfo(file).lastModified();
        found.append({modified, {{"id", id},
                                  {"name", o.value("meta").toObject().value("displayName").toString(id)},
                                  {"kind", o.value("kind").toString()},
                                  {"modified", modified.toString(Qt::ISODate)}}});
    }
    std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) { return a.modified > b.modified; });
    m_recent.clear();
    for (const Entry &e : found)
        m_recent.append(e.data);
    emit recentChanged();
}

void ForgeController::previewNode(const QString &node, const QString &output)
{
    m_previewTarget = std::make_pair(node, output);
    emit previewTargetChanged();
    refresh(true);
}

void ForgeController::previewOutput()
{
    m_previewTarget.reset();
    emit previewTargetChanged();
    refresh(true);
}

void ForgeController::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        useSelection();
        schedulePreview();
        return;
    }
    clearPreview();
}

void ForgeController::clearPreview()
{
    m_stageTimer.stop();
    m_sampleTimer.stop();
    stopLoop();
    m_app->playback()->setPreviewPatch({});
    for (const char *k : {"effect", "transition", "animation"})
        clearDraft(QLatin1String(k));
    m_stagedKey.clear();
    m_stagedKind.clear();
}

void ForgeController::schedulePreview()
{
    if (!m_active || !m_result.ok)
        return;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QJsonDocument(packageManifest(m_doc, m_result)).toJson(QJsonDocument::Compact));
    for (const CompiledPass &p : m_result.passes) {
        hash.addData(p.file.toUtf8());
        hash.addData(p.source.toUtf8());
    }
    for (const ForgeAsset &a : m_doc.assets) {
        hash.addData(a.id.toUtf8());
        hash.addData(a.data.toLatin1());
    }
    const QString key = QString::fromLatin1(hash.result().toHex());
    if (key != m_stagedKey || m_stagedKind != kind()) {
        m_pendingKey = key;
        m_stageTimer.start();
        return;
    }
    m_stageTimer.stop();
    applyPatch();
}

void ForgeController::stageNow()
{
    if (!m_active || !m_result.ok)
        return;
    QMap<QString, QByteArray> files;
    for (const CompiledPass &p : m_result.passes)
        files.insert(p.file, p.source.toUtf8());
    for (const QJsonValue &v : m_result.textures) {
        const QJsonObject t = v.toObject();
        files.insert(t.value("file").toString(), assetBytes(m_doc, t.value("assetId").toString()));
    }
    for (const ParamDef &p : m_doc.params) {
        if (p.type != ParamType::Image)
            continue;
        const QString file = imageParamFile(m_doc, p);
        if (!file.isEmpty())
            files.insert(file, assetBytes(m_doc, p.defaultValue.toString()));
    }
    const QString kindName = kind();
    const DraftResult r = stageDraft(kindName, packageManifest(m_doc, m_result), files);
    m_stageErrors.clear();
    if (r.ok) {
        m_stagedKey = m_pendingKey;
        m_stagedKind = kindName;
    } else {
        m_stageErrors << r.error;
        m_stageErrors << r.shaderLog;
    }
    emit compiled();
    if (r.ok)
        applyPatch();
}

PreviewPatch ForgeController::targetPatch(const Target &t, const QMap<QString, QVariant> &params) const
{
    switch (m_doc.kind) {
    case Kind::Transition: return patchTransition(t.fromId, t.toId, params, t.durationUs);
    case Kind::Animation: return patchAnimation(t.clipId, !m_animOut, params, drift::TimeUs(m_animDurationMs) * 1000);
    default: return patchAddEffect(t.clipId, params);
    }
}

QString ForgeController::samplePath(int which)
{
    const QString dir = rootDir() + QStringLiteral("/forge-drafts/samples");
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/sample%2-v2.png").arg(dir).arg(which);
    if (!QFile::exists(path))
        defaultSampleImage(which, QSize(1280, 720)).save(path);
    return path;
}

ForgeController::Target ForgeController::sampleTarget() const
{
    Target t;
    t.description = tr("Built-in sample");
    const drift::TimeUs playhead = drift::secondsToUs(m_app->playheadSeconds());
    if (m_doc.kind == Kind::Transition) {
        const drift::TimeUs cut = std::max<drift::TimeUs>(playhead, 2'000'000);
        t.fromId = QStringLiteral("forge.sample.a");
        t.toId = QStringLiteral("forge.sample.b");
        t.durationUs = kDefaultTransitionUs;
        t.clipStart = cut - 2'000'000;
        t.clipEnd = cut + 2'000'000;
        t.windowStart = cut - kDefaultTransitionUs / 2;
        t.windowEnd = cut + kDefaultTransitionUs / 2;
    } else {
        t.clipId = QStringLiteral("forge.sample.a");
        t.clipStart = playhead;
        t.clipEnd = playhead + kSampleSpanUs;
    }
    return t;
}

PreviewPatch ForgeController::samplePatch(const QMap<QString, QVariant> &params)
{
    const Target t = sampleTarget();
    const bool transition = m_doc.kind == Kind::Transition;
    const QString pathA = samplePath(transition ? 1 : 0);
    const QString pathB = transition ? samplePath(2) : QString();
    const PreviewPatch inner = targetPatch(t, params);
    return [=](drift::Project &project) {
        drift::Track track;
        track.id = QStringLiteral("forge.sample.track");
        track.type = drift::trackTypeForClipType(drift::ClipType::Image);
        auto image = [&](const QString &id, const QString &path, drift::TimeUs start, drift::TimeUs end) {
            drift::Clip c;
            c.id = id;
            c.type = drift::ClipType::Image;
            c.name = QStringLiteral("Sample");
            c.path = path;
            c.thumbnailPath = path;
            c.filmstripPath = path;
            c.timelineStart = start;
            c.timelineDuration = end - start;
            c.srcIn = 0;
            c.srcOut = end - start;
            return c;
        };
        if (transition) {
            const drift::TimeUs cut = (t.clipStart + t.clipEnd) / 2;
            track.clips.append(image(t.fromId, pathA, t.clipStart, cut));
            track.clips.append(image(t.toId, pathB, cut, t.clipEnd));
        } else {
            track.clips.append(image(t.clipId, pathA, t.clipStart, t.clipEnd));
        }
        project.tracks().prepend(track);
        inner(project);
    };
}

void ForgeController::applyPatch()
{
    if (!m_active || m_stagedKey.isEmpty() || m_stagedKind != kind())
        return;
    const QMap<QString, QVariant> values = previewParams(m_doc, m_result, m_values);
    m_app->playback()->setPreviewPatch(m_target.valid() ? targetPatch(m_target, values) : samplePatch(values));
}

void ForgeController::setTarget(const Target &t)
{
    if (m_looping)
        stopLoop();
    m_target = t;
    emit targetChanged();
    applyPatch();
}

void ForgeController::setFollowSelection(bool on)
{
    if (on == m_follow)
        return;
    m_follow = on;
    emit targetChanged();
    if (on && m_active)
        useSelection();
}

void ForgeController::setAnimDirection(const QString &dir)
{
    const bool out = dir == QLatin1String("out");
    if (out == m_animOut)
        return;
    m_animOut = out;
    emit animChanged();
    applyPatch();
}

void ForgeController::setAnimDurationMs(int ms)
{
    ms = std::clamp(ms, 50, 10000);
    if (ms == m_animDurationMs)
        return;
    m_animDurationMs = ms;
    emit animChanged();
    applyPatch();
}

void ForgeController::useSelection()
{
    const drift::Project *project = m_app->project();
    Target t;
    if (m_doc.kind == Kind::Transition) {
        int track = m_app->selectedTransitionTrack();
        int left = m_app->selectedTransitionLeftClip();
        if (track < 0 || left < 0) {
            track = m_app->selectedTrack();
            left = -1;
            if (track >= 0 && track < project->tracks().size()) {
                const drift::Track &tr_ = project->tracks().at(track);
                const drift::TimeUs playhead = drift::secondsToUs(m_app->playheadSeconds());
                drift::TimeUs best = std::numeric_limits<drift::TimeUs>::max();
                for (int i = 0; i + 1 < tr_.clips.size(); ++i) {
                    if (!m_app->hasTransitionPartner(track, i))
                        continue;
                    const drift::TimeUs dist = std::abs(tr_.clips.at(i).timelineEnd() - playhead);
                    if (dist < best) {
                        best = dist;
                        left = i;
                    }
                }
            }
        }
        if (track >= 0 && track < project->tracks().size() && left >= 0) {
            const drift::Track &tk = project->tracks().at(track);
            const drift::Clip *from = clipAt(*project, track, left);
            const drift::Clip *to = nullptr;
            drift::Transition existing;
            existing.durationUs = kDefaultTransitionUs;
            for (const drift::Transition &x : tk.transitions) {
                if (from && x.fromClipId == from->id) {
                    existing = x;
                    to = drift::clipById(tk, x.toClipId);
                }
            }
            if (!to)
                to = clipAt(*project, track, left + 1);
            if (from && to) {
                t.fromId = from->id;
                t.toId = to->id;
                t.durationUs = existing.durationUs;
                existing.fromClipId = from->id;
                existing.toClipId = to->id;
                if (!drift::transitionWindow(tk, existing, t.windowStart, t.windowEnd)) {
                    t.windowStart = from->timelineEnd() - t.durationUs / 2;
                    t.windowEnd = from->timelineEnd() + t.durationUs / 2;
                }
                t.description = tr("Cut: %1 to %2").arg(clipLabel(*from), clipLabel(*to));
            }
        }
    } else if (const drift::Clip *clip = clipAt(*project, m_app->selectedTrack(), m_app->selectedClip())) {
        t.clipId = clip->id;
        t.clipStart = clip->timelineStart;
        t.clipEnd = clip->timelineEnd();
        t.description = tr("Clip: %1").arg(clipLabel(*clip));
    }
    if (!t.valid())
        t.description = tr("Built-in sample");
    setTarget(t);
}

bool ForgeController::loopRange(const Target &t, drift::TimeUs *start, drift::TimeUs *end) const
{
    switch (m_doc.kind) {
    case Kind::Transition:
        *start = std::max<drift::TimeUs>(0, t.windowStart - kLoopPadUs);
        *end = t.windowEnd + kLoopPadUs;
        return true;
    case Kind::Animation: {
        const drift::TimeUs span = drift::TimeUs(m_animDurationMs) * 1000;
        const drift::TimeUs a = m_animOut ? std::max(t.clipStart, t.clipEnd - span) : t.clipStart;
        const drift::TimeUs b = m_animOut ? t.clipEnd : std::min(t.clipEnd, t.clipStart + span);
        *start = std::max<drift::TimeUs>(0, a - kLoopPadUs);
        *end = b + kLoopPadUs;
        return true;
    }
    default:
        if (t.clipEnd <= t.clipStart || m_target.valid())
            return false;
        *start = t.clipStart;
        *end = t.clipEnd;
        return true;
    }
}

void ForgeController::playLoop()
{
    if (!m_active)
        return;
    if (m_looping) {
        m_app->setPlaying(false);
        return;
    }
    drift::TimeUs start = 0, end = 0;
    if (loopRange(m_target.valid() ? m_target : sampleTarget(), &start, &end)) {
        m_app->playback()->setPreviewRange(start, end);
        m_app->setPlayheadSeconds(drift::usToSeconds(start));
        m_looping = true;
        emit loopingChanged();
    }
    m_app->setPlaying(true);
}

void ForgeController::stopLoop()
{
    if (!m_looping)
        return;
    m_looping = false;
    m_app->playback()->clearPreviewRange();
    emit loopingChanged();
    if (m_app->playing())
        m_app->setPlaying(false);
}
