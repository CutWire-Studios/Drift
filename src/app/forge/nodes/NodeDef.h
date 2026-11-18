#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QList>
#include <QMap>
#include <QString>

#include <functional>
#include <optional>

namespace drift::forge {

// Defined by the compiler; node emitters receive it by reference.
class EmitCtx;

// Samples a stage buffer at a uv expression.
using StageReader = std::function<QString(const QString &uv)>;

struct InputDef
{
    QString id;
    QString label;
    SocketType type = SocketType::Float;
    Literal defaultValue;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<double> step;
    // Unconnected, this input follows the clip clock: seconds for effects, progress for transitions.
    bool clock = false;
    // "angle", "point", "toggle", "swatch", "seed" or "choice" (index into the node's `labels` option); empty = none.
    QString widget;
    // Counts (slices, levels...): exposed as a whole-number slider.
    bool integer = false;
    QString unit;
    // Pixel-space and similar inputs where exposing would confuse; stays a plain literal.
    bool noExpose = false;
    QString hint;
};

struct OutputDef
{
    QString id;
    QString label;
    SocketType type = SocketType::Float;
};

enum class OptionKind { Select, Toggle, Number, Code, Labels, Curve, Gradient, Region, Asset };

struct SelectChoice
{
    QString value;
    QString label;
};

struct OptionDef
{
    QString id;
    QString label;
    OptionKind kind = OptionKind::Toggle;
    // Undefined for Asset options, which have no default.
    QJsonValue defaultValue = QJsonValue::Undefined;
    QList<SelectChoice> choices;
    double min = 0;
    double max = 0;
    std::optional<double> step;

    // Curve, gradient, picture and region options can be exposed to Drift by storing a ParamRef in node.data.
    bool exposable() const
    {
        return kind == OptionKind::Curve || kind == OptionKind::Gradient || kind == OptionKind::Asset
            || kind == OptionKind::Region;
    }
};

struct StageDef
{
    // A function body producing vec4; `prev` samples the previous stage (stage 0: none).
    std::function<QString(EmitCtx &, const StageReader &prev)> body;
};

struct NodeDef
{
    QString type;
    QString label;
    QString category;
    QString description;
    QList<InputDef> inputs;
    QList<OutputDef> outputs;
    QList<OptionDef> options;
    // Restricts the node to these document kinds; empty = all.
    QList<Kind> kinds;
    // Listed in the package's nextFeatures: Drift versions that can't run it refuse the package.
    bool next = false;
    // Cheap to call many times (raw texture reads, constants): never worth a buffer of its own.
    bool cheap = false;
    bool output = false;
    // Inputs sampled many times per pixel; their upstream renders to a buffer first unless cheap.
    QStringList heavyInputsStatic;
    std::function<QStringList(const std::function<QJsonValue(const QString &)> &opt)> heavyInputs;
    QList<StageDef> stages;
    // Named emitFn because Qt defines `emit` as a macro. One function body per output id; `stage` samples the last stage's buffer when `stages` is set.
    std::function<QMap<QString, QString>(EmitCtx &, const StageReader &stage)> emitFn;

    const InputDef *input(const QString &id) const
    {
        for (const InputDef &i : inputs)
            if (i.id == id)
                return &i;
        return nullptr;
    }
    const OutputDef *outputDef(const QString &id) const
    {
        for (const OutputDef &o : outputs)
            if (o.id == id)
                return &o;
        return nullptr;
    }
    const OptionDef *option(const QString &id) const
    {
        for (const OptionDef &o : options)
            if (o.id == id)
                return &o;
        return nullptr;
    }
};

}
