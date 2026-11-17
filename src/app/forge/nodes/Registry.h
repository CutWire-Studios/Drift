#pragma once

#include "app/forge/nodes/NodeDef.h"

namespace drift::forge {

// Adds definitions to the registry. Called by the per-category register* functions in Registry.cpp's
// built-in list; a type registered twice keeps its first definition.
void registerNodeDefs(QList<NodeDef> defs);

const NodeDef *nodeDef(const QString &type);
const QList<const NodeDef *> &allNodeDefs();

bool availableFor(const NodeDef &def, Kind kind);
// A node with every input and (non-asset) option at its default and a fresh id.
ForgeNode createNode(const QString &type, double x, double y);
QString outputType(Kind kind);

void registerInputNodes();
void registerControlNodes();
void registerColorNodes();
void registerGradeNodes();
void registerDistortNodes();
void registerKitDistortNodes();
void registerThreeNodes();
void registerBlurNodes();
void registerKitBlurNodes();
void registerStylizeNodes();
void registerGenerateNodes();
void registerKitOverlayNodes();
void registerMixNodes();
void registerTransitionNodes();
void registerKitTransitionNodes();
void registerAnimateNodes();
void registerMathNodes();
void registerAdvancedNodes();
void registerOutputNodes();

}
