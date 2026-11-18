#pragma once

#include "app/forge/edit/OpResult.h"

namespace drift::forge {

QString uniqueParamName(const ForgeDoc &doc, const QString &base);
// How many inputs, options and rack controls read a parameter.
int paramUsers(const ForgeDoc &doc, const QString &name);

// Turns an unconnected setting into a slider; created holds its name (two for a point already split).
OpResult expose(const ForgeDoc &doc, const QString &nodeId, const QString &inputId,
                const QString &labelOverride = QString());
ForgeDoc unexpose(const ForgeDoc &doc, const QString &nodeId, const QString &inputId);
// Exposes a curve, gradient, picture or region option; created[0] is the slider name.
OpResult exposeOption(const ForgeDoc &doc, const QString &nodeId, const QString &optionId);
ForgeDoc unexposeOption(const ForgeDoc &doc, const QString &nodeId, const QString &optionId);

ForgeDoc addParam(const ForgeDoc &doc, const ParamDef &p);
// `patch` holds the ParamDef JSON keys to overwrite, like Object.assign.
OpResult updateParam(const ForgeDoc &doc, const QString &identifier, const QJsonObject &patch);
ForgeDoc removeParam(const ForgeDoc &doc, const QString &identifier);
ForgeDoc moveParam(const ForgeDoc &doc, const QString &identifier, int delta);
ForgeDoc savePreset(const ForgeDoc &doc, const QString &name, const QJsonObject &values);
ForgeDoc deletePreset(const ForgeDoc &doc, const QString &name);

}
