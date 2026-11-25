#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace drift::projdir {

// The rows of a data file: CSV (first line = column names; quoted fields and "" escapes; cells that look
// like numbers become numbers) or a JSON array of objects. `name` picks the format by its suffix.
// Returns an array of objects; `error` says why it is empty when it is not a valid file.
QJsonArray parseDataRows(const QString &name, const QByteArray &bytes, QString *error = nullptr);

} // namespace drift::projdir
