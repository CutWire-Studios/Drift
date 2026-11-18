#include "app/forge/doc/Naming.h"

#include "app/forge/glsl/Literals.h"

#include <QRegularExpression>

namespace drift::forge {

QString paramNameProblem(const QString &name)
{
    static const QRegularExpression ident(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    static const QRegularExpression generated(QStringLiteral("^(n|s|k|x|pv)_"));
    static const QRegularExpression generatedBuf(QStringLiteral("^buf\\d+$|^tex\\d+$"));
    if (!ident.match(name).hasMatch())
        return QStringLiteral("Use letters, digits and _ only, starting with a letter.");
    if (name.startsWith(QLatin1String("u_")) || name.startsWith(QLatin1String("gl_")))
        return QStringLiteral("Names starting with \"u_\" or \"gl_\" are reserved by Drift.");
    if (generated.match(name).hasMatch())
        return QStringLiteral("Names starting with n_, s_, k_, x_ or pv_ are used by generated code.");
    if (generatedBuf.match(name).hasMatch())
        return QStringLiteral("That name is used by generated code.");
    if (glslReserved().contains(name))
        return QStringLiteral("That word is reserved in shader code.");
    if (name.size() > 48)
        return QStringLiteral("Keep it under 48 characters.");
    return {};
}

QStringList audioScalarNames(const QString &id)
{
    return {id + QStringLiteral("_level"), id + QStringLiteral("_bass"), id + QStringLiteral("_beat")};
}

}
