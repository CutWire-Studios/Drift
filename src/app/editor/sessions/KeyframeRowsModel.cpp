#include "app/editor/sessions/KeyframeRowsModel.h"

#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/KeyframeTimeView.h"

#include <algorithm>

using namespace KeyframeRows;

KeyframeRowsModel::KeyframeRowsModel(QObject *parent) : QAbstractListModel(parent) {}

QObject *KeyframeRowsModel::controller() const
{
    return m_controller;
}

void KeyframeRowsModel::setController(QObject *controller)
{
    KeyframeController *typed = qobject_cast<KeyframeController *>(controller);
    if (m_controller == typed)
        return;
    m_controller = typed;
    m_entriesKey.clear();
    emit controllerChanged();
}

void KeyframeRowsModel::setTimeView(KeyframeTimeView *view)
{
    if (m_timeView == view)
        return;
    m_timeView = view;
    emit timeViewChanged();
}

int KeyframeRowsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant KeyframeRowsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case RowRole:
        return r.toMap();
    case RowKeyRole:
        return r.rowKey();
    default:
        return {};
    }
}

QHash<int, QByteArray> KeyframeRowsModel::roleNames() const
{
    return {{RowRole, "rowData"}, {RowKeyRole, "rowKey"}};
}

QVariantList KeyframeRowsModel::rows() const
{
    if (!m_rowsCacheValid) {
        m_rowsCache.clear();
        m_rowsCache.reserve(m_rows.size());
        for (const Row &r : m_rows)
            m_rowsCache.append(r.toMap());
        m_rowsCacheValid = true;
    }
    return m_rowsCache;
}

void KeyframeRowsModel::setRows(const QList<Row> &next)
{
    const int before = m_rows.size();
    m_rowsCacheValid = false;

    QStringList keys;
    QSet<QString> wanted;
    keys.reserve(next.size());
    for (const Row &r : next) {
        keys.append(r.rowKey());
        wanted.insert(keys.last());
    }

    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (wanted.contains(m_rows.at(i).rowKey()))
            continue;
        beginRemoveRows({}, i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }

    int at = 0;
    bool ordered = true;
    for (int i = 0; i < m_rows.size() && ordered; ++i) {
        const QString key = m_rows.at(i).rowKey();
        while (at < keys.size() && keys.at(at) != key)
            ++at;
        if (at++ >= keys.size())
            ordered = false;
    }
    if (!ordered) {
        beginResetModel();
        m_rows.clear();
        endResetModel();
    }

    for (int i = 0; i < next.size(); ++i) {
        if (i < m_rows.size() && m_rows.at(i).rowKey() == keys.at(i)) {
            m_rows[i] = next.at(i);
        } else {
            beginInsertRows({}, i, i);
            m_rows.insert(i, next.at(i));
            endInsertRows();
        }
    }

    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));
    emit rowsChanged();
    if (m_rows.size() != before)
        emit countChanged();
}

void KeyframeRowsModel::clear()
{
    setRows({});
}

void KeyframeRowsModel::rebuild()
{
    if (!m_controller || m_track < 0 || m_clip < 0) {
        setRows({});
        return;
    }
    const QString key = QStringLiteral("%1:%2:%3").arg(m_track).arg(m_clip).arg(m_revision);
    if (key != m_entriesKey) {
        m_entriesKey = key;
        m_entries = m_controller->animatableProperties(m_track, m_clip);
    }
    Options opts;
    opts.scaleLabel = m_scaleLabel;
    opts.sizeLinked = m_sizeLinked;
    opts.animatedOnly = m_animatedOnly;
    for (auto it = m_collapsed.constBegin(); it != m_collapsed.constEnd(); ++it) {
        if (it.value().toBool())
            opts.collapsed.insert(it.key());
    }
    opts.clipStart = m_timeView ? m_timeView->clipStart() : 0.0;
    const QPointer<KeyframeController> controller = m_controller;
    opts.keysFor = [controller, this](const QString &prop) {
        return controller->keysFor(m_track, m_clip, prop);
    };
    setRows(build(m_entries, opts));
}

