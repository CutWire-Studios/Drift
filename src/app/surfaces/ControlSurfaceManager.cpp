#include "app/surfaces/ControlSurfaceManager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

namespace drift {

namespace {

constexpr int kFormat = 1;
const QString kSettingsKey = QStringLiteral("surfaces/bindings");

QString feedbackKey(const QString &device, const QString &control)
{
    return device + QLatin1Char('\n') + control;
}

QString kindName(SurfaceControl::Kind kind)
{
    switch (kind) {
    case SurfaceControl::Button:
        return QStringLiteral("button");
    case SurfaceControl::Absolute:
        return QStringLiteral("absolute");
    case SurfaceControl::Relative:
        return QStringLiteral("relative");
    }
    return {};
}

} // namespace

ControlSurfaceManager::ControlSurfaceManager(SurfaceTargets *targets, QObject *parent)
    : QObject(parent), m_targets(targets)
{
    m_feedbackTimer.setInterval(60);
    connect(&m_feedbackTimer, &QTimer::timeout, this, &ControlSurfaceManager::pushFeedback);
    if (m_targets) {
        m_targets->invalidated = [this] { resetPickup(); };
        m_targets->labelsChanged = [this] { emit stripLabelsChanged(); };
        m_targets->requestAction = [this](const QString &id) { emit actionRequested(id); };
    }
    load();
}

void ControlSurfaceManager::load()
{
    QSettings settings;
    const QByteArray raw = settings.value(kSettingsKey).toByteArray();
    if (raw.isEmpty())
        return;
    m_loading = true;
    importJson(QString::fromUtf8(raw));
    m_loading = false;
}

void ControlSurfaceManager::save() const
{
    if (m_loading)
        return;
    QSettings settings;
    settings.setValue(kSettingsKey, exportJson().toUtf8());
}

void ControlSurfaceManager::changed()
{
    save();
    emit devicesChanged();
    updateFeedbackTimer();
}

QJsonObject ControlSurfaceManager::deviceJson(const QString &id, const DeviceState &state) const
{
    QJsonArray list;
    for (const Binding &b : state.bindings) {
        QJsonObject o{{QStringLiteral("control"), b.control}, {QStringLiteral("target"), b.target}};
        if (!b.label.isEmpty())
            o.insert(QStringLiteral("label"), b.label);
        if (b.invert)
            o.insert(QStringLiteral("invert"), true);
        if (b.sensitivity != 1.0)
            o.insert(QStringLiteral("sensitivity"), b.sensitivity);
        list.append(o);
    }
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), state.info.name},
            {QStringLiteral("bindings"), list}};
}

QString ControlSurfaceManager::exportJson(const QString &deviceId) const
{
    QJsonArray devices;
    for (const QString &id : m_order) {
        if (!deviceId.isEmpty() && id != deviceId)
            continue;
        devices.append(deviceJson(id, m_devices.value(id)));
    }
    const QJsonObject root{{QStringLiteral("driftSurfaceBindings"), kFormat},
                           {QStringLiteral("devices"), devices}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

int ControlSurfaceManager::importJson(const QString &json, const QString &deviceId)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject() || doc.object().value(QStringLiteral("driftSurfaceBindings")).toInt() != kFormat)
        return -1;
    int count = 0;
    bool first = true;
    for (const QJsonValue &dv : doc.object().value(QStringLiteral("devices")).toArray()) {
        const QJsonObject d = dv.toObject();
        QString id = d.value(QStringLiteral("id")).toString();
        if (!deviceId.isEmpty()) {
            if (!first)
                break;
            id = deviceId;
        }
        first = false;
        if (id.isEmpty())
            continue;
        if (!m_devices.contains(id))
            m_order.append(id);
        DeviceState &state = m_devices[id];
        if (state.info.name.isEmpty())
            state.info.name = d.value(QStringLiteral("name")).toString(id);
        state.info.id = id;
        state.bindings.clear();
        m_seen.insert(id);
        for (const QJsonValue &bv : d.value(QStringLiteral("bindings")).toArray()) {
            const QJsonObject o = bv.toObject();
            Binding b;
            b.control = o.value(QStringLiteral("control")).toString();
            b.target = o.value(QStringLiteral("target")).toString();
            if (b.control.isEmpty() || b.target.isEmpty())
                continue;
            b.label = o.value(QStringLiteral("label")).toString();
            b.invert = o.value(QStringLiteral("invert")).toBool();
            b.sensitivity = o.value(QStringLiteral("sensitivity")).toDouble(1.0);
            state.bindings.append(b);
            ++count;
        }
    }
    if (!m_loading)
        changed();
    return count;
}

