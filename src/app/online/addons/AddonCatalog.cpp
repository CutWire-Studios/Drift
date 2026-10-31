#include "app/online/addons/AddonCatalog.h"

#include <algorithm>

namespace drift::addon {

AddonCatalog AddonCatalog::parse(const QJsonObject &root)
{
    AddonCatalog catalog;
    if (root.value(QStringLiteral("schema")).toInt() != kSchema
        || !root.value(QStringLiteral("entries")).isArray())
        return catalog;

    catalog.m_valid = true;
    catalog.m_revision = root.value(QStringLiteral("revision")).toString();
    catalog.m_categories = root.value(QStringLiteral("categories")).toArray();
    catalog.m_collections = root.value(QStringLiteral("collections")).toArray();
    for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString match = entry.value(QStringLiteral("match")).toString();
        if (id.isEmpty() && match.isEmpty())
            continue;
        const int index = int(catalog.m_entries.size());
        catalog.m_entries.append(entry);
        if (!id.isEmpty())
            catalog.m_byId.insert(id, index);
        if (!match.isEmpty())
            catalog.m_globs.emplace_back(
                QRegularExpression(QRegularExpression::anchoredPattern(
                    QRegularExpression::wildcardToRegularExpression(match))),
                index);
    }
    return catalog;
}

QJsonObject AddonCatalog::entryFor(const QString &addonId) const
{
    const auto exact = m_byId.constFind(addonId);
    if (exact != m_byId.constEnd())
        return m_entries.at(exact.value());
    for (const auto &[pattern, index] : m_globs) {
        if (pattern.match(addonId).hasMatch())
            return m_entries.at(index);
    }
    return {};
}

std::optional<QString> AddonCatalog::pickProvider(const QJsonObject &requirement,
                                                  const QList<IndexRow> &rows, const QString &platform)
{
    const auto available = [&](const QString &id) {
        return !id.isEmpty()
               && std::any_of(rows.cbegin(), rows.cend(), [&](const IndexRow &row) { return row.id == id; });
    };

    const QString id = requirement.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        return available(id) ? std::optional<QString>(id) : std::nullopt;

    const QString preferred =
        requirement.value(QStringLiteral("prefer")).toObject().value(platform).toString();
    if (available(preferred))
        return preferred;
    const QString fallback = requirement.value(QStringLiteral("default")).toString();
    if (available(fallback))
        return fallback;
    const QString kind = requirement.value(QStringLiteral("kind")).toString();
    for (const IndexRow &row : rows) {
        if (row.kinds.contains(kind))
            return row.id;
    }
    return std::nullopt;
}

AddonCatalog::Plan AddonCatalog::resolve(const QStringList &requested, const QList<IndexRow> &rows,
                                         const QSet<QString> &installedIds,
                                         const QSet<QString> &installedKinds,
                                         const QString &platform) const
{
    Plan plan;
    QSet<QString> done;
    QSet<QString> visiting;
    const QSet<QString> requestedSet(requested.cbegin(), requested.cend());

    const auto kindsOf = [&](const QString &id) {
        for (const IndexRow &row : rows) {
            if (row.id == id)
                return row.kinds;
        }
        return QStringList();
    };

    const auto visit = [&](auto &&self, const QString &id) -> void {
        if (done.contains(id) || visiting.contains(id))
            return;
        visiting.insert(id);

        const QJsonObject entry = entryFor(id);
        for (const QJsonValue &value : entry.value(QStringLiteral("requires")).toArray()) {
            const QJsonObject requirement = value.toObject();
            const QString reason = requirement.value(QStringLiteral("reason")).toString();
            const QString wantedId = requirement.value(QStringLiteral("id")).toString();
            const QString kind = requirement.value(QStringLiteral("kind")).toString();

            QString dependency;
            if (!wantedId.isEmpty()) {
                if (installedIds.contains(wantedId))
                    continue;
                dependency = wantedId;
            } else {
                if (installedKinds.contains(kind))
                    continue;
                // Something already planned, or about to be (a later requested id), provides it.
                for (const QString &candidate : plan.ids + requested) {
                    if (candidate != id && kindsOf(candidate).contains(kind)) {
                        dependency = candidate;
                        break;
                    }
                }
            }

            const bool planned = !dependency.isEmpty() && (done.contains(dependency)
                                                           || requestedSet.contains(dependency));
            if (!planned) {
                const auto picked = pickProvider(requirement, rows, platform);
                if (!picked) {
                    plan.missing.append({id, wantedId.isEmpty() ? kind : wantedId});
                    continue;
                }
                dependency = *picked;
                if (installedIds.contains(dependency))
                    continue;
            }

            self(self, dependency);
            if (!done.contains(dependency))
                continue; // a cycle back to an addon still being visited
            if (!plan.needs[id].contains(dependency))
                plan.needs[id].append(dependency);
            if (!requestedSet.contains(dependency)) {
                const bool known = std::any_of(plan.added.cbegin(), plan.added.cend(),
                                               [&](const Added &a) { return a.id == dependency; });
                if (!known)
                    plan.added.append({dependency, id, reason});
            }
        }

        visiting.remove(id);
        done.insert(id);
        plan.ids.append(id);
    };

    for (const QString &id : requested)
        visit(visit, id);
    return plan;
}

} // namespace drift::addon
