#pragma once

#include "app/forge/edit/OpResult.h"

#include <QMap>

namespace drift::forge {

// A block that reads a sampler parameter the block itself creates.
struct SamplerNode
{
    QString key;
    QString base;
    QString label;
    ParamType type;
};
const SamplerNode *samplerNode(const QString &nodeType);

// created[0] is the new node's id.
OpResult addNode(const ForgeDoc &doc, const QString &type, double x, double y);
// Removes blocks (never the Output) and every wire touching them.
ForgeDoc removeNodes(const ForgeDoc &doc, const QStringList &ids);
// Copies offset by 40,40 with the wires between copied blocks relinked; created holds the new ids.
OpResult duplicateNodes(const ForgeDoc &doc, const QStringList &ids);
ForgeDoc moveNode(const ForgeDoc &doc, const QString &id, double x, double y);
// Wires an output into an input, replacing whatever fed that input. Refuses loops.
OpResult connect(const ForgeDoc &doc, const QString &from, const QString &fromSocket, const QString &to,
                 const QString &toSocket);
ForgeDoc disconnect(const ForgeDoc &doc, const QStringList &edgeIds);
ForgeDoc setInput(const ForgeDoc &doc, const QString &node, const QString &input, const InputValue &value);
ForgeDoc setData(const ForgeDoc &doc, const QString &node, const QString &key, const QJsonValue &value);
ForgeDoc addAsset(const ForgeDoc &doc, const ForgeAsset &asset);

}
