#include "app/forge/AudioForgeController.h"

#include "app/editor/AppController.h"
#include "app/editor/WorkspaceStore.h"
#include "app/forge/ForgeDraft.h"
#include "app/forge/ForgeGraphMapping.h"
#include "app/forge/PackageInstaller.h"
#include "app/forge/audio/AudioManifest.h"
#include "app/forge/audio/RackGraphMapping.h"
#include "app/forge/audio/RackValidate.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/ParamOps.h"
#include "core/project/Project.h"
#include "engine/addons/AddonPackage.h"
#include "engine/audio/dsp/graph/AudioGraph.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"
#include "app/online/addons/AddonManager.h"

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

#include <algorithm>

using namespace drift::forge;
namespace rg = drift::forge::rackgraph;

namespace {

constexpr int kBurstMs = 400;
constexpr drift::TimeUs kLoopSpanUs = 4'000'000;
constexpr qint64 kMaxIrBytes = 16 * 1024 * 1024;
const QString kWorkspaceId = QStringLiteral("audio-forge");
const QString kPackageKind = QStringLiteral("audio_effect");

QString rootDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString clipLabel(const drift::Clip &c)
{
    return c.name.isEmpty() ? c.id.left(8) : c.name;
}

// A new document starts as a small working rack: a low-pass filter swept by an LFO into a delay,
// with the delay's mix on a slider.
ForgeDoc seedDoc()
{
    ForgeDoc d = emptyDoc(Kind::Audio);
    auto apply = [&](const OpResult &r) {
        if (r.ok())
            d = r.doc;
        return r.created.value(0);
    };
    const QString filter = apply(addPedal(d, QStringLiteral("filter")));
    const QString delay = apply(addPedal(d, QStringLiteral("delay")));
    const QString lfo = apply(addModulator(d, QStringLiteral("lfo")));
    apply(addRoute(d, lfo, filter, QStringLiteral("cutoff"), 0.5));
    apply(exposeValue(d, ValuePath::knobOf(delay, QStringLiteral("mix"))));
    return d;
}

double asNumber(const QJsonValue &v)
{
    return v.isBool() ? (v.toBool() ? 1.0 : 0.0) : v.toDouble();
}

QString normalizedKind(const QString &kind)
{
    return kind == QLatin1String("audio") ? kPackageKind : kind;
}

ForgeDoc duplicateBoardNodes(const ForgeDoc &doc, const QStringList &ids, QStringList *created)
{
    Rack rack = rackOf(doc);
    QMap<QString, QPointF> layout = rg::layoutOf(doc);
    for (const QString &id : ids) {
        const QPointF origin = layout.value(id);
        const Located at = findItem(rack, id);
        if (at.item && !at.item->isSplit()) {
            if (itemCount(rack) >= drift::audiofx::kMaxGraphNodes)
                break;
            RackItem copy = *at.item;
            copy.id = uid(QStringLiteral("p"));
            const QString newId = copy.id;
            at.list->insert(at.list->begin() + at.index + 1, std::move(copy));
            layout[newId] = origin + QPointF(40, 40);
            *created << newId;
            continue;
        }
        for (const Modulator &m : std::as_const(rack.modulators)) {
            if (m.id != id)
                continue;
            Modulator copy = m;
            copy.id = uid(QStringLiteral("m"));
            layout[copy.id] = origin + QPointF(40, 40);
            *created << copy.id;
            rack.modulators.append(copy);
            break;
        }
    }
    return rg::withLayout(withRack(doc, rack), layout);
}

}

AudioForgeController::AudioForgeController(AppController *app, WorkspaceStore *workspaces, QObject *parent)
    : NodeGraphController(parent)
    , m_app(app)
    , m_workspaces(workspaces)
{
    m_undo.setUndoLimit(300);
    connect(&m_undo, &QUndoStack::indexChanged, this, &NodeGraphController::historyChanged);

    m_stageTimer.setSingleShot(true);
    m_stageTimer.setInterval(150);
    connect(&m_stageTimer, &QTimer::timeout, this, &AudioForgeController::stageNow);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(700);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { save(); });
    m_selectionTimer.setSingleShot(true);
    connect(&m_selectionTimer, &QTimer::timeout, this, [this] {
        if (m_follow && m_active)
            useSelection();
    });

    m_doc = seedDoc();
    refresh(true);
    refreshRecent();

    connect(m_workspaces, &WorkspaceStore::activeIdChanged, this,
            [this] { setActive(m_workspaces->activeId() == kWorkspaceId); });
    connect(m_app, &AppController::selectionChanged, this, [this] { m_selectionTimer.start(0); });
    connect(m_app->playback(), &PlaybackEngine::playingChanged, this, [this] {
        if (!m_app->playback()->isPlaying())
            stopLoop();
    });
    if (m_app->addonManager())
        connect(m_app->addonManager(), &AddonManager::kindChanged, this, [this] { m_canEdit.clear(); });
    setActive(m_workspaces->activeId() == kWorkspaceId);
}

