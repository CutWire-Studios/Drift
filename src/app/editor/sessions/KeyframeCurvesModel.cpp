#include "app/editor/sessions/KeyframeCurvesModel.h"

#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeRowsModel.h"
#include "app/editor/sessions/KeyframeSelection.h"

#include <algorithm>
#include <cmath>

using namespace KeyframeCurves;

KeyframeCurvesModel::KeyframeCurvesModel(QObject *parent) : QObject(parent) {}

void KeyframeCurvesModel::setRowsModel(KeyframeRowsModel *rows)
{
    if (m_rows == rows)
        return;
    if (m_rows)
        m_rows->disconnect(this);
    m_rows = rows;
    if (m_rows) {
        connect(m_rows, &KeyframeRowsModel::rowsChanged, this, &KeyframeCurvesModel::rebuild);
    }
    rebuild();
    emit rowsModelChanged();
}

void KeyframeCurvesModel::setSelection(KeyframeSelection *selection)
{
    if (m_selection == selection)
        return;
    m_selection = selection;
    emit selectionChanged();
}

void KeyframeCurvesModel::setExternalSeries(const QVariantList &series)
{
    m_external = true;
    m_externalList = series;
    rebuild();
    emit externalSeriesChanged();
}

void KeyframeCurvesModel::setNormalize(bool normalize)
{
    if (m_normalize == normalize)
        return;
    m_normalize = normalize;
    applyRanges();
    emit inputsChanged();
    emit changed();
}

void KeyframeCurvesModel::setCurveShown(const QVariantMap &shown)
{
    if (m_curveShown == shown)
        return;
    m_curveShown = shown;
    rebuild();
    emit inputsChanged();
}

void KeyframeCurvesModel::setFocusedProp(const QString &prop)
{
    if (m_focusedProp == prop)
        return;
    m_focusedProp = prop;
    emit changed();
}

void KeyframeCurvesModel::setFocusedTrack(const QString &track)
{
    if (m_focusedTrack == track)
        return;
    m_focusedTrack = track;
    emit changed();
}

void KeyframeCurvesModel::setGraphMargin(qreal margin)
{
    if (m_graphMargin == margin)
        return;
    m_graphMargin = margin;
    emit layoutChanged();
    emit changed();
}

void KeyframeCurvesModel::setViewHeight(qreal height)
{
    if (m_viewHeight == height)
        return;
    m_viewHeight = height;
    emit layoutChanged();
    emit changed();
}

qreal KeyframeCurvesModel::graphHeight() const
{
    return std::max(1.0, m_viewHeight - 2 * m_graphMargin);
}

void KeyframeCurvesModel::setFrozen(bool frozen)
{
    if (m_frozen == frozen)
        return;
    m_frozen = frozen;
    if (!m_frozen)
        applyRanges();
    emit changed();
}

QVariantList KeyframeCurvesModel::series() const
{
    if (!m_seriesCacheValid) {
        m_seriesCache = toList(m_series);
        m_seriesCacheValid = true;
    }
    return m_seriesCache;
}

int KeyframeCurvesModel::focusedIndex() const
{
    if (m_focusedProp.isEmpty())
        return -1;
    int first = -1;
    for (int i = 0; i < m_series.size(); ++i) {
        if (m_series.at(i).rowProp != m_focusedProp)
            continue;
        if (m_series.at(i).prop == m_focusedTrack)
            return i;
        if (first < 0)
            first = i;
    }
    return first;
}

bool KeyframeCurvesModel::isFocused(int index) const
{
    return focusedIndex() < 0 || (index >= 0 && index < m_series.size() && m_series.at(index).rowProp == m_focusedProp);
}

const Series *KeyframeCurvesModel::focusedSeries() const
{
    const int i = focusedIndex();
    return i >= 0 && i < m_series.size() ? &m_series.at(i) : nullptr;
}

void KeyframeCurvesModel::setShowHandles(bool show)
{
    if (m_showHandles == show)
        return;
    m_showHandles = show;
    emit changed();
}

QVariantList KeyframeCurvesModel::gridTicks() const
{
    const QVariantMap label = labelRange();
    QVariantList out;
    for (const Tick &t : KeyframeCurves::gridTicks({label.value(QStringLiteral("min")).toDouble(),
                                                    label.value(QStringLiteral("max")).toDouble()},
                                                   label.value(QStringLiteral("labelled")).toBool()))
        out.append(QVariantMap{{QStringLiteral("frac"), t.frac}, {QStringLiteral("text"), t.text}});
    return out;
}

QVariantMap KeyframeCurvesModel::labelRange() const
{
    if (!m_normalize)
        return {{QStringLiteral("min"), m_vMin}, {QStringLiteral("max"), m_vMax}, {QStringLiteral("labelled"), true},
                {QStringLiteral("color"), QVariant()}};
    if (const Series *s = focusedSeries())
        return {{QStringLiteral("min"), s->valueMin}, {QStringLiteral("max"), s->valueMax},
                {QStringLiteral("labelled"), true}, {QStringLiteral("color"), s->color}};
    return {{QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}, {QStringLiteral("labelled"), false},
            {QStringLiteral("color"), QVariant()}};
}

