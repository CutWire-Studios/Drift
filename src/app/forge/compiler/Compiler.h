#pragma once

#include "app/forge/compiler/Passes.h"
#include "app/forge/compiler/Validate.h"
#include "app/forge/doc/ForgeDoc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>

#include <optional>
#include <utility>

namespace drift::forge {

struct CompileOptions
{
    enum Mode { Export, Preview };
    Mode mode = Export;
    // Render this {node, output} instead of the Output node (per-node previews).
    std::optional<std::pair<QString, QString>> target;
};

struct CompileResult
{
    bool ok = false;
    QList<CompileError> errors;
    // The last error message per node, for the blocks in the editor.
    QMap<QString, QString> nodeErrors;
    QList<CompiledPass> passes;
    // "buf0", "buf1"...
    QJsonArray buffers;
    // {id, assetId, file}
    QJsonArray textures;
    // Preview mode only: unconnected inputs become uniforms so sliders don't recompile.
    QList<LiteralUniform> literals;
    bool usesTime = false;
    // Reads the clip's masks: the package declares "requires": "mask".
    bool usesMask = false;
    bool usesDepth = false;
    bool usesFace = false;
};

// Audio effects have no shader; they compile to an empty ok result.
CompileResult compile(const ForgeDoc &doc, const CompileOptions &opts = {});

// Everything about a document that changes the generated shader; literal values don't.
QString structureKey(const ForgeDoc &doc);

// The preview-mode compile as the web preview consumes it (the shape the golden preview.json uses).
QJsonObject previewJson(const ForgeDoc &doc, const CompileResult &result);

}