AudioForgeController::~AudioForgeController()
{
    if (m_dirty)
        save();
    if (m_active)
        clearPreview();
}

QVariantMap AudioForgeController::meta() const
{
    const ForgeMeta &m = m_doc.meta;
    return {{"id", m.id}, {"displayName", m.displayName}, {"category", m.category},
            {"description", m.description}, {"author", m.author}, {"version", m.version}};
}

QStringList AudioForgeController::categories() const
{
    return kindInfo(Kind::Audio).categories;
}

QVariantList AudioForgeController::params() const
{
    QVariantList out;
    for (const ParamDef &p : m_doc.params) {
        QVariantMap m = paramToJson(p).toVariantMap();
        m["used"] = paramUsers(m_doc, p.identifier);
        out.append(m);
    }
    return out;
}

QVariantMap AudioForgeController::previewValues() const
{
    QVariantMap out;
    for (const ParamDef &p : m_doc.params)
        out[p.identifier] = (m_values.contains(p.identifier) ? m_values.value(p.identifier) : p.defaultValue).toVariant();
    return out;
}

QStringList AudioForgeController::presetNames() const
{
    QStringList out;
    for (const Preset &p : m_doc.presets.value_or(QList<Preset>()))
        out << p.name;
    return out;
}

QList<NodeGraphController::CatalogEntry> AudioForgeController::catalog() const
{
    return rg::catalogEntries();
}

void AudioForgeController::commit(const ForgeDoc &next, const QString &text, const QString &key, bool structural)
{
    m_republish = structural;
    m_undo.push(new DocSnapshotCommand(m_doc, next, text, key, [this](const ForgeDoc &d) { applyDoc(d); }));
}

void AudioForgeController::applyDoc(const ForgeDoc &doc)
{
    m_doc = doc;
    const bool republish = m_republish;
    m_republish = true;
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
    refresh(republish);
    scheduleSave();
}

QString AudioForgeController::burstKey(const QString &base)
{
    if (base != m_lastBurstBase || !m_burstClock.isValid() || m_burstClock.elapsed() > kBurstMs)
        ++m_burst;
    m_lastBurstBase = base;
    m_burstClock.restart();
    return base + QLatin1Char('#') + QString::number(m_burst);
}

void AudioForgeController::refresh(bool republish)
{
    if (republish) {
        for (auto it = m_values.begin(); it != m_values.end();) {
            if (m_doc.param(it.key()))
                ++it;
            else
                it = m_values.erase(it);
        }
        const QStringList problems = validateRack(m_doc);
        const bool changed = problems != m_problems;
        m_problems = problems;
        if (m_problems.isEmpty())
            m_manifest = audioPreviewManifest(m_doc, &m_hoisted);
        if (changed || !m_stageErrors.isEmpty()) {
            m_stageErrors.clear();
            emit validationChanged();
        }
    }
    nodes()->setNodes(rg::nodeItems(m_doc));
    edges()->setEdges(rg::edgeItems(m_doc));
    if (republish) {
        emit metaChanged();
        emit paramsChanged();
        emit previewValuesChanged();
        schedulePreview();
    }
    emit historyChanged();
}

void AudioForgeController::moveNodes(const QStringList &ids, qreal dx, qreal dy)
{
    commit(rg::moveNodes(m_doc, ids, dx, dy), tr("Move nodes"), QStringLiteral("move#%1").arg(m_moveSerial), false);
}

void AudioForgeController::endMove()
{
    ++m_moveSerial;
}

bool AudioForgeController::canConnect(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                      const QString &toPort) const
{
    return rg::canConnect(m_doc, fromNode, fromPort, toNode, toPort);
}

