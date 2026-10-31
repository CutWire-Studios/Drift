#include "app/editor/sessions/MaskStackModel.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/jobs/vision/TrackingController.h"

#include <cmath>

namespace {

constexpr double kKeyEpsilon = 0.001;

constexpr int FirstRole = Qt::UserRole + 1;
constexpr int KeyTimesRole = Qt::UserRole + 100;

const QList<QByteArray> kKeys = {"track",     "clip",     "id",           "name",         "shape",
                                 "op",        "invert",   "enabled",      "feather",      "mediaChannel",
                                 "isMedia",   "mediaSource", "pinned",    "pathAnimated", "pathKeyCount",
                                 "selected",  "trackerId", "trackState", "solve", "trackerPoints"};

} // namespace

MaskStackModel::MaskStackModel(AppController *app, MaskingController *owner)
    : QAbstractListModel(owner)
    , m_app(app)
    , m_owner(owner)
{
    connect(m_app, &AppController::playheadSecondsChanged, this, &MaskStackModel::updateShapeKeyState);
}

int MaskStackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> MaskStackModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < kKeys.size(); ++i)
        names.insert(FirstRole + i, kKeys.at(i));
    names.insert(KeyTimesRole, "shapeKeyTimes");
    return names;
}

QVariant MaskStackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Entry &entry = m_rows.at(index.row());
    if (role == KeyTimesRole)
        return entry.keyTimes;
    const int k = role - FirstRole;
    if (k < 0 || k >= kKeys.size())
        return {};
    return entry.map.value(QString::fromLatin1(kKeys.at(k)));
}

QVariantMap MaskStackModel::rowAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).map : QVariantMap{};
}

void MaskStackModel::refresh()
{
    const QVariantMap target = m_owner->maskTarget();
    QVariantList stack;
    if (target.value(QStringLiteral("kind")).toString() != QLatin1String("none"))
        stack = m_owner->maskStack(target.value(QStringLiteral("track")).toInt(),
                                   target.value(QStringLiteral("clip")).toInt());

    QList<Entry> next;
    bool hasEnabled = false;
    QString refineId;
    QString fixId;
    QString fixName;
    QVariantList keyTimes;
    for (const QVariant &v : stack) {
        Entry entry;
        entry.map = v.toMap();
        const QVariantMap tracked = m_app->tracking()->maskTrackInfo(entry.map.value(QStringLiteral("id")).toString());
        for (auto it = tracked.constBegin(); it != tracked.constEnd(); ++it)
            entry.map.insert(it.key(), it.value());
        const bool selected = entry.map.value(QStringLiteral("selected")).toBool();
        if (selected && entry.map.value(QStringLiteral("shape")).toString() == QLatin1String("freeform"))
            entry.keyTimes = m_owner->maskPathKeyTimes(entry.map.value(QStringLiteral("track")).toInt(),
                                                       entry.map.value(QStringLiteral("clip")).toInt());
        if (selected && refineId.isEmpty()
            && entry.map.value(QStringLiteral("mediaSource")).toString() == QLatin1String("object"))
            refineId = entry.map.value(QStringLiteral("id")).toString();
        const QString source = entry.map.value(QStringLiteral("mediaSource")).toString();
        if (selected && fixId.isEmpty() && (source == QLatin1String("object") || source == QLatin1String("people"))) {
            fixId = entry.map.value(QStringLiteral("id")).toString();
            fixName = entry.map.value(QStringLiteral("name")).toString();
        }
        if (selected && keyTimes.isEmpty())
            keyTimes = entry.keyTimes;
        hasEnabled = hasEnabled || entry.map.value(QStringLiteral("enabled")).toBool();
        next.append(entry);
    }

    bool sameIds = next.size() == m_rows.size();
    for (int i = 0; sameIds && i < next.size(); ++i)
        sameIds = next.at(i).map.value(QStringLiteral("id")) == m_rows.at(i).map.value(QStringLiteral("id"));

    if (!sameIds) {
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).map == m_rows.at(i).map && next.at(i).keyTimes == m_rows.at(i).keyTimes)
                continue;
            m_rows[i] = next.at(i);
            emit dataChanged(index(i), index(i));
        }
    }

    m_hasEnabled = hasEnabled;
    m_refineMaskId = refineId;
    m_cutoutFixId = fixId;
    m_cutoutFixName = fixName;
    m_shapeKeyTimes = keyTimes;
    ++m_revision;
    emit summaryChanged();
    updateShapeKeyState();
}

void MaskStackModel::updateShapeKeyState()
{
    const double t = m_app->playheadSeconds();
    const double half = 0.5 / std::max(1, int(m_app->projectFile()->projectFps()));
    bool earlier = false;
    bool later = false;
    bool on = false;
    for (const QVariant &v : m_shapeKeyTimes) {
        const double k = v.toDouble();
        earlier = earlier || k < t - kKeyEpsilon;
        later = later || k > t + kKeyEpsilon;
        on = on || std::abs(k - t) <= half;
    }
    if (earlier == m_earlier && later == m_later && on == m_onKey)
        return;
    m_earlier = earlier;
    m_later = later;
    m_onKey = on;
    emit shapeKeyStateChanged();
}

void MaskStackModel::stepShapeKey(bool previous)
{
    const double t = m_app->playheadSeconds();
    double best = 0.0;
    bool found = false;
    for (const QVariant &v : m_shapeKeyTimes) {
        const double k = v.toDouble();
        if (previous && k < t - kKeyEpsilon && (!found || k > best)) {
            best = k;
            found = true;
        } else if (!previous && k > t + kKeyEpsilon && (!found || k < best)) {
            best = k;
            found = true;
        }
    }
    if (found)
        m_app->setPlayheadSeconds(best);
}

void MaskStackModel::toggleShapeKey(const QString &maskId)
{
    m_owner->toggleMaskPathKeyAtPlayhead(maskId);
}
