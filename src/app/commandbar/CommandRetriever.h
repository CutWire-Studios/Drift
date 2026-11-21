#pragma once

#include "app/commandbar/CommandCatalog.h"

#include <QHash>
#include <QStringList>
#include <QVector>

namespace drift::commandbar {

// BM25 (Okapi, k1 1.5, b 0.75, epsilon 0.25) over name x2 + toolbox + description, with a static
// synonym table appended to the query. A port of cactus-research/ft/retriever.py: the fine-tuned
// model was trained on the tool lists this produces, so the ranking has to match it exactly.
class CommandRetriever
{
public:
    explicit CommandRetriever(const QList<CommandOp> &ops);

    // Every op name, best first; ties by name.
    QStringList rank(const QString &query) const;
    QStringList top(const QString &query, int k = 10) const;

    static QStringList tokens(const QString &text);
    static QString expand(const QString &query);

private:
    QStringList m_names;
    QVector<QHash<QString, int>> m_freqs;
    QVector<int> m_lengths;
    QHash<QString, double> m_idf;
    double m_avgdl = 0;
};

} // namespace drift::commandbar
