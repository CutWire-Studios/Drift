#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QJsonValue>
#include <QSet>
#include <QString>

#include <optional>

namespace drift::forge {

class Compiler;
struct PassBuild;
struct NodeDef;

// What a node's emit function sees: its inputs as GLSL expressions plus the compiler services it may
// use. Each call may register samplers, uniforms, helpers or functions with the pass being built.
class EmitCtx
{
public:
    EmitCtx(Compiler &compiler, PassBuild &pass, const ForgeNode &node, const NodeDef &def);

    Kind kind;
    // Unique, GLSL-safe prefix for anything the node declares at file scope.
    QString prefix;

    // Input value as a GLSL expression of the input's own type, evaluated at `uv`.
    QString in(const QString &id, const QString &uv = QStringLiteral("uv"));
    // True when something is wired into the input.
    bool connected(const QString &id) const;

    QJsonValue opt(const QString &id) const;
    QString optString(const QString &id) const { return opt(id).toString(); }
    bool optBool(const QString &id) const { return opt(id).toBool(); }
    // Uniform name when the option is exposed as a Drift parameter, else nullopt.
    std::optional<QString> optParam(const QString &id);

    void helper(const QString &name);
    // u_time for effects, u_progress for transitions.
    QString clock();
    QString time();
    QString progress();
    QString res();
    QString aspect();
    QString source(int index, const QString &uv);
    // Samples the node's image (asset or exposed picture), top row at v=0 either way.
    QString asset(const QString &uv, const QString &option = QStringLiteral("asset"));
    QString paramSampler(const QString &key, const QString &uv);
    QString paramName(const QString &key);
    QString mask(const QString &uv);
    QString hasMask();
    QString depth(const QString &uv);
    QString depthNormal(const QString &uv, const QString &strength);
    QString hasDepth();
    QString face(const QString &name);
    QString engine(const QString &name);
    // Declares a file-scope function or constant; text must already use `prefix` for its names.
    void declare(const QString &text);

private:
    Compiler &m_c;
    PassBuild &m_p;
    const ForgeNode &m_node;
    const NodeDef &m_def;
    QSet<QString> m_declared;
};

}
