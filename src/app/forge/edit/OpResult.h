#pragma once

#include "app/forge/doc/ForgeDoc.h"

namespace drift::forge {

// Either a new document or an error message. `created` carries the ids or slider names the op made.
struct OpResult
{
    ForgeDoc doc;
    QString error;
    QStringList created;

    bool ok() const { return error.isEmpty(); }
    static OpResult fail(const QString &message) { return {{}, message, {}}; }
};

}
