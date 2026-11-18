#pragma once

#include <QList>
#include <QModelIndex>
#include <QSet>
#include <QString>

// Row-wise diff for the id-keyed graph list models: rows that vanish are removed, new ones are
// inserted, reordered ones are moved, and a row whose content changed gets a dataChanged for just
// the roles that differ. A pure position or value change therefore never resets the model, so the
// delegates a view built for it survive a drag.
//
// Derived provides idOf(T), changedRoles(old, new) and a friend declaration for this class.
template<class Derived, class T>
class ListDiff
{
protected:
    void applyDiff(QList<T> &cur, const QList<T> &next)
    {
        auto *d = static_cast<Derived *>(this);
        QSet<QString> keep;
        for (const T &n : next)
            keep.insert(d->idOf(n));

        for (int i = cur.size() - 1; i >= 0; --i) {
            if (keep.contains(d->idOf(cur.at(i))))
                continue;
            d->beginRemoveRows(QModelIndex(), i, i);
            cur.removeAt(i);
            d->endRemoveRows();
        }

        for (int i = 0; i < next.size(); ++i) {
            const T &n = next.at(i);
            const QString id = d->idOf(n);
            if (i < cur.size() && d->idOf(cur.at(i)) == id) {
                update(cur, i, n);
                continue;
            }
            int from = -1;
            for (int j = i + 1; j < cur.size(); ++j) {
                if (d->idOf(cur.at(j)) == id) {
                    from = j;
                    break;
                }
            }
            if (from >= 0) {
                d->beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
                cur.move(from, i);
                d->endMoveRows();
                update(cur, i, n);
            } else {
                d->beginInsertRows(QModelIndex(), i, i);
                cur.insert(i, n);
                d->endInsertRows();
            }
        }
    }

private:
    void update(QList<T> &cur, int row, const T &n)
    {
        auto *d = static_cast<Derived *>(this);
        const QList<int> roles = d->changedRoles(cur.at(row), n);
        if (roles.isEmpty())
            return;
        cur[row] = n;
        const QModelIndex idx = d->index(row);
        emit d->dataChanged(idx, idx, roles);
    }
};
