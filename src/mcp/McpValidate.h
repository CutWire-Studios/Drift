#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace drift::mcp {

// Checks args against an op's inputSchema: required keys, JSON types (numeric strings pass for
// number/integer, matching what the handlers already coerce), enums (case-insensitive, and the
// value is rewritten to the canonical spelling), and declared minimum/maximum. Returns an empty
// object when valid, else an err(). Keys the schema does not know are not errors; they are
// appended to *ignored so the caller can echo them.
QJsonObject validateArgs(const QString &op, const QJsonObject &schema, QJsonObject &args,
                         QStringList *ignored);

} // namespace drift::mcp