bool AudioForgeController::connectPorts(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                        const QString &toPort)
{
    const OpResult r = rg::connect(m_doc, fromNode, fromPort, toNode, toPort);
    if (!r.ok()) {
        emit message(r.error);
        return false;
    }
    commit(r.doc, tr("Connect"));
    return true;
}

void AudioForgeController::disconnectInput(const QString &node, const QString &port)
{
    const ForgeDoc next = rg::disconnectInput(m_doc, node, port);
    if (rackOf(next).routes.size() == rackOf(m_doc).routes.size()) {
        emit message(tr("Audio wiring follows pedal order. Drag a pedal to a new place or delete it."));
        return;
    }
    commit(next, tr("Disconnect"));
}

void AudioForgeController::deleteSelection()
{
    const ForgeDoc next = rg::deleteNodes(m_doc, selection());
    if (next.toJson() != m_doc.toJson())
        commit(next, tr("Delete nodes"));
}

void AudioForgeController::duplicateSelection()
{
    QStringList created;
    const ForgeDoc next = duplicateBoardNodes(m_doc, selection(), &created);
    if (created.isEmpty())
        return;
    commit(next, tr("Duplicate nodes"));
    setSelection(created);
}

void AudioForgeController::copySelection()
{
    m_clipboard = selection();
}

void AudioForgeController::paste(const QPointF &)
{
    QStringList created;
    const ForgeDoc next = duplicateBoardNodes(m_doc, m_clipboard, &created);
    if (created.isEmpty())
        return;
    commit(next, tr("Paste nodes"));
    setSelection(created);
}

void AudioForgeController::undo()
{
    m_republish = true;
    m_undo.undo();
}

void AudioForgeController::redo()
{
    m_republish = true;
    m_undo.redo();
}

QString AudioForgeController::addNode(const QString &type, const QPointF &at)
{
    const OpResult r = rg::addNode(m_doc, type, at);
    if (!r.ok()) {
        emit message(r.error);
        return {};
    }
    commit(r.doc, tr("Add node"));
    return r.created.value(0);
}

QString AudioForgeController::addNodeConnected(const QString &type, const QPointF &at, const QString &fromNode,
                                               const QString &fromPort, bool fromIsOutput)
{
    const OpResult r = rg::addNodeConnected(m_doc, type, at, fromNode, fromPort, fromIsOutput);
    if (!r.ok()) {
        emit message(r.error);
        return {};
    }
    commit(r.doc, tr("Add node"));
    return r.created.value(0);
}

void AudioForgeController::setInputValue(const QString &node, const QString &input, const QVariant &value,
                                         bool coalesce)
{
    const ForgeDoc next = rg::setInputValue(m_doc, node, input, value);
    if (next.toJson() == m_doc.toJson())
        return;
    const QString key = coalesce ? burstKey(QStringLiteral("val:%1:%2").arg(node, input)) : QString();
    commit(next, tr("Edit value"), key);
}

QVariantList AudioForgeController::nodeContextActions(const QString &id) const
{
    QVariantList out;
    const int row = nodes()->rowOfNode(id);
    if (row < 0)
        return out;
    const auto entry = [&](const QString &action, const QString &label, bool enabled = true) {
        out.append(QVariantMap{{"id", action}, {"label", label}, {"enabled", enabled}});
    };
    const QVariantMap node = nodes()->get(row);
    Rack rack = rackOf(m_doc);
    const RackItem *pedal = pedalAt(rack, id);
    if (pedal)
        entry(QStringLiteral("bypass"), getValue(m_doc, ValuePath::bypassOf(id)).value_or(KnobValue()).truthy()
                                            ? tr("Switch on") : tr("Bypass"));
    for (const QVariant &v : node.value("inputs").toList()) {
        const QVariantMap in = v.toMap();
        const QString inputId = in.value("id").toString();
        if (!rg::valuePathOf(m_doc, id, inputId))
            continue;
        if (in.contains("exposed"))
            entry(QStringLiteral("unexpose:") + inputId, tr("Unexpose %1").arg(in.value("label").toString()));
        else if (!in.value("connected").toBool())
            entry(QStringLiteral("expose:") + inputId, tr("Expose %1 as slider").arg(in.value("label").toString()));
    }
    if (const RackItem *split = splitAt(rack, id)) {
        entry(QStringLiteral("addLane"), tr("Add lane"), int(split->lanes.size()) < drift::audiofx::kMaxSplitLanes);
        entry(QStringLiteral("removeLane"), tr("Remove last lane"), split->lanes.size() > 2);
    }
    if (pedal) {
        const bool modulated = std::any_of(rack.routes.cbegin(), rack.routes.cend(),
                                           [&](const ModRoute &r) { return r.to == id; });
        entry(QStringLiteral("clearMod"), tr("Remove modulation"), modulated);
    }
    return out;
}

