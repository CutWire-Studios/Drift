#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QMap>

namespace drift::forge {

struct CompileError
{
    QString node;
    QString param;
    QString message;
};

struct ValidateOptions
{
    bool requireOutput = true;
};

QList<CompileError> validate(const ForgeDoc &doc, const ValidateOptions &opts = {});
// The last message per node, as the editor shows them on the blocks.
QMap<QString, QString> nodeErrors(const QList<CompileError> &errors);

}
