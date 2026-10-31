#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace drift::addon {

// The store metadata compiled by drift-addons' packer/catalog.py: categories, shelves and one
// entry per addon (or per platform family via `match`). Pure data, no network — AddonManager
// owns fetching and caching. Unknown fields are ignored; a `schema` above 1 is rejected.
class AddonCatalog
{
public:
    static constexpr int kSchema = 1;

    // What the resolver needs to know about an addon the store offers on this platform.
    struct IndexRow
    {
        QString id;
        QStringList kinds;
    };

    struct Added
    {
        QString id;
        QString forId;
        QString reason;
    };

    struct Missing
    {
        QString forId;
        QString requirement; // an addon id, or a kind no available addon provides
    };

    struct Plan
    {
        // Dependencies before dependents, deduplicated, requested order otherwise kept.
        QStringList ids;
        QList<Added> added;
        QList<Missing> missing;
        // id -> the ids in `ids` it must wait for.
        QHash<QString, QStringList> needs;
    };

    static AddonCatalog parse(const QJsonObject &root);

    bool valid() const { return m_valid; }
    QString revision() const { return m_revision; }
    QJsonArray categories() const { return m_categories; }
    QJsonArray collections() const { return m_collections; }

    // Exact id first, then the first `match` glob that fits. Empty when the catalog has no entry.
    QJsonObject entryFor(const QString &addonId) const;

    // The addon a `requires` item resolves to on this platform: its id, else prefer[platform],
    // else default, else the first row providing the kind. Nullopt when nothing qualifies.
    static std::optional<QString> pickProvider(const QJsonObject &requirement,
                                               const QList<IndexRow> &rows, const QString &platform);

    // Expands `requested` with whatever its catalog entries require. A kind requirement is met by
    // an installed kind or by another addon in the plan; installed ids are never added.
    Plan resolve(const QStringList &requested, const QList<IndexRow> &rows,
                 const QSet<QString> &installedIds, const QSet<QString> &installedKinds,
                 const QString &platform) const;

private:
    bool m_valid = false;
    QString m_revision;
    QJsonArray m_categories;
    QJsonArray m_collections;
    QList<QJsonObject> m_entries;
    QHash<QString, int> m_byId;
    std::vector<std::pair<QRegularExpression, int>> m_globs;
};

} // namespace drift::addon