void AudioForgeController::triggerNodeAction(const QString &id, const QString &action)
{
    const int colon = action.indexOf(QLatin1Char(':'));
    const QString verb = colon < 0 ? action : action.left(colon);
    const QString arg = colon < 0 ? QString() : action.mid(colon + 1);
    Rack rack = rackOf(m_doc);
    if (verb == QLatin1String("expose"))
        exposeInput(id, arg);
    else if (verb == QLatin1String("unexpose"))
        unexposeInput(id, arg);
    else if (verb == QLatin1String("bypass"))
        setBypass(id, !getValue(m_doc, ValuePath::bypassOf(id)).value_or(KnobValue()).truthy());
    else if (verb == QLatin1String("addLane"))
        addLane(id);
    else if (verb == QLatin1String("removeLane")) {
        if (const RackItem *split = splitAt(rack, id); split && !split->lanes.empty())
            removeLane(id, split->lanes.back().id);
    } else if (verb == QLatin1String("clearMod")) {
        ForgeDoc next = m_doc;
        for (const ModRoute &r : std::as_const(rack.routes))
            if (r.to == id)
                next = drift::forge::removeRoute(next, r.id);
        commit(next, tr("Remove modulation"));
    }
}

QVariantMap AudioForgeController::nodeInfo(const QString &id) const
{
    const int row = nodes()->rowOfNode(id);
    if (row < 0)
        return {};
    const QVariantMap node = nodes()->get(row);
    Rack rack = rackOf(m_doc);
    QVariantList inputs;
    for (const QVariant &v : node.value("inputs").toList()) {
        QVariantMap in = v.toMap();
        in["exposable"] = rg::valuePathOf(m_doc, id, in.value("id").toString()).has_value();
        inputs.append(in);
    }
    QVariantMap out{{"id", id}, {"title", node.value("title")}, {"deletable", node.value("deletable")},
                    {"inputs", inputs}, {"type", node.value("type")}};

    const auto describeRoute = [&](const ModRoute &r, bool towardsTarget) {
        QString label;
        QString knobLabel;
        if (towardsTarget) {
            const int fromRow = nodes()->rowOfNode(r.to);
            label = fromRow >= 0 ? nodes()->get(fromRow).value("title").toString() : r.to;
        } else {
            const int fromRow = nodes()->rowOfNode(r.from);
            label = fromRow >= 0 ? nodes()->get(fromRow).value("title").toString() : r.from;
        }
        if (const RackItem *target = pedalAt(rack, r.to))
            if (const auto *spec = drift::audiofx::pedalSpec(target->type.toStdString()))
                if (const auto *k = drift::audiofx::findKnob(*spec, r.knob.toStdString()))
                    knobLabel = QString::fromLatin1(k->label);
        QVariantMap m{{"id", r.id}, {"label", label}, {"knob", r.knob}, {"knobLabel", knobLabel},
                      {"depth", r.depth.bound() ? (m_doc.param(r.depth.param) ? m_doc.param(r.depth.param)->defaultValue.toDouble() : 0)
                                                : r.depth.number}};
        if (r.depth.bound())
            m["exposed"] = r.depth.param;
        return m;
    };

    if (id == QLatin1String(rg::kInputId)) {
        out["kind"] = "input";
    } else if (id == QLatin1String(rg::kOutputId)) {
        out["kind"] = "output";
    } else if (!rg::splitOfMerge(id).isEmpty()) {
        out["kind"] = "merge";
        out["split"] = rg::splitOfMerge(id);
    } else if (const RackItem *pedal = pedalAt(rack, id)) {
        out["kind"] = "pedal";
        const KnobValue bypass = getValue(m_doc, ValuePath::bypassOf(id)).value_or(KnobValue());
        QVariantMap b{{"value", bypass.bound() ? false : bypass.truthy()}};
        if (bypass.bound())
            b["exposed"] = bypass.param;
        out["bypass"] = b;
        QVariantList routes;
        for (const ModRoute &r : std::as_const(rack.routes))
            if (r.to == id)
                routes.append(describeRoute(r, false));
        out["routes"] = routes;
        out["isConvolution"] = pedal->type == QLatin1String("convolution");
        out["ir"] = pedal->ir;
    } else if (const RackItem *split = splitAt(rack, id)) {
        out["kind"] = "split";
        out["mode"] = split->mode;
        QVariantList lanes;
        for (size_t i = 0; i < split->lanes.size(); ++i)
            lanes.append(QVariantMap{{"id", split->lanes[i].id}, {"label", tr("Lane %1").arg(i + 1)},
                                     {"gain", split->lanes[i].gain}});
        out["lanes"] = lanes;
        out["canAddLane"] = int(split->lanes.size()) < drift::audiofx::kMaxSplitLanes;
        out["canRemoveLane"] = split->lanes.size() > 2;
    } else {
        for (const Modulator &m : std::as_const(rack.modulators)) {
            if (m.id != id)
                continue;
            out["kind"] = "modulator";
            out["modulatorType"] = m.type;
            QVariantList routes;
            for (const ModRoute &r : std::as_const(rack.routes))
                if (r.from == id)
                    routes.append(describeRoute(r, true));
            out["routes"] = routes;
            if (m.steps) {
                QVariantList steps;
                for (double s : *m.steps)
                    steps.append(s);
                out["steps"] = steps;
                out["maxSteps"] = drift::audiofx::kMaxSteps;
            }
        }
    }
    return out;
}

