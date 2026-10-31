#include "app/editor/sessions/KeyframeSelection.h"

#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

KeyframeSelection::KeyframeSelection(QObject *parent) : QObject(parent) {}

QString KeyframeSelection::keyId(const QString &prop, double seconds)
{
    return prop + QLatin1Char('@') + QString::number(qRound64(seconds * 1e6));
}

QList<KeyframeSelection::Ref> KeyframeSelection::parse(const QVariantList &refs)
{
    QList<Ref> out;
    out.reserve(refs.size());
    for (const QVariant &v : refs) {
        const QVariantMap m = v.toMap();
        out.append({m.value(QStringLiteral("prop")).toString(), m.value(QStringLiteral("seconds")).toDouble()});
    }
    return out;
}

void KeyframeSelection::assign(const QList<Ref> &next)
{
    m_refs.clear();
    m_index.clear();
    for (const Ref &r : next) {
        const QString id = keyId(r.prop, r.seconds);
        if (m_index.contains(id))
            continue;
        m_index.insert(id, m_refs.size());
        m_refs.append(r);
    }
    ++m_revision;
    emit revisionChanged();
}

void KeyframeSelection::replace(const QVariantList &refs)
{
    assign(parse(refs));
}

void KeyframeSelection::add(const QVariantList &refs)
{
    QList<Ref> next = m_refs;
    next.append(parse(refs));
    assign(next);
}

void KeyframeSelection::toggle(const QVariantMap &ref)
{
    const QString prop = ref.value(QStringLiteral("prop")).toString();
    const double seconds = ref.value(QStringLiteral("seconds")).toDouble();
    if (!has(prop, seconds)) {
        add({ref});
        return;
    }
    const QString id = keyId(prop, seconds);
    QList<Ref> next;
    for (const Ref &r : std::as_const(m_refs)) {
        if (keyId(r.prop, r.seconds) != id)
            next.append(r);
    }
    assign(next);
}

void KeyframeSelection::clear()
{
    if (m_refs.isEmpty())
        return;
    assign({});
}

void KeyframeSelection::remove(const QVariantList &refs)
{
    QSet<QString> drop;
    for (const Ref &r : parse(refs))
        drop.insert(keyId(r.prop, r.seconds));
    QList<Ref> next;
    for (const Ref &r : std::as_const(m_refs)) {
        if (!drop.contains(keyId(r.prop, r.seconds)))
            next.append(r);
    }
    assign(next);
}

bool KeyframeSelection::hasAll(const QVariantList &refs) const
{
    if (refs.isEmpty())
        return false;
    for (const Ref &r : parse(refs)) {
        if (!has(r.prop, r.seconds))
            return false;
    }
    return true;
}

int KeyframeSelection::press(int modifiers, const QVariantList &hitRefs)
{
    const bool selected = hasAll(hitRefs);
    if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
        if (selected) {
            remove(hitRefs);
            return 0;
        }
        add(hitRefs);
    } else if (modifiers & Qt::ShiftModifier) {
        add(hitRefs);
    } else if (!selected) {
        replace(hitRefs);
    } else {
        return 2;
    }
    return 1;
}

QVariantList KeyframeSelection::refs() const
{
    QVariantList out;
    out.reserve(m_refs.size());
    for (const Ref &r : m_refs)
        out.append(QVariantMap{{QStringLiteral("prop"), r.prop}, {QStringLiteral("seconds"), r.seconds}});
    return out;
}

bool KeyframeSelection::has(const QString &prop, double seconds) const
{
    return m_index.contains(keyId(prop, seconds));
}

QVariant KeyframeSelection::span() const
{
    if (m_refs.isEmpty())
        return {};
    double start = m_refs.first().seconds;
    double end = start;
    for (const Ref &r : m_refs) {
        start = std::min(start, r.seconds);
        end = std::max(end, r.seconds);
    }
    return QVariantMap{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}};
}

QStringList KeyframeSelection::props() const
{
    QStringList out;
    for (const Ref &r : m_refs) {
        if (!out.contains(r.prop))
            out.append(r.prop);
    }
    return out;
}
