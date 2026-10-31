#include "app/editor/sessions/KeyframeRows.h"

#include <algorithm>
#include <cmath>

namespace KeyframeRows {

namespace {

bool sameKey(const QString &a, double sa, const QString &b, double sb)
{
    return a == b && qRound64(sa * 1e6) == qRound64(sb * 1e6);
}

QVariantList visibleKeys(const QVariantMap &entry, const QVariantList &keys, double clipStart)
{
    if (!entry.value(QStringLiteral("animated")).toBool()
        && entry.value(QStringLiteral("group")).toString() == QLatin1String("transform") && keys.size() == 1
        && std::abs(secondsOf(keys.first()) - clipStart) < kTimeEps)
        return {};
    return keys;
}

Row propRow(const QVariantMap &e)
{
    Row r;
    r.type = QStringLiteral("prop");
    r.entry = e;
    r.group = e.value(QStringLiteral("group")).toString();
    r.label = e.value(QStringLiteral("label")).toString();
    r.prop = e.value(QStringLiteral("prop")).toString();
    r.keyProp = e.value(QStringLiteral("keyProp")).toString();
    r.kind = e.value(QStringLiteral("kind")).toString();
    r.tracks = e.value(QStringLiteral("tracks")).toStringList();
    r.animated = e.value(QStringLiteral("animated")).toBool();
    r.enabled = e.value(QStringLiteral("enabled")).toBool();
    return r;
}

void fillKeys(Row &row, const Options &opts)
{
    QList<QVariantList> lists;
    const QVariantMap *source = &row.entry;
    for (const QString &track : std::as_const(row.tracks)) {
        QVariantList own = visibleKeys(*source, opts.keysFor ? opts.keysFor(track) : QVariantList(),
                                       opts.clipStart);
        row.trackKeys.insert(track, own);
        lists.append(own);
    }
    row.keys = unionKeys(lists);
}

} // namespace

double secondsOf(const QVariant &key)
{
    return key.toMap().value(QStringLiteral("seconds")).toDouble();
}

QVariantMap makeRef(const QString &prop, double seconds)
{
    return {{QStringLiteral("prop"), prop}, {QStringLiteral("seconds"), seconds}};
}

QString Row::rowKey() const
{
    return isGroup() ? QStringLiteral("g:") + group : QStringLiteral("p:") + group + QLatin1Char(':') + prop;
}

QVariantMap Row::toMap() const
{
    QVariantMap m = entry;
    m.insert(QStringLiteral("type"), type);
    m.insert(QStringLiteral("group"), group);
    m.insert(QStringLiteral("label"), label);
    if (isGroup()) {
        QVariantList kids;
        for (const Row &c : children)
            kids.append(c.toMap());
        m.insert(QStringLiteral("count"), count);
        m.insert(QStringLiteral("collapsed"), collapsed);
        m.insert(QStringLiteral("children"), kids);
        return m;
    }
    if (type == QLatin1String("scale")) {
        m.insert(QStringLiteral("kind"), kind);
        m.insert(QStringLiteral("prop"), prop);
        m.insert(QStringLiteral("tracks"), tracks);
        m.insert(QStringLiteral("keyProp"), keyProp);
        m.insert(QStringLiteral("animated"), animated);
        m.insert(QStringLiteral("enabled"), enabled);
        m.insert(QStringLiteral("entries"), entries);
    }
    QVariantMap tk;
    for (auto it = trackKeys.constBegin(); it != trackKeys.constEnd(); ++it)
        tk.insert(it.key(), it.value());
    m.insert(QStringLiteral("trackKeys"), tk);
    m.insert(QStringLiteral("keys"), keys);
    return m;
}

QVariantList unionKeys(const QList<QVariantList> &lists)
{
    QVariantList all;
    for (const QVariantList &l : lists)
        all.append(l);
    std::stable_sort(all.begin(), all.end(),
                     [](const QVariant &a, const QVariant &b) { return secondsOf(a) < secondsOf(b); });
    QVariantList out;
    for (const QVariant &k : std::as_const(all)) {
        if (out.isEmpty() || secondsOf(k) - secondsOf(out.last()) > kTimeEps)
            out.append(k);
    }
    return out;
}

QList<Row> build(const QVariantList &entries, const Options &opts)
{
    struct Group
    {
        QString group;
        QString label;
        QList<QVariantMap> items;
    };
    QList<Group> groups;
    QHash<QString, int> byGroup;
    for (const QVariant &v : entries) {
        const QVariantMap e = v.toMap();
        const QString g = e.value(QStringLiteral("group")).toString();
        if (!byGroup.contains(g)) {
            byGroup.insert(g, groups.size());
            groups.append({g, e.value(QStringLiteral("groupLabel")).toString(), {}});
        }
        groups[byGroup.value(g)].items.append(e);
    }

    const auto findProp = [](const QList<QVariantMap> &items, const QString &prop) -> const QVariantMap * {
        for (const QVariantMap &i : items) {
            if (i.value(QStringLiteral("prop")).toString() == prop)
                return &i;
        }
        return nullptr;
    };

    QList<Row> out;
    for (const Group &grp : std::as_const(groups)) {
        const bool isCollapsed = opts.collapsed.contains(grp.group);
        QList<Row> items;
        bool scaleInserted = false;
        const QVariantMap *w = findProp(grp.items, QStringLiteral("width"));
        const QVariantMap *h = findProp(grp.items, QStringLiteral("height"));
        const bool hasSize = opts.sizeLinked && grp.group == QLatin1String("transform") && w && h;
        for (const QVariantMap &e : grp.items) {
            const QString prop = e.value(QStringLiteral("prop")).toString();
            if (hasSize && (prop == QLatin1String("width") || prop == QLatin1String("height"))) {
                if (scaleInserted)
                    continue;
                scaleInserted = true;
                const bool animated = w->value(QStringLiteral("animated")).toBool()
                                      || h->value(QStringLiteral("animated")).toBool();
                if (opts.animatedOnly && !animated)
                    continue;
                Row r;
                r.type = QStringLiteral("scale");
                r.label = opts.scaleLabel.isEmpty() ? QStringLiteral("Scale") : opts.scaleLabel;
                r.group = grp.group;
                r.kind = QStringLiteral("scalar");
                r.prop = QStringLiteral("scale");
                r.tracks = {QStringLiteral("width"), QStringLiteral("height")};
                r.keyProp = QStringLiteral("width");
                r.animated = animated;
                r.enabled = w->value(QStringLiteral("enabled")).toBool()
                            && h->value(QStringLiteral("enabled")).toBool();
                r.entries = {*w, *h};
                r.entry = {{QStringLiteral("group"), grp.group},
                           {QStringLiteral("groupLabel"), grp.label},
                           {QStringLiteral("animated"), animated}};
                items.append(r);
                continue;
            }
            if (opts.animatedOnly && !e.value(QStringLiteral("animated")).toBool())
                continue;
            items.append(propRow(e));
        }
        if (items.isEmpty())
            continue;

        int count = 0;
        for (Row &item : items) {
            fillKeys(item, opts);
            count += item.keys.size();
        }
        Row g;
        g.type = QStringLiteral("group");
        g.group = grp.group;
        g.label = grp.label;
        g.count = count;
        g.collapsed = isCollapsed;
        g.children = items;
        out.append(g);
        if (!isCollapsed)
            out.append(items);
    }
    return out;
}

QVariantList refsForRowAt(const Row &row, double seconds, double tolerance)
{
    QVariantList refs;
    const QStringList tracks = row.tracks.isEmpty() ? QStringList{row.prop} : row.tracks;
    for (const QString &track : tracks) {
        const auto it = row.trackKeys.constFind(track);
        if (it == row.trackKeys.constEnd())
            continue;
        int best = -1;
        double bestDist = tolerance;
        for (int j = 0; j < it->size(); ++j) {
            const double d = std::abs(secondsOf(it->at(j)) - seconds);
            if (d <= bestDist) {
                bestDist = d;
                best = j;
            }
        }
        if (best >= 0)
            refs.append(makeRef(track, secondsOf(it->at(best))));
    }
    return refs;
}

QVariantList laneDots(const QList<Row> &rows, int index)
{
    if (index < 0 || index >= rows.size())
        return {};
    const Row &row = rows.at(index);
    QVariantList out;
    const auto dot = [](double seconds, const QVariantList &refs, bool hold) {
        return QVariantMap{{QStringLiteral("seconds"), seconds},
                           {QStringLiteral("refs"), refs},
                           {QStringLiteral("hold"), hold}};
    };
    if (!row.isGroup()) {
        for (const QVariant &k : row.keys) {
            const double s = secondsOf(k);
            out.append(dot(s, refsForRowAt(row, s), k.toMap().value(QStringLiteral("hold")).toBool()));
        }
        return out;
    }
    struct Item
    {
        double seconds;
        QVariantList refs;
    };
    QList<Item> all;
    for (const Row &child : row.children) {
        for (const QVariant &k : child.keys) {
            const double s = secondsOf(k);
            all.append({s, refsForRowAt(child, s)});
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const Item &a, const Item &b) { return a.seconds < b.seconds; });
    double lastSeconds = 0;
    QVariantList lastRefs;
    bool open = false;
    const auto flush = [&]() {
        if (open)
            out.append(dot(lastSeconds, lastRefs, false));
    };
    for (const Item &it : all) {
        if (open && it.seconds - lastSeconds <= kTimeEps) {
            lastRefs.append(it.refs);
        } else {
            flush();
            lastSeconds = it.seconds;
            lastRefs = it.refs;
            open = true;
        }
    }
    flush();
    return out;
}

QList<double> allKeyTimes(const QList<Row> &rows)
{
    QList<double> times;
    for (const Row &r : rows) {
        if (r.isGroup()) {
            if (!r.collapsed)
                continue;
            for (const Row &c : r.children) {
                for (const QVariant &k : c.keys)
                    times.append(secondsOf(k));
            }
        } else {
            for (const QVariant &k : r.keys)
                times.append(secondsOf(k));
        }
    }
    return times;
}

double prevKeyTime(const QList<double> &times, double t)
{
    double best = -1;
    for (double s : times) {
        if (s < t - kTimeEps && (best < 0 || s > best))
            best = s;
    }
    return best;
}

double nextKeyTime(const QList<double> &times, double t)
{
    double best = -1;
    for (double s : times) {
        if (s > t + kTimeEps && (best < 0 || s < best))
            best = s;
    }
    return best;
}

QVariantMap hitTest(const QList<Row> &rows, const Axis &axis, double x, double y, double contentY,
                    double rowHeight, double slop)
{
    const int idx = int(std::floor((y + contentY) / rowHeight));
    if (idx < 0 || idx >= rows.size())
        return {{QStringLiteral("row"), -1}};
    const QVariantList dots = laneDots(rows, idx);
    QVariantMap best;
    double bestDist = slop + 0.5;
    for (const QVariant &d : dots) {
        const QVariantMap m = d.toMap();
        const double dist = std::abs(axis.xFor(m.value(QStringLiteral("seconds")).toDouble()) - x);
        if (dist < bestDist) {
            bestDist = dist;
            best = m;
        }
    }
    QVariantMap out;
    out.insert(QStringLiteral("row"), idx);
    if (!best.isEmpty()) {
        out.insert(QStringLiteral("dot"), best);
        out.insert(QStringLiteral("refs"), best.value(QStringLiteral("refs")));
        out.insert(QStringLiteral("seconds"), best.value(QStringLiteral("seconds")));
    } else {
        out.insert(QStringLiteral("dot"), QVariant());
        out.insert(QStringLiteral("refs"), QVariantList());
        out.insert(QStringLiteral("seconds"), axis.secondsFor(x));
    }
    return out;
}

QVariantList marqueeRefs(const QList<Row> &rows, const Axis &axis, double x0, double x1, double y0,
                         double y1, double contentY, double rowHeight, const QVariantList &base)
{
    const double s0 = axis.secondsFor(x0);
    const double s1 = axis.secondsFor(x1);
    const int first = int(std::floor((y0 + contentY) / rowHeight));
    const int last = int(std::floor((y1 + contentY) / rowHeight));
    QVariantList refs = base;
    for (int r = std::max(0, first); r <= last && r < rows.size(); ++r) {
        for (const QVariant &d : laneDots(rows, r)) {
            const QVariantMap m = d.toMap();
            const double s = m.value(QStringLiteral("seconds")).toDouble();
            if (s >= s0 && s <= s1)
                refs.append(m.value(QStringLiteral("refs")).toList());
        }
    }
    return refs;
}

} // namespace KeyframeRows
