#pragma once

#include "core/animation/Keyframe.h"
#include "core/base/Time.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVariant>

namespace drift {

// A Lua-built 3D scene placed on a Graphic track (ClipType::Procedural). Only data lives here: the
// package (code, shaders, param schema) is looked up by id in the engine's procedural catalog, and
// a clip whose package is missing keeps all of this so it renders again once the package returns.
struct ProceduralSource
{
    QString packageId;

    // Same shape as Effect: a keyed param wins over `parameters` at render time, and `parameters`
    // keeps the last static value. Keyframe times are clip-relative.
    QMap<QString, QVariant> parameters;
    QMap<QString, KeyframeTrack<double>> paramKeyframes;

    int seed = 0;

    bool isEmpty() const { return packageId.isEmpty(); }
    bool isAnimated() const;
    QVariant valueAt(const QString &key, TimeUs clipTimeUs) const;
    // Every animated param baked down to its value at clipTimeUs; paramKeyframes is cleared.
    ProceduralSource resolvedAt(TimeUs clipTimeUs) const;

    QJsonObject toJson() const;
    static ProceduralSource fromJson(const QJsonObject &o);
};

} // namespace drift
