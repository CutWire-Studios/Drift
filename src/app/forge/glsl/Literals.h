#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QSet>
#include <QString>

namespace drift::forge {

QString glslFloat(double n);
QString glslType(SocketType t);
QString glslLiteral(const Literal &v, SocketType t);
// Converts between socket types. color -> float uses luma(), so callers must include that helper.
QString coerce(const QString &expr, SocketType from, SocketType to);
const QSet<QString> &glslReserved();

}