QColor KeyframeRowsModel::colorFor(const QString &prop) const
{
    if (m_curveRamp.isEmpty())
        return {};
    if (prop.isEmpty())
        return m_curveRamp.first().value<QColor>();
    const auto own = m_curveColors.constFind(prop);
    if (own != m_curveColors.constEnd())
        return own->value<QColor>();
    quint32 hash = 0;
    for (QChar ch : prop)
        hash = (hash * 31 + ch.unicode()) & 0x7fffffff;
    return m_curveRamp.at(int(hash % quint32(m_curveRamp.size()))).value<QColor>();
}

QVariantMap KeyframeRowsModel::rowAt(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows.at(index).toMap() : QVariantMap();
}

QVariantList KeyframeRowsModel::dots(int index) const
{
    return laneDots(m_rows, index);
}

QVariantList KeyframeRowsModel::refsAt(int index, double seconds, double tolerance) const
{
    if (index < 0 || index >= m_rows.size())
        return {};
    return refsForRowAt(m_rows.at(index), seconds, tolerance);
}

QVariantList KeyframeRowsModel::rowRefs(int index) const
{
    QVariantList out;
    for (const QVariant &d : laneDots(m_rows, index))
        out.append(d.toMap().value(QStringLiteral("refs")).toList());
    return out;
}

QVariantList KeyframeRowsModel::allVisibleKeyRefs() const
{
    QVariantList refs;
    for (const Row &r : m_rows) {
        if (r.isGroup())
            continue;
        for (const QString &track : r.tracks) {
            for (const QVariant &k : r.trackKeys.value(track))
                refs.append(makeRef(track, secondsOf(k)));
        }
    }
    return refs;
}

QList<int> KeyframeRowsModel::indicesOrDataRows(const QVariantList &indices) const
{
    QList<int> out;
    if (indices.isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (!m_rows.at(i).isGroup())
                out.append(i);
        }
        return out;
    }
    for (const QVariant &v : indices) {
        const int i = v.toInt();
        if (i >= 0 && i < m_rows.size())
            out.append(i);
    }
    return out;
}

QList<Row> KeyframeRowsModel::pick(const QVariantList &indices) const
{
    QList<Row> out;
    for (int i : indicesOrDataRows(indices))
        out.append(m_rows.at(i));
    return out;
}

double KeyframeRowsModel::prevKeyTime(const QVariantList &indices, double t) const
{
    return KeyframeRows::prevKeyTime(allKeyTimes(pick(indices)), t);
}

double KeyframeRowsModel::nextKeyTime(const QVariantList &indices, double t) const
{
    return KeyframeRows::nextKeyTime(allKeyTimes(pick(indices)), t);
}

QVariantList KeyframeRowsModel::targetIndices(const QStringList &selectedProps, const QString &focusedProp) const
{
    QVariantList out;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        if (r.isGroup())
            continue;
        const bool hit = !selectedProps.isEmpty()
                             ? std::any_of(selectedProps.cbegin(), selectedProps.cend(),
                                           [&r](const QString &p) { return r.tracks.contains(p); })
                             : r.prop == focusedProp;
        if (hit)
            out.append(i);
    }
    return out;
}

QStringList KeyframeRowsModel::targetTracks(const QVariantList &indices) const
{
    QStringList out;
    if (indices.isEmpty())
        return out;
    for (int i : indicesOrDataRows(indices)) {
        for (const QString &t : m_rows.at(i).tracks) {
            if (!out.contains(t))
                out.append(t);
        }
    }
    return out;
}

bool KeyframeRowsModel::allTargetsKeyed(const QVariantList &indices, double t, double tolerance) const
{
    if (indices.isEmpty())
        return false;
    for (int i : indicesOrDataRows(indices)) {
        if (refsForRowAt(m_rows.at(i), t, tolerance).isEmpty())
            return false;
    }
    return true;
}

bool KeyframeRowsModel::targetsHaveKeys(const QVariantList &indices) const
{
    if (indices.isEmpty())
        return false;
    for (int i : indicesOrDataRows(indices)) {
        if (!m_rows.at(i).keys.isEmpty())
            return true;
    }
    return false;
}

#include "moc_KeyframeRowsModel.cpp"