void AudioForgeController::setBypass(const QString &node, bool on)
{
    setInputValue(node, QStringLiteral("bypass"), on, false);
}

void AudioForgeController::exposeInput(const QString &node, const QString &input)
{
    const auto path = rg::valuePathOf(m_doc, node, input);
    if (!path)
        return;
    const OpResult r = exposeValue(m_doc, *path);
    if (!r.ok())
        emit message(r.error);
    else
        commit(r.doc, tr("Expose as slider"));
}

void AudioForgeController::unexposeInput(const QString &node, const QString &input)
{
    const auto path = rg::valuePathOf(m_doc, node, input);
    if (path)
        commit(unexposeValue(m_doc, *path), tr("Unexpose slider"));
}

void AudioForgeController::addLane(const QString &split)
{
    const OpResult r = drift::forge::addLane(m_doc, split);
    if (!r.ok())
        emit message(r.error);
    else
        commit(r.doc, tr("Add lane"));
}

void AudioForgeController::removeLane(const QString &split, const QString &lane)
{
    const OpResult r = drift::forge::removeLane(m_doc, split, lane);
    if (!r.ok())
        emit message(r.error);
    else
        commit(r.doc, tr("Remove lane"));
}

void AudioForgeController::setLaneGain(const QString &split, const QString &lane, double gain, bool coalesce)
{
    const QString key = coalesce ? burstKey(QStringLiteral("gain:%1").arg(lane)) : QString();
    commit(drift::forge::setLaneGain(m_doc, split, lane, gain), tr("Edit lane"), key);
}

void AudioForgeController::setSteps(const QString &modulator, const QVariantList &steps, bool coalesce)
{
    QList<double> values;
    for (const QVariant &v : steps)
        values.append(v.toDouble());
    const QString key = coalesce ? burstKey(QStringLiteral("steps:%1").arg(modulator)) : QString();
    commit(drift::forge::setSteps(m_doc, modulator, values), tr("Edit steps"), key);
}

void AudioForgeController::setRouteDepth(const QString &route, double depth, bool coalesce)
{
    const QString key = coalesce ? burstKey(QStringLiteral("depth:%1").arg(route)) : QString();
    commit(drift::forge::setRouteDepth(m_doc, route, depth), tr("Edit depth"), key);
}

void AudioForgeController::removeRoute(const QString &route)
{
    commit(drift::forge::removeRoute(m_doc, route), tr("Remove modulation"));
}

