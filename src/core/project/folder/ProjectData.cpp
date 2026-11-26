#include "core/project/folder/ProjectData.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace drift::projdir {

namespace {

QList<QStringList> csvCells(const QString &text)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    bool any = false;
    const auto endCell = [&] {
        row.append(cell);
        cell.clear();
        any = true;
    };
    const auto endRow = [&] {
        if (any || !cell.isEmpty()) {
            endCell();
            if (!(row.size() == 1 && row.first().isEmpty()))
                rows.append(row);
        }
        row.clear();
        any = false;
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
            any = true;
        } else if (c == QLatin1Char(',')) {
            endCell();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            endRow();
        } else {
            cell += c;
        }
    }
    endRow();
    return rows;
}

QJsonValue cellValue(const QString &cell)
{
    static const QRegularExpression number(QStringLiteral("^-?(0|[1-9]\\d*)(\\.\\d+)?$"));
    if (number.match(cell).hasMatch())
        return cell.toDouble();
    return cell;
}

} // namespace

QJsonArray parseDataRows(const QString &name, const QByteArray &bytes, QString *error)
{
    QJsonArray out;
    if (name.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (error)
                *error = parseError.errorString();
            return out;
        }
        if (!doc.isArray()) {
            if (error)
                *error = QStringLiteral("expected a JSON array of objects");
            return out;
        }
        return doc.array();
    }
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const QList<QStringList> rows = csvCells(text);
    if (rows.isEmpty())
        return out;
    const QStringList header = rows.first();
    for (int r = 1; r < rows.size(); ++r) {
        QJsonObject object;
        for (int c = 0; c < header.size(); ++c)
            object.insert(header.at(c).trimmed(), c < rows.at(r).size() ? cellValue(rows.at(r).at(c)) : QJsonValue(QString()));
        out.append(object);
    }
    return out;
}

} // namespace drift::projdir
