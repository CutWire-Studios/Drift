#pragma once

#include "app/forge/doc/Kinds.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace drift::forge {

inline constexpr int kForgeSchema = 3;

enum class SocketType { Float, Vec2, Color };

// Number, bool, [x, y] or [r, g, b, a].
using Literal = QJsonValue;
// A Literal, or {"param": name} / {"param": [nameX, nameY]}.
using InputValue = QJsonValue;

bool isParamRef(const QJsonValue &v);
// The slider names a ParamRef binds (one, or two for a point split over two sliders); empty if not a ref.
QStringList paramRefNames(const QJsonValue &v);
QJsonObject makeParamRef(const QString &name);

enum class ParamType {
    Float, Bool, Color, Int, Point, Choice, Seed, Region, Image, Track, TrackMask, Audio, Gradient, Curve
};

QString paramTypeName(ParamType t);
bool paramTypeFromName(const QString &name, ParamType *out);

struct ShowWhen
{
    QString param;
    bool equals = false;
};

struct ParamDef
{
    QString identifier;
    QString displayName;
    ParamType type = ParamType::Float;
    double min = 0;
    double max = 1;
    std::optional<QString> group;
    std::optional<QStringList> options;
    std::optional<bool> alpha;
    std::optional<QString> shape;
    // control/unit/step/precision; empty means absent.
    QJsonObject ui;
    std::optional<ShowWhen> showWhen;
    // Shape depends on type: number, bool, "#rrggbb[aa]", [x, y], [x, y, w, h], asset id, gradient or curve array.
    QJsonValue defaultValue;
    QJsonObject extra;
};

struct Preset
{
    QString name;
    QJsonObject values;
    QJsonObject extra;
};

struct ForgeNode
{
    QString id;
    QString type;
    double x = 0;
    double y = 0;
    QJsonObject inputs;
    QJsonObject data;
    QJsonObject extra;
};

struct ForgeEdge
{
    QString id;
    QString from;
    QString fromSocket;
    QString to;
    QString toSocket;
    QJsonObject extra;
};

struct ForgeAsset
{
    QString id;
    QString name;
    QString mime;
    QString data;
    double width = 0;
    double height = 0;
    QJsonObject extra;
};

struct ForgeMeta
{
    QString id;
    QString displayName;
    QString category;
    QString description;
    QString author;
    QString version;
    std::optional<QStringList> directions;
    std::optional<double> defaultDurationMs;
    QJsonObject extra;
};

struct ForgePreview
{
    double thumbTime = 0.5;
    std::optional<QString> customThumb;
    std::optional<QString> clip;
    // Audio preview input; opaque to the node-graph code.
    std::optional<QJsonObject> input;
    QJsonObject extra;
};

struct ForgeDoc
{
    Kind kind = Kind::Effect;
    ForgeMeta meta;
    QList<ParamDef> params;
    QList<ForgeNode> nodes;
    QList<ForgeEdge> edges;
    QList<ForgeAsset> assets;
    std::optional<QList<Preset>> presets;
    // Audio effects: {"rack": {chain, modulators, routes}}; kept as JSON, the rack has no native model yet.
    std::optional<QJsonObject> audio;
    ForgePreview preview;
    QJsonObject extra;

    const ForgeNode *node(const QString &id) const;
    ForgeNode *node(const QString &id);
    const ParamDef *param(const QString &identifier) const;
    ParamDef *param(const QString &identifier);

    // Applies schema migrations (2 -> 3: "clip" params become "track"). Rejects schema-1 audio documents.
    static ForgeDoc fromJson(const QJsonObject &json, QString *error = nullptr);
    static ForgeDoc fromJsonText(const QByteArray &text, QString *error = nullptr);
    QJsonObject toJson() const;
};

QJsonObject paramToJson(const ParamDef &p);
ParamDef paramFromJson(const QJsonObject &o);

}