QString AudioForgeController::importIr(const QString &node, const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return tr("Cannot open %1").arg(path);
    if (file.size() > kMaxIrBytes)
        return tr("That recording is larger than 16 MB.");
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 12 || !bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE")
        return tr("Impulse responses must be WAV files.");
    const QString name = QFileInfo(path).completeBaseName();
    static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9]+"));
    QString slug = name.toLower().replace(unsafe, QStringLiteral("_"));
    slug = slug.left(24);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(6));
    ForgeAsset asset;
    asset.id = QStringLiteral("ir_%1_%2").arg(slug, hash);
    asset.name = name;
    asset.mime = QStringLiteral("audio/wav");
    asset.data = QStringLiteral("data:audio/wav;base64,") + QString::fromLatin1(bytes.toBase64());
    ForgeDoc next = m_doc;
    next.assets.removeIf([&](const ForgeAsset &a) { return a.id == asset.id; });
    next.assets.append(asset);
    next = drift::forge::setIr(next, node, asset.id);
    if (rackOf(next).toJson() == rackOf(m_doc).toJson() && next.assets.size() == m_doc.assets.size())
        return tr("Select a convolution reverb first.");
    commit(next, tr("Import impulse response"));
    return {};
}

void AudioForgeController::updateParam(const QString &id, const QVariantMap &patch, bool coalesce)
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

void AudioForgeController::moveParam(const QString &id, int delta)
{
    commit(drift::forge::moveParam(m_doc, id, delta), tr("Reorder sliders"));
}

void AudioForgeController::removeParam(const QString &id)
{
    commit(drift::forge::removeParam(m_doc, id), tr("Remove slider"));
}

void AudioForgeController::setPreviewValue(const QString &name, const QVariant &value)
{
    m_values.insert(name, QJsonValue::fromVariant(value));
    emit previewValuesChanged();
    applyPatch();
}

void AudioForgeController::resetPreviewValues()
{
    m_values.clear();
    emit previewValuesChanged();
    applyPatch();
}

void AudioForgeController::addPreset(const QString &name)
{
    QJsonObject values;
    const QVariantMap current = previewValues();
    for (auto it = current.begin(); it != current.end(); ++it)
        values.insert(it.key(), QJsonValue::fromVariant(it.value()));
    commit(savePreset(m_doc, name, values), tr("Save preset"));
}

void AudioForgeController::applyPreset(const QString &name)
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

void AudioForgeController::deletePreset(const QString &name)
{
    commit(drift::forge::deletePreset(m_doc, name), tr("Delete preset"));
}

QString AudioForgeController::setMeta(const QString &key, const QVariant &value)
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
    } else {
        return tr("Unknown field.");
    }
    commit(next, tr("Edit details"), burstKey(QStringLiteral("meta:") + key));
    return {};
}

void AudioForgeController::replaceDoc(const ForgeDoc &doc)
{
    m_undo.clear();
    m_values.clear();
    m_clipboard.clear();
    setSelection({});
    m_stagedKey.clear();
    m_doc = doc;
    m_republish = true;
    m_dirty = false;
    emit dirtyChanged();
    if (m_active)
        useSelection();
    refresh(true);
}

void AudioForgeController::newDoc()
{
    if (m_dirty)
        save();
    replaceDoc(seedDoc());
    refreshRecent();
}

void AudioForgeController::activateWorkspace()
{
    m_workspaces->setActiveId(kWorkspaceId);
}

void AudioForgeController::createNew()
{
    newDoc();
    activateWorkspace();
}

bool AudioForgeController::canEdit(const QString &kindName, const QString &id)
{
    const QString name = normalizedKind(kindName);
    if (name != kPackageKind)
        return false;
    const QString key = name + QLatin1Char(':') + id;
    const auto cached = m_canEdit.constFind(key);
    if (cached != m_canEdit.constEnd())
        return *cached;
    bool found = false;
    if (const auto pk = drift::forge::packageKind(name)) {
        const QString dir = drift::forge::packageDirForId(*pk, id);
        found = !dir.isEmpty() && drift::forge::isUserDir(*pk, dir)
                && QFile::exists(QDir(dir).filePath(QStringLiteral("forge.json")));
    }
    m_canEdit.insert(key, found);
    return found;
}

