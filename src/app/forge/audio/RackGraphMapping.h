#pragma once

#include "app/forge/audio/Rack.h"
#include "app/forge/audio/RackOps.h"
#include "app/forge/edit/OpResult.h"
#include "ui/nodes/NodeGraphController.h"

#include <QMap>
#include <QPointF>
#include <QVariant>

// The pedalboard as a node graph. The rack is the only model: nodes and wires are derived from it,
// and every edit on the canvas is turned back into a rack operation. Only positions live outside
// the rack, in the document's top-level "nativeLayout" key.
namespace drift::forge::rackgraph {

inline constexpr const char *kInputId = "$input";
inline constexpr const char *kOutputId = "$output";

QString mergeId(const QString &splitId);
// The split a merge node belongs to; null string for any other id.
QString splitOfMerge(const QString &nodeId);

QMap<QString, QPointF> storedLayout(const ForgeDoc &doc);
ForgeDoc withLayout(const ForgeDoc &doc, const QMap<QString, QPointF> &layout);
// Left to right by chain order, lanes stacked, modulators in a row underneath.
QMap<QString, QPointF> autoLayout(const ForgeDoc &doc);
// Every node's position: the stored one when there is one, the automatic one otherwise.
QMap<QString, QPointF> layoutOf(const ForgeDoc &doc);

// validateRack's messages that belong to one node, by node id; the rest land on the output node.
QMap<QString, QString> nodeErrors(const ForgeDoc &doc);

QList<NodeItem> nodeItems(const ForgeDoc &doc);
QList<EdgeItem> edgeItems(const ForgeDoc &doc);
QList<NodeGraphController::CatalogEntry> catalogEntries();
// Socket type of a port ("audio", "mod" or "float"); empty when the port does not exist.
QString portType(const ForgeDoc &doc, const QString &node, const QString &port, bool isOutput);

// Outputs first, whichever way the wire was dragged.
bool canConnect(const ForgeDoc &doc, const QString &fromNode, const QString &fromPort, const QString &toNode,
                const QString &toPort);
OpResult connect(const ForgeDoc &doc, const QString &fromNode, const QString &fromPort, const QString &toNode,
                 const QString &toPort);
// Removes the routes into a modulation port. Audio wiring follows pedal order, so it has nothing to remove.
ForgeDoc disconnectInput(const ForgeDoc &doc, const QString &node, const QString &port);

ForgeDoc deleteNodes(const ForgeDoc &doc, const QStringList &ids);
// Catalog types: a pedal type, "split:parallel", "split:bands", "lfo", "envelope", "steps".
// Pedals and splits join the end of the top chain. created[0] is the new node's id.
OpResult addNode(const ForgeDoc &doc, const QString &type, const QPointF &at);
OpResult addNodeConnected(const ForgeDoc &doc, const QString &type, const QPointF &at, const QString &fromNode,
                          const QString &fromPort, bool fromIsOutput);
ForgeDoc moveNodes(const ForgeDoc &doc, const QStringList &ids, qreal dx, qreal dy);

// An inline control of a node: a knob, a split's mode, crossfade, blend or crossoverN, a convolution's
// "ir", an envelope's "source".
ForgeDoc setInputValue(const ForgeDoc &doc, const QString &node, const QString &input, const QVariant &value);
// The control behind an input when it can be bound to a slider.
std::optional<ValuePath> valuePathOf(const ForgeDoc &doc, const QString &node, const QString &input);

}