bool ControlSurfaceManager::exportToFile(const QUrl &file, const QString &deviceId) const
{
    QSaveFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(exportJson(deviceId).toUtf8());
    return out.commit();
}

int ControlSurfaceManager::importFromFile(const QUrl &file, const QString &deviceId)
{
    QFile in(file.toLocalFile());
    if (!in.open(QIODevice::ReadOnly))
        return -1;
    return importJson(QString::fromUtf8(in.readAll()), deviceId);
}

void ControlSurfaceManager::registerDevice(const SurfaceDevice &device)
{
    if (!m_devices.contains(device.id))
        m_order.append(device.id);
    DeviceState &state = m_devices[device.id];
    const QList<SurfaceControl> extra = state.info.controls;
    state.info = device;
    for (const SurfaceControl &c : extra) {
        const bool known = std::any_of(device.controls.begin(), device.controls.end(),
                                       [&](const SurfaceControl &k) { return k.id == c.id; });
        if (!known)
            state.info.controls.append(c);
    }
    state.connected = true;
    if (!m_seen.contains(device.id)) {
        m_seen.insert(device.id);
        for (const SurfaceBindingDef &d : device.defaults) {
            Binding b;
            static_cast<SurfaceBindingDef &>(b) = d;
            state.bindings.append(b);
        }
    }
    for (Binding &b : state.bindings)
        b.pickedUp = false;
    const QString prefix = device.id + QLatin1Char('\n');
    for (auto it = m_sent.begin(); it != m_sent.end();) {
        if (it.key().startsWith(prefix))
            it = m_sent.erase(it);
        else
            ++it;
    }
    changed();
    pushFeedback();
}

void ControlSurfaceManager::unregisterDevice(const QString &deviceId)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return;
    it->connected = false;
    changed();
}

void ControlSurfaceManager::ensureControl(const QString &deviceId, const SurfaceControl &control)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return;
    const bool known = std::any_of(it->info.controls.begin(), it->info.controls.end(),
                                   [&](const SurfaceControl &k) { return k.id == control.id; });
    if (known)
        return;
    it->info.controls.append(control);
    emit devicesChanged();
}

ControlSurfaceManager::Binding *ControlSurfaceManager::find(const QString &deviceId, const QString &controlId)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return nullptr;
    for (Binding &b : it->bindings) {
        if (b.control == controlId)
            return &b;
    }
    return nullptr;
}

void ControlSurfaceManager::button(const QString &deviceId, const QString &controlId, bool down)
{
    input(deviceId, controlId, SurfaceControl::Button, down ? 1.0 : 0.0);
}

void ControlSurfaceManager::absolute(const QString &deviceId, const QString &controlId, double normalized)
{
    input(deviceId, controlId, SurfaceControl::Absolute, std::clamp(normalized, 0.0, 1.0));
}

void ControlSurfaceManager::relative(const QString &deviceId, const QString &controlId, double ticks)
{
    input(deviceId, controlId, SurfaceControl::Relative, ticks);
}

void ControlSurfaceManager::setTouched(const QString &deviceId, const QString &controlId, bool touched)
{
    const QString key = feedbackKey(deviceId, controlId);
    if (touched) {
        m_touched.insert(key);
    } else {
        m_touched.remove(key);
        m_sent.remove(key);
    }
}

void ControlSurfaceManager::input(const QString &deviceId, const QString &controlId,
                                  SurfaceControl::Kind kind, double v)
{
    emit inputSeen(deviceId, controlId, v);

    if (!m_learnTarget.isEmpty()) {
        // A button's release and an encoder's zero tick are not a choice of control.
        if ((kind == SurfaceControl::Button && v <= 0.0) || (kind == SurfaceControl::Relative && v == 0.0))
            return;
        const QString target = m_learnTarget;
        bindInternal(deviceId, controlId, target, m_learnLabel);
        m_learnTarget.clear();
        m_learnLabel.clear();
        emit learnChanged();
        emit learned(deviceId, controlId, target);
        changed();
        return;
    }

    if (Binding *b = find(deviceId, controlId))
        apply(m_devices.value(deviceId).info.motorized, *b, kind, v);
}