bool AudioForgeController::openPackage(const QString &kindName, const QString &id)
{
    if (!canEdit(kindName, id))
        return false;
    const auto pk = drift::forge::packageKind(kPackageKind);
    QFile file(QDir(drift::forge::packageDirForId(*pk, id)).filePath(QStringLiteral("forge.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QString error;
    ForgeDoc doc = ForgeDoc::fromJsonText(file.readAll(), &error);
    if (!error.isEmpty() || doc.kind != Kind::Audio) {
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

QString AudioForgeController::stageForExport(const PackageContents &contents, const QString &dir) const
{
    QJsonObject trusted;
    for (const PackageFile &f : contents.files)
        trusted.insert(f.path, QString::fromLatin1(f.data.toBase64()));
    const QJsonObject error = drift::forge::stage(*drift::forge::packageKind(kPackageKind), dir, contents.manifest,
                                                  QJsonObject{{"trusted_files", trusted}});
    return error.isEmpty() ? QString() : error.value("detail").toString(error.value("error").toString());
}

bool AudioForgeController::buildPackage(PackageContents *out, QString *error)
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
    if (const QStringList problems = validateRack(m_doc); !problems.isEmpty()) {
        *error = problems.join(QLatin1Char('\n'));
        return false;
    }
    *out = packageContents(m_doc, CompileResult{});
    return true;
}

QVariantMap AudioForgeController::install(bool overwrite)
{
    const auto fail = [](const QString &text, const char *code = "error") {
        return QVariantMap{{"ok", false}, {"error", text}, {"code", QString::fromLatin1(code)}};
    };
    PackageContents contents;
    QString error;
    if (!buildPackage(&contents, &error))
        return fail(error, "invalid");
    const drift::forge::PackageKind pk = *drift::forge::packageKind(kPackageKind);
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

QVariantMap AudioForgeController::exportDriftfx(const QUrl &url)
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
    if (!drift::addon::writeUserPackage(dir, drift::forge::packageKind(kPackageKind)->subdir, meta, path, &error))
        return fail(error);
    return {{"ok", true}, {"path", path}};
}

QVariantMap AudioForgeController::exportFolder(const QUrl &url)
{
    const auto fail = [](const QString &text) { return QVariantMap{{"ok", false}, {"error", text}}; };
    PackageContents contents;
    QString error;
    if (!buildPackage(&contents, &error))
        return fail(error);
    const QString root = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString dir = QDir(root).filePath(m_doc.meta.id);
    const QString manifest = drift::forge::packageKind(kPackageKind)->manifest;
    if (QFileInfo::exists(dir) && !QFile::exists(QDir(dir).filePath(manifest)))
        return fail(tr("%1 already exists and is not a package folder.").arg(dir));
    if (!(error = stageForExport(contents, dir)).isEmpty())
        return fail(error);
    return {{"ok", true}, {"path", dir}};
}

QString AudioForgeController::projectDir(const QString &id) const
{
    return rootDir() + QStringLiteral("/forge-projects/") + id;
}

bool AudioForgeController::open(const QString &pathOrId)
{
    const QString path = QFileInfo(pathOrId).isFile() ? pathOrId : projectDir(pathOrId) + QStringLiteral("/forge.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message(tr("Cannot open %1").arg(path));
        return false;
    }
    QString error;
    const ForgeDoc doc = ForgeDoc::fromJsonText(file.readAll(), &error);
    if (!error.isEmpty() || doc.kind != Kind::Audio) {
        emit message(error.isEmpty() ? tr("That project is not an audio effect.") : error);
        return false;
    }
    if (m_dirty)
        save();
    replaceDoc(doc);
    m_savedId = doc.meta.id;
    return true;
}

void AudioForgeController::scheduleSave()
{
    m_saveTimer.start();
}

bool AudioForgeController::save()
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

void AudioForgeController::refreshRecent()
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
        if (o.value("kind").toString() != QLatin1String("audio"))
            continue;
        const QDateTime modified = QFileInfo(file).lastModified();
        found.append({modified, {{"id", id},
                                  {"name", o.value("meta").toObject().value("displayName").toString(id)},
                                  {"kind", QStringLiteral("audio")},
                                  {"modified", modified.toString(Qt::ISODate)}}});
    }
    std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) { return a.modified > b.modified; });
    m_recent.clear();
    for (const Entry &e : found)
        m_recent.append(e.data);
    emit recentChanged();
}

void AudioForgeController::setActive(bool active)
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

void AudioForgeController::clearPreview()
{
    m_stageTimer.stop();
    stopLoop();
    m_app->playback()->setPreviewPatch({});
    clearDraft(kPackageKind);
    m_stagedKey.clear();
}

