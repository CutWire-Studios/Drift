#pragma once

#include <QString>
#include <QStringList>

namespace drift::forge {

// Why `name` can't be a Drift parameter identifier; a null QString if it can.
QString paramNameProblem(const QString &name);
// The shader names an audio slider's level, bass and beat take; no other slider may use them.
QStringList audioScalarNames(const QString &id);

}