void ControlSurfaceManager::trigger(const QString &target)
{
    if (!m_targets->trigger(target))
        emit actionRequested(target);
}

void ControlSurfaceManager::apply(bool motorized, Binding &b, SurfaceControl::Kind kind, double v)
{
    const SurfaceTarget::Kind tk = m_targets->describe(b.target).kind;
    const double sign = b.invert ? -1.0 : 1.0;

    switch (kind) {
    case SurfaceControl::Button:
        if (v <= 0.0)
            return;
        if (tk == SurfaceTarget::Action)
            trigger(b.target);
        else if (tk == SurfaceTarget::Relative)
            m_targets->nudge(b.target, sign * b.sensitivity);
        else
            m_targets->setAbsolute(b.target, b.invert ? 0.0 : 1.0);
        return;
    case SurfaceControl::Absolute: {
        const double n = b.invert ? 1.0 - v : v;
        const double last = b.last;
        b.last = n;
        if (tk == SurfaceTarget::Action) {
            if (n > 0.5 && !(last > 0.5))
                trigger(b.target);
        } else if (tk == SurfaceTarget::Relative) {
            if (!std::isnan(last))
                m_targets->nudge(b.target, (n - last) * 100.0 * b.sensitivity);
        } else {
            const double cur = m_targets->current(b.target);
            if (!motorized && !b.pickedUp && !std::isnan(cur)) {
                const bool crossed = !std::isnan(last) && (last - cur) * (n - cur) <= 0.0;
                if (std::abs(n - cur) > kPickupWindow && !crossed)
                    return;
                b.pickedUp = true;
            }
            m_targets->setAbsolute(b.target, n);
        }
        return;
    }
    case SurfaceControl::Relative:
        if (v == 0.0)
            return;
        if (tk == SurfaceTarget::Action) {
            trigger(b.target);
        } else if (tk == SurfaceTarget::Relative) {
            m_targets->nudge(b.target, v * sign * b.sensitivity);
        } else {
            const double cur = m_targets->current(b.target);
            if (!std::isnan(cur))
                m_targets->setAbsolute(b.target, std::clamp(cur + v * sign * b.sensitivity * 0.01, 0.0, 1.0));
        }
        return;
    }
}

void ControlSurfaceManager::resetPickup()
{
    for (DeviceState &state : m_devices) {
        for (Binding &b : state.bindings) {
            b.pickedUp = false;
            b.last = std::nan("");
        }
    }
    m_sent.clear();
}

void ControlSurfaceManager::bindInternal(const QString &deviceId, const QString &controlId,
                                         const QString &targetId, const QString &label)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return;
    QString text = label;
    if (text.isEmpty())
        text = m_targets->describe(targetId).label;
    for (Binding &b : it->bindings) {
        if (b.control == controlId) {
            b.target = targetId;
            b.label = text;
            b.pickedUp = false;
            b.last = std::nan("");
            return;
        }
    }
    Binding b;
    b.control = controlId;
    b.target = targetId;
    b.label = text;
    it->bindings.append(b);
}

void ControlSurfaceManager::bind(const QString &deviceId, const QString &controlId, const QString &targetId,
                                 const QString &label)
{
    bindInternal(deviceId, controlId, targetId, label);
    changed();
}

void ControlSurfaceManager::removeBinding(const QString &deviceId, const QString &controlId)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return;
    it->bindings.removeIf([&](const Binding &b) { return b.control == controlId; });
    changed();
}

void ControlSurfaceManager::setBindingOption(const QString &deviceId, const QString &controlId,
                                             const QString &key, const QVariant &value)
{
    Binding *b = find(deviceId, controlId);
    if (!b)
        return;
    if (key == QLatin1String("invert"))
        b->invert = value.toBool();
    else if (key == QLatin1String("sensitivity"))
        b->sensitivity = std::clamp(value.toDouble(), 0.01, 100.0);
    changed();
}

void ControlSurfaceManager::clearBindings(const QString &deviceId)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end())
        return;
    it->bindings.clear();
    changed();
}

void ControlSurfaceManager::forgetDevice(const QString &deviceId)
{
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end() || it->connected)
        return;
    m_devices.erase(it);
    m_order.removeAll(deviceId);
    m_seen.remove(deviceId);
    changed();
}

