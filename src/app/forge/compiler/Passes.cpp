#include "app/forge/compiler/Passes.h"

namespace drift::forge {

int paramSamplerUnit(int inputCount, int index)
{
    int unit = inputCount;
    for (int i = 0;; ++unit) {
        if (unit == 8 || unit == 9)
            continue;
        if (i++ == index)
            return unit;
    }
}

QString TexRef::key() const
{
    switch (type) {
    case Source: return QStringLiteral("source:%1").arg(index);
    case Buffer: return QStringLiteral("buffer:%1").arg(sym);
    case Asset: return QStringLiteral("asset:%1").arg(assetId);
    }
    return {};
}

QString samplerName(int i)
{
    return i == 0 ? QStringLiteral("u_currentTexture") : QStringLiteral("u_texture%1").arg(i);
}

}