void KeyframeCurvesModel::rebuild()
{
    m_base.clear();
    if (m_external) {
        m_base = seriesFromList(m_externalList);
        for (int i = 0; i < m_base.size(); ++i) {
            Series &s = m_base[i];
            s.rowIndex = i;
            s.ownMin = s.valueMin;
            s.ownMax = s.valueMax;
            s.fixedRange = hasFixedRange(s.prop, s.fixedRange);
        }
        applyRanges();
        emit changed();
        return;
    }
    if (m_rows) {
        const QList<KeyframeRows::Row> &rows = m_rows->list();
        for (int i = 0; i < rows.size(); ++i) {
            const KeyframeRows::Row &r = rows.at(i);
            if (r.isGroup() || r.kind == QLatin1String("color") || !m_curveShown.value(r.keyProp).toBool())
                continue;
            const bool stepped = r.kind == QLatin1String("bool") || r.kind == QLatin1String("enum")
                                 || r.kind == QLatin1String("int");
            for (const QString &id : r.tracks) {
                Series s;
                s.prop = id;
                s.rowProp = r.prop;
                s.rowIndex = i;
                s.label = r.tracks.size() > 1 ? r.label + QLatin1Char(' ') + id.mid(id.lastIndexOf(QLatin1Char('.')) + 1)
                                              : r.label;
                s.color = m_rows->colorFor(id);
                s.enabled = r.enabled;
                for (const QVariant &k : r.trackKeys.value(id)) {
                    Point p = pointFromMap(k.toMap());
                    if (stepped)
                        p.hold = true;
                    s.points.append(p);
                }
                s.baseValue = !s.points.isEmpty() ? s.points.first().value
                              : m_rows->keyframes() ? m_rows->keyframes()->baseValue(m_rows->track(), m_rows->clip(), id)
                                                    : 0.0;
                const Range range = valueRange(id, s.points, s.baseValue, r.entry.value(QStringLiteral("hasRange")).toBool(),
                                               r.entry.value(QStringLiteral("min")).toDouble(),
                                               r.entry.value(QStringLiteral("max")).toDouble());
                s.ownMin = s.valueMin = range.min;
                s.ownMax = s.valueMax = range.max;
                s.fixedRange = hasFixedRange(id, r.entry.value(QStringLiteral("hasRange")).toBool());
                m_base.append(s);
            }
        }
    }
    if (m_needsFit && !m_base.isEmpty()) {
        m_needsFit = false;
        fitValues(false);
        return;
    }
    if (m_frozen)
        return;
    applyRanges();
    emit changed();
}

void KeyframeCurvesModel::applyRanges()
{
    if (m_frozen)
        return;
    m_series = m_base;
    for (Series &s : m_series) {
        double lo = m_vMin;
        double hi = m_vMax;
        const auto lock = m_locked ? m_lockedRanges.constFind(s.prop) : m_lockedRanges.constEnd();
        if (lock != m_lockedRanges.constEnd()) {
            lo = lock->min;
            hi = lock->max;
        } else if (m_normalize || m_external) {
            lo = s.ownMin;
            hi = s.ownMax;
        }
        s.valueMin = lo;
        s.valueMax = hi;
    }
    m_seriesCacheValid = false;
}

void KeyframeCurvesModel::setRange(const Range &range)
{
    m_vMin = range.min;
    m_vMax = range.max;
    applyRanges();
    emit changed();
}

void KeyframeCurvesModel::requestFit()
{
    m_needsFit = true;
}

bool KeyframeCurvesModel::selected(const QString &prop, double seconds) const
{
    return m_selection && m_selection->has(prop, seconds);
}

void KeyframeCurvesModel::fitValues(bool selectedOnly)
{
    Range range;
    const bool fitted = fitRange(m_base, [this](const QString &p, double s) { return selected(p, s); }, selectedOnly,
                                 &range);
    if (!fitted) {
        if (selectedOnly)
            fitValues(false);
        return;
    }
    setRange(range);
}

void KeyframeCurvesModel::zoomValuesAt(double y, double factor)
{
    const double h = graphHeight();
    const double t = std::clamp((m_graphMargin + h - y) / h, 0.0, 1.0);
    const double span = m_vMax - m_vMin;
    const double anchor = m_vMin + t * span;
    const double next = std::max(1e-6, span * factor);
    const double lo = anchor - t * next;
    setRange({lo, lo + next});
}

void KeyframeCurvesModel::panValuesBy(double dy)
{
    const double dv = dy / graphHeight() * (m_vMax - m_vMin);
    setRange({m_vMin + dv, m_vMax + dv});
}

void KeyframeCurvesModel::lockRanges()
{
    m_lockedRanges.clear();
    for (const Series &s : std::as_const(m_series))
        m_lockedRanges.insert(s.prop, {s.valueMin, s.valueMax});
    m_locked = true;
}

void KeyframeCurvesModel::unlockRanges()
{
    if (!m_locked)
        return;
    m_locked = false;
    m_lockedRanges.clear();
    applyRanges();
    emit changed();
}

double KeyframeCurvesModel::screenY(double value, int seriesIndex) const
{
    if (seriesIndex < 0 || seriesIndex >= m_series.size())
        return 0;
    return m_graphMargin + yForValue(value, m_series.at(seriesIndex), graphHeight());
}

double KeyframeCurvesModel::valueForY(double y, int seriesIndex) const
{
    if (seriesIndex < 0 || seriesIndex >= m_series.size())
        return 0;
    return KeyframeCurves::valueForY(y, m_series.at(seriesIndex), graphHeight());
}

double KeyframeCurvesModel::valueDelta(const QString &prop, double dy) const
{
    const auto it = m_locked ? m_lockedRanges.constFind(prop) : m_lockedRanges.constEnd();
    if (it == m_lockedRanges.constEnd())
        return 0;
    return -dy / graphHeight() * (it->max - it->min);
}

#include "moc_KeyframeCurvesModel.cpp"