void ControlSurfaceManager::setLearnMode(bool on)
{
    if (m_learnMode == on)
        return;
    m_learnMode = on;
    if (!on) {
        m_learnTarget.clear();
        m_learnLabel.clear();
    }
    emit learnChanged();
}

void ControlSurfaceManager::armLearn(const QString &targetId, const QString &label)
{
    m_learnTarget = targetId;
    m_learnLabel = label;
    emit learnChanged();
}

void ControlSurfaceManager::armLearnProperty(const QString &key, const QString &label, double min, double max)
{
    const QString id = m_targets->idForProperty(key, min, max);
    if (!id.isEmpty())
        armLearn(id, label);
}

void ControlSurfaceManager::cancelLearn()
{
    if (m_learnTarget.isEmpty())
        return;
    m_learnTarget.clear();
    m_learnLabel.clear();
    emit learnChanged();
}

QVariantList ControlSurfaceManager::devices() const
{
    QVariantList out;
    for (const QString &id : m_order) {
        const DeviceState &state = m_devices.value(id);
        QVariantList controls;
        for (const SurfaceControl &c : state.info.controls) {
            controls.append(QVariantMap{{QStringLiteral("id"), c.id},
                                        {QStringLiteral("label"), c.label.isEmpty() ? c.id : c.label},
                                        {QStringLiteral("kind"), kindName(c.kind)}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), id},
                               {QStringLiteral("name"), state.info.name},
                               {QStringLiteral("connected"), state.connected},
                               {QStringLiteral("bindingCount"), state.bindings.size()},
                               {QStringLiteral("controls"), controls}});
    }
    return out;
}

QVariantList ControlSurfaceManager::bindings(const QString &deviceId) const
{
    QVariantList out;
    const auto it = m_devices.constFind(deviceId);
    if (it == m_devices.constEnd())
        return out;
    for (const Binding &b : it->bindings) {
        QString controlLabel = b.control;
        QString kind;
        for (const SurfaceControl &c : it->info.controls) {
            if (c.id == b.control) {
                controlLabel = c.label.isEmpty() ? c.id : c.label;
                kind = kindName(c.kind);
            }
        }
        out.append(QVariantMap{{QStringLiteral("control"), b.control},
                               {QStringLiteral("controlLabel"), controlLabel},
                               {QStringLiteral("controlKind"), kind},
                               {QStringLiteral("target"), b.target},
                               {QStringLiteral("targetLabel"), b.label.isEmpty() ? b.target : b.label},
                               {QStringLiteral("invert"), b.invert},
                               {QStringLiteral("sensitivity"), b.sensitivity}});
    }
    return out;
}

QVariantList ControlSurfaceManager::targets() const
{
    QVariantList out;
    for (const SurfaceTarget &t : m_targets->targets()) {
        out.append(QVariantMap{{QStringLiteral("id"), t.id},
                               {QStringLiteral("label"), t.label},
                               {QStringLiteral("group"), t.group},
                               {QStringLiteral("kind"), t.kind == SurfaceTarget::Action ? QStringLiteral("action")
                                                       : t.kind == SurfaceTarget::Absolute ? QStringLiteral("absolute")
                                                                                          : QStringLiteral("relative")}});
    }
    return out;
}

void ControlSurfaceManager::updateFeedbackTimer()
{
    const bool needed = std::any_of(m_devices.constBegin(), m_devices.constEnd(), [](const DeviceState &s) {
        return s.connected && s.info.feedback && !s.bindings.isEmpty();
    });
    if (needed && !m_feedbackTimer.isActive())
        m_feedbackTimer.start();
    else if (!needed)
        m_feedbackTimer.stop();
}

void ControlSurfaceManager::pushFeedback()
{
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it) {
        if (!it->connected || !it->info.feedback)
            continue;
        for (const Binding &b : it->bindings) {
            const QString key = feedbackKey(it.key(), b.control);
            if (m_touched.contains(key))
                continue;
            double v = m_targets->current(b.target);
            if (std::isnan(v))
                continue;
            if (b.invert)
                v = 1.0 - v;
            const auto sent = m_sent.constFind(key);
            if (sent != m_sent.constEnd() && std::abs(sent.value() - v) < 0.002)
                continue;
            m_sent.insert(key, v);
            emit feedback(it.key(), b.control, v);
        }
    }
}

} // namespace drift
