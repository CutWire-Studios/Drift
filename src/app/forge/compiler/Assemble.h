#pragma once

#include "app/forge/compiler/Compiler.h"

namespace drift::forge {

struct AssembleFlags
{
    bool usesTime = false;
    bool usesMask = false;
    bool usesDepth = false;
    bool usesFace = false;
};

// Lays the built passes out as files, sharing buffers between passes that are never alive at once.
CompileResult assemble(const ForgeDoc &doc, const QList<PassBuild *> &order, const AssembleFlags &flags);

}
