#pragma once

#include "app/forge/doc/ForgeDoc.h"

namespace drift::forge {

QString uid(const QString &prefix = QString());
QString slugify(const QString &name);
QString makeEffectId(const QString &name);
ForgeDoc emptyDoc(Kind kind, const QString &name = QString());

// 0..1 RGBA from "#rrggbb" or "#rrggbbaa"; opaque black if malformed.
QJsonArray hexToRgba(const QString &hex);
QString rgbToHex(const QJsonArray &c);
QString rgbaToHex(const QJsonArray &c);

// JavaScript's Math.round: halves go toward +infinity.
double jsRound(double v);

}