// Hidden parameters keep their defaults out of the key: those change with every knob drag and must
// not rebuild the graph.
QString AudioForgeController::structureKey() const
{
    QJsonObject manifest = m_manifest;
    QJsonArray parameters = manifest.value("parameters").toArray();
    for (qsizetype i = 0; i < parameters.size(); ++i) {
        QJsonObject p = parameters.at(i).toObject();
        if (p.value("hidden").toBool()) {
            p.remove("defaultValue");
            parameters[i] = p;
        }
    }
    manifest["parameters"] = parameters;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    for (const ForgeAsset &a : m_doc.assets) {
        hash.addData(a.id.toUtf8());
        hash.addData(a.data.toLatin1());
    }
    return QString::fromLatin1(hash.result().toHex());
}

void AudioForgeController::schedulePreview()
{
    if (!m_active || !m_problems.isEmpty())
        return;
    const QString key = structureKey();
    if (key != m_stagedKey) {
        m_pendingKey = key;
        m_stageTimer.start();
        return;
    }
    m_stageTimer.stop();
    applyPatch();
}

void AudioForgeController::stageNow()
{
    if (!m_active || !m_problems.isEmpty())
        return;
    QMap<QString, QByteArray> files;
    const PackageContents contents = packageContents(m_doc, CompileResult{});
    for (const PackageFile &f : contents.files)
        if (f.path != QLatin1String("forge.json"))
            files.insert(f.path, f.data);
    const DraftResult r = stageDraft(kPackageKind, m_manifest, files);
    m_stageErrors.clear();
    if (r.ok)
        m_stagedKey = m_pendingKey;
    else
        m_stageErrors << r.error;
    emit validationChanged();
    if (r.ok)
        applyPatch();
}

QMap<QString, QVariant> AudioForgeController::patchValues() const
{
    QMap<QString, QVariant> out;
    for (const ParamDef &p : m_doc.params) {
        const QJsonValue v = m_values.contains(p.identifier) ? m_values.value(p.identifier) : p.defaultValue;
        out.insert(p.identifier, asNumber(v));
    }
    for (auto it = m_hoisted.begin(); it != m_hoisted.end(); ++it)
        out.insert(it.key(), it.value().toDouble());
    return out;
}

void AudioForgeController::applyPatch()
{
    if (!m_active || m_stagedKey.isEmpty() || !m_problems.isEmpty())
        return;
    m_app->playback()->setPreviewPatch(hasTarget() ? patchAudioEffect(m_targetClip, patchValues())
                                                   : std::function<void(drift::Project &)>());
}

void AudioForgeController::setTarget(const QString &clipId, drift::TimeUs start, drift::TimeUs end,
                                     const QString &description)
{
    if (m_looping)
        stopLoop();
    m_targetClip = clipId;
    m_targetStart = start;
    m_targetEnd = end;
    m_targetDescription = description;
    emit targetChanged();
    applyPatch();
}

void AudioForgeController::setFollowSelection(bool on)
{
    if (on == m_follow)
        return;
    m_follow = on;
    emit targetChanged();
    if (on && m_active)
        useSelection();
}

void AudioForgeController::useSelection()
{
    const drift::Project *project = m_app->project();
    const int track = m_app->selectedTrack();
    const int index = m_app->selectedClip();
    if (track >= 0 && track < project->tracks().size()) {
        const drift::Track &t = project->tracks().at(track);
        if (index >= 0 && index < t.clips.size()) {
            const drift::Clip &c = t.clips.at(index);
            const bool carriesAudio = c.type == drift::ClipType::Audio
                                      || (c.type == drift::ClipType::Video && !c.suppressEmbeddedAudio);
            if (carriesAudio) {
                setTarget(c.id, c.timelineStart, c.timelineEnd(), tr("Clip: %1").arg(clipLabel(c)));
                return;
            }
        }
    }
    setTarget({}, 0, 0, tr("Select an audio clip"));
}

void AudioForgeController::playLoop()
{
    if (!m_active)
        return;
    if (m_looping) {
        m_app->setPlaying(false);
        return;
    }
    if (!hasTarget() || m_targetEnd <= m_targetStart) {
        emit message(tr("Select an audio clip first."));
        return;
    }
    m_app->playback()->setPreviewRange(m_targetStart, std::min(m_targetEnd, m_targetStart + kLoopSpanUs));
    m_app->setPlayheadSeconds(drift::usToSeconds(m_targetStart));
    m_looping = true;
    emit loopingChanged();
    m_app->setPlaying(true);
}

void AudioForgeController::stopLoop()
{
    if (!m_looping)
        return;
    m_looping = false;
    m_app->playback()->clearPreviewRange();
    emit loopingChanged();
    if (m_app->playing())
        m_app->setPlaying(false);
}
