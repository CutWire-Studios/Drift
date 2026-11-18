#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace drift::forge {

struct LiteralUniform
{
    QString name;
    QString node;
    QString input;
    SocketType type = SocketType::Float;
};

struct CompiledPass
{
    QString file;
    QString source;
    // {"type":"source_texture","index"?} / {"type":"buffer","id"} / {"type":"texture","id"}
    QJsonArray inputs;
    // {"type":"canvas"} or {"type":"buffer","id"}
    QJsonObject output;
    // Sampler parameters bound by name to the units after `inputs` (see paramSamplerUnit).
    QStringList paramSamplers;
};

// Drift binds depth on unit 8 and the clip mask on unit 9 in every pass, so param samplers skip them.
int paramSamplerUnit(int inputCount, int index);

struct TexRef
{
    enum Type { Source, Buffer, Asset } type = Source;
    int index = 0;
    QString sym;
    QString assetId;

    QString key() const;
};

// A pass while it is being built; ordered containers keep the output deterministic.
struct PassBuild
{
    QString key;
    // The buffer symbol this pass renders to; empty for the root pass, which draws to the canvas.
    QString writes;
    QList<TexRef> samplers;
    QStringList helpers;
    QStringList functions;
    QHash<QString, QString> fnNames;
    bool usesRes = false;
    bool usesTime = false;
    bool usesProgress = false;
    QStringList params;
    QStringList engines;
    QList<LiteralUniform> literals;
    QString main;

    void addUnique(QStringList &list, const QString &v)
    {
        if (!list.contains(v))
            list.append(v);
    }
};

QString samplerName(int i);

}
