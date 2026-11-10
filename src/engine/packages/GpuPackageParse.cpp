#include "engine/packages/GpuPackageParse.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace GpuPackageParse {

namespace {

void fail(QString *errorOut, const QString &message)
{
    if (errorOut)
        *errorOut = message;
}

bool parsePassInput(const QJsonObject &obj, int maxSourceIndex, drift::GpuEffectPassInput *out,
                    QString *errorOut)
{
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("source_texture")) {
        out->type = drift::GpuEffectPassInput::Type::SourceTexture;
        out->sourceIndex = obj.value(QStringLiteral("index")).toInt(0);
        if (out->sourceIndex < 0 || out->sourceIndex > maxSourceIndex) {
            fail(errorOut, QStringLiteral("source_texture index %1 out of range (0..%2)")
                               .arg(out->sourceIndex)
                               .arg(maxSourceIndex));
            return false;
        }
        return true;
    }
    if (type == QLatin1String("buffer")) {
        out->type = drift::GpuEffectPassInput::Type::Buffer;
        out->bufferId = obj.value(QStringLiteral("id")).toString();
        if (out->bufferId.isEmpty()) {
            fail(errorOut, QStringLiteral("buffer input missing id"));
            return false;
        }
        return true;
    }
    if (type == QLatin1String("texture")) {
        out->type = drift::GpuEffectPassInput::Type::Texture;
        out->textureId = obj.value(QStringLiteral("id")).toString();
        if (out->textureId.isEmpty()) {
            fail(errorOut, QStringLiteral("texture input missing id"));
            return false;
        }
        return true;
    }
    fail(errorOut, QStringLiteral("unknown pass input type '%1'").arg(type));
    return false;
}

bool parsePassOutput(const QJsonObject &obj, drift::GpuEffectPassOutput *out, QString *errorOut)
{
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas")) {
        out->type = drift::GpuEffectPassOutput::Type::Canvas;
        return true;
    }
    if (type == QLatin1String("buffer")) {
        out->type = drift::GpuEffectPassOutput::Type::Buffer;
        out->bufferId = obj.value(QStringLiteral("id")).toString();
        if (out->bufferId.isEmpty()) {
            fail(errorOut, QStringLiteral("buffer output missing id"));
            return false;
        }
        return true;
    }
    fail(errorOut, QStringLiteral("unknown pass output type '%1'").arg(type));
    return false;
}

// "#rrggbb" or CSS "#rrggbbaa" to the "#rrggbbaa" a gradient stop is stored as.
bool normalizeRgbaHex(const QString &hex, QString *out)
{
    if (!hex.startsWith(QLatin1Char('#')) || (hex.size() != 7 && hex.size() != 9))
        return false;
    const QColor color(hex.left(7));
    if (!color.isValid())
        return false;
    int alpha = 255;
    if (hex.size() == 9) {
        bool ok = false;
        alpha = hex.right(2).toInt(&ok, 16);
        if (!ok)
            return false;
    }
    *out = color.name(QColor::HexRgb) + QStringLiteral("%1").arg(alpha, 2, 16, QLatin1Char('0'));
    return true;
}

} // namespace

QString readTextFile(const QString &path, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fail(errorOut, QStringLiteral("cannot open %1").arg(path));
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString slugifyCategory(const QString &raw)
{
    QString slug = raw.trimmed().toLower();
    slug.replace(QLatin1Char('&'), QLatin1Char(' '));
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    while (slug.startsWith(QLatin1Char('_')))
        slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('_')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("misc") : slug;
}

QVariant jsonToVariant(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble();
    if (value.isString())
        return value.toString();
    return value.toVariant();
}

bool parseParameters(const QJsonArray &params, QList<drift::EffectParamSpec> *out, bool gpuBackend,
                     QString *errorOut, const QString &packageDir)
{
    for (const QJsonValue &pv : params) {
        const QJsonObject p = pv.toObject();
        drift::EffectParamSpec spec;
        spec.key = p.value(QStringLiteral("identifier")).toString();
        if (spec.key.isEmpty())
            spec.key = p.value(QStringLiteral("key")).toString();
        spec.label = p.value(QStringLiteral("displayName")).toString();
        if (spec.label.isEmpty())
            spec.label = p.value(QStringLiteral("label")).toString();
        if (spec.label.isEmpty())
            spec.label = spec.key;
        const QString type = p.value(QStringLiteral("type")).toString(QStringLiteral("float"));
        if (type == QLatin1String("bool") || type == QLatin1String("boolean"))
            spec.type = drift::EffectParamType::Bool;
        else if (type == QLatin1String("color") || type == QLatin1String("colour"))
            spec.type = drift::EffectParamType::Color;
        else if (type == QLatin1String("file"))
            spec.type = drift::EffectParamType::FilePath;
        else if (type == QLatin1String("clip"))
            spec.type = drift::EffectParamType::Clip;
        else if (type == QLatin1String("point") || type == QLatin1String("vec2"))
            spec.type = drift::EffectParamType::Vec2;
        else if (type == QLatin1String("choice") || type == QLatin1String("enum"))
            spec.type = drift::EffectParamType::Enum;
        else if (type == QLatin1String("int") || type == QLatin1String("integer"))
            spec.type = drift::EffectParamType::Int;
        else if (type == QLatin1String("float") || type == QLatin1String("number"))
            spec.type = drift::EffectParamType::Float;
        else if (type == QLatin1String("seed"))
            spec.type = drift::EffectParamType::Seed;
        else if (type == QLatin1String("region"))
            spec.type = drift::EffectParamType::Region;
        else if (type == QLatin1String("image"))
            spec.type = drift::EffectParamType::Image;
        else if (type == QLatin1String("gradient"))
            spec.type = drift::EffectParamType::Gradient;
        else if (type == QLatin1String("curve"))
            spec.type = drift::EffectParamType::Curve;
        else if (type == QLatin1String("lut"))
            spec.type = drift::EffectParamType::Lut;
        else if (type == QLatin1String("track"))
            spec.type = drift::EffectParamType::Track;
        else if (type == QLatin1String("trackMask"))
            spec.type = drift::EffectParamType::TrackMask;
        else if (type == QLatin1String("audio"))
            spec.type = drift::EffectParamType::Audio;
        else {
            // An unknown type must not quietly become a float: that is how a colour param ends up
            // binding 0.0 to a vec3 uniform and rendering the frame black. But refusing the package
            // outright would take an already-installed effect away from someone who has it working
            // — effects.trending shipped duotone with type "string" and hex defaults. So infer the
            // colour when the default says plainly that it is one, and only refuse what cannot be
            // read at all.
            const QString declared = p.value(QStringLiteral("defaultValue")).toString().isEmpty()
                ? p.value(QStringLiteral("default")).toString()
                : p.value(QStringLiteral("defaultValue")).toString();
            if (declared.startsWith(QLatin1Char('#')) && QColor(declared).isValid()) {
                qWarning("GpuPackageParse: parameter '%s' has unknown type '%s'; reading it as a "
                         "colour from its default. Declare it as \"color\".",
                         qUtf8Printable(spec.key), qUtf8Printable(type));
                spec.type = drift::EffectParamType::Color;
            } else {
                fail(errorOut,
                     QStringLiteral("parameter '%1' has unknown type '%2'").arg(spec.key, type));
                return false;
            }
        }
        // A point carries its range as [min, min] / [max, max]; the axes share one range here.
        const auto scalarOrFirst = [](const QJsonValue &v, double fallback) {
            if (v.isArray())
                return v.toArray().isEmpty() ? fallback : v.toArray().first().toDouble(fallback);
            return v.toDouble(fallback);
        };
        const QJsonValue minV = p.contains(QStringLiteral("minValue")) ? p.value(QStringLiteral("minValue"))
                                                                       : p.value(QStringLiteral("min"));
        const QJsonValue maxV = p.contains(QStringLiteral("maxValue")) ? p.value(QStringLiteral("maxValue"))
                                                                       : p.value(QStringLiteral("max"));
        spec.min = scalarOrFirst(minV, 0.0);
        spec.max = scalarOrFirst(maxV, 1.0);
        const QJsonValue defV = p.contains(QStringLiteral("defaultValue")) ? p.value(QStringLiteral("defaultValue"))
                                                                           : p.value(QStringLiteral("default"));
        if (spec.type == drift::EffectParamType::Vec2) {
            const QJsonArray xy = defV.toArray();
            spec.defaultX = xy.size() > 0 ? xy.at(0).toDouble(0.0) : 0.0;
            spec.defaultY = xy.size() > 1 ? xy.at(1).toDouble(0.0) : 0.0;
        } else if (spec.type == drift::EffectParamType::Region) {
            const QJsonArray xywh = defV.toArray();
            spec.defaultX = xywh.size() > 0 ? xywh.at(0).toDouble(0.0) : 0.0;
            spec.defaultY = xywh.size() > 1 ? xywh.at(1).toDouble(0.0) : 0.0;
            spec.defaultW = xywh.size() > 2 ? xywh.at(2).toDouble(1.0) : 1.0;
            spec.defaultH = xywh.size() > 3 ? xywh.at(3).toDouble(1.0) : 1.0;
            spec.regionShape = p.value(QStringLiteral("shape")).toString(QStringLiteral("rect"));
            if (spec.regionShape != QLatin1String("rect") && spec.regionShape != QLatin1String("ellipse")) {
                fail(errorOut, QStringLiteral("parameter '%1' has an unknown region shape '%2'")
                                   .arg(spec.key, spec.regionShape));
                return false;
            }
        } else if (spec.type == drift::EffectParamType::Lut) {
            spec.defaultString = defV.toString();
        } else {
            spec.defaultValue = defV.toDouble(0.0);
        }
        spec.step = p.value(QStringLiteral("step")).toDouble(
            p.value(QStringLiteral("ui")).toObject().value(QStringLiteral("step")).toDouble(0.0));
        if (spec.type == drift::EffectParamType::Int)
            spec.step = 1.0;
        if (spec.type == drift::EffectParamType::Enum) {
            for (const QJsonValue &o : p.value(QStringLiteral("options")).toArray())
                spec.options.append(o.toString());
            if (spec.options.size() < 2) {
                fail(errorOut, QStringLiteral("parameter '%1' needs at least two options").arg(spec.key));
                return false;
            }
            spec.min = 0.0;
            spec.max = spec.options.size() - 1;
        }
        spec.desktopGlOnly = p.value(QStringLiteral("desktopGlOnly")).toBool(false);
        spec.group = p.value(QStringLiteral("group")).toString();
        spec.groupCollapsed = p.value(QStringLiteral("groupCollapsed")).toBool(false);
        if (spec.type == drift::EffectParamType::Curve) {
            spec.curveMode = p.value(QStringLiteral("curveMode")).toString();
            spec.curveChannel = p.value(QStringLiteral("curveChannel")).toString();
        }
        spec.widget = p.value(QStringLiteral("widget")).toString();
        spec.track = p.value(QStringLiteral("track")).toString();

        if (spec.key.isEmpty()) {
            fail(errorOut, QStringLiteral("parameter missing identifier"));
            return false;
        }
        if (spec.type == drift::EffectParamType::Color) {
            QString hex = p.value(QStringLiteral("defaultValue")).toString();
            if (hex.isEmpty())
                hex = p.value(QStringLiteral("default")).toString();
            spec.alpha = p.value(QStringLiteral("alpha")).toBool(false);
            // Package hex is CSS-style #rrggbbaa; Qt reads 8 digits as #aarrggbb, so split it.
            int alpha = 255;
            QString rgbHex = hex;
            if (spec.alpha && hex.size() == 9 && hex.startsWith(QLatin1Char('#'))) {
                bool ok = false;
                alpha = hex.right(2).toInt(&ok, 16);
                if (!ok)
                    alpha = 255;
                rgbHex = hex.left(7);
            }
            const QColor color(rgbHex);
            if (!rgbHex.startsWith(QLatin1Char('#')) || !color.isValid()) {
                fail(errorOut, QStringLiteral("parameter '%1' has an invalid colour default '%2'")
                                   .arg(spec.key, hex));
                return false;
            }
            // Normalized here so the project file, the swatch and the uniform all agree on one
            // spelling. Alpha survives only when the param declares "alpha": true, as
            // #rrggbbaa, which binds as a vec4; otherwise it is dropped and the colour is a vec3.
            spec.defaultColorHex = color.name(QColor::HexRgb);
            if (spec.alpha)
                spec.defaultColorHex += QStringLiteral("%1").arg(alpha, 2, 16, QLatin1Char('0'));
            for (const QJsonValue &sv : p.value(QStringLiteral("swatches")).toArray()) {
                const QString swatch = sv.toString();
                const QColor c(swatch);
                if (!swatch.startsWith(QLatin1Char('#')) || !c.isValid()) {
                    fail(errorOut, QStringLiteral("parameter '%1' has an invalid swatch '%2'")
                                       .arg(spec.key, swatch));
                    return false;
                }
                spec.swatches.append(c.name(QColor::HexRgb));
            }
            spec.enables = p.value(QStringLiteral("enables")).toString();
        } else if (spec.type == drift::EffectParamType::FilePath) {
            QString def = p.value(QStringLiteral("defaultValue")).toString();
            if (def.isEmpty())
                def = p.value(QStringLiteral("default")).toString();
            if (!def.isEmpty() && !packageDir.isEmpty())
                spec.defaultString = resolvePackageAsset(packageDir, def);
            else
                spec.defaultString = def;
            for (const QJsonValue &f : p.value(QStringLiteral("fileFilters")).toArray()) {
                const QString filter = f.toString();
                if (!filter.isEmpty())
                    spec.fileFilters.append(filter);
            }
            if (spec.fileFilters.isEmpty())
                spec.fileFilters.append(QStringLiteral("All files (*)"));
        } else if (spec.type == drift::EffectParamType::Image) {
            const QString file = p.value(QStringLiteral("defaultValue")).toString();
            if (!file.isEmpty()) {
                spec.defaultString = resolvePackageAsset(packageDir, file);
                if (spec.defaultString.isEmpty()) {
                    fail(errorOut, QStringLiteral("parameter '%1' default image '%2' is missing")
                                       .arg(spec.key, file));
                    return false;
                }
            }
        } else if (spec.type == drift::EffectParamType::Gradient) {
            QJsonArray stops;
            for (const QJsonValue &sv : defV.toArray()) {
                const QJsonObject stop = sv.toObject();
                QString rgba;
                if (!normalizeRgbaHex(stop.value(QStringLiteral("color")).toString(), &rgba)) {
                    fail(errorOut, QStringLiteral("parameter '%1' has an invalid gradient colour '%2'")
                                       .arg(spec.key, stop.value(QStringLiteral("color")).toString()));
                    return false;
                }
                const QJsonObject normalized{
                    {QStringLiteral("position"),
                     qBound(0.0, stop.value(QStringLiteral("position")).toDouble(0.0), 1.0)},
                    {QStringLiteral("color"), rgba},
                };
                stops.append(normalized);
            }
            if (stops.isEmpty()) {
                fail(errorOut, QStringLiteral("parameter '%1' needs at least one gradient stop").arg(spec.key));
                return false;
            }
            spec.defaultString = QString::fromUtf8(QJsonDocument(stops).toJson(QJsonDocument::Compact));
        } else if (spec.type == drift::EffectParamType::Curve) {
            static const QStringList kEases{QStringLiteral("linear"), QStringLiteral("smooth"),
                                            QStringLiteral("in"), QStringLiteral("out"),
                                            QStringLiteral("hold"), QStringLiteral("spline")};
            QJsonArray keys;
            for (const QJsonValue &kv : defV.toArray()) {
                const QJsonObject point = kv.toObject();
                const QString ease = point.value(QStringLiteral("ease")).toString(QStringLiteral("linear"));
                if (!kEases.contains(ease)) {
                    fail(errorOut, QStringLiteral("parameter '%1' has an unknown curve ease '%2'")
                                       .arg(spec.key, ease));
                    return false;
                }
                const QJsonObject normalized{
                    {QStringLiteral("x"), qBound(0.0, point.value(QStringLiteral("x")).toDouble(0.0), 1.0)},
                    {QStringLiteral("y"), qBound(0.0, point.value(QStringLiteral("y")).toDouble(0.0), 1.0)},
                    {QStringLiteral("ease"), ease},
                };
                keys.append(normalized);
            }
            if (keys.isEmpty()) {
                fail(errorOut, QStringLiteral("parameter '%1' needs at least one curve key").arg(spec.key));
                return false;
            }
            spec.defaultString = QString::fromUtf8(QJsonDocument(keys).toJson(QJsonDocument::Compact));
        }
        // File params are never GPU uniforms — skip the reserved-name check for them so a
        // package can still call a file param something that would collide as a uniform.
        if (gpuBackend && !spec.isFilePath() && !spec.isClip()
            && drift::isReservedGpuUniform(spec.key)) {
            fail(errorOut,
                 QStringLiteral("parameter '%1' collides with reserved uniform").arg(spec.key));
            return false;
        }
        out->append(spec);
    }
    for (const drift::EffectParamSpec &spec : std::as_const(*out)) {
        if (spec.enables.isEmpty())
            continue;
        const bool found = std::any_of(out->cbegin(), out->cend(), [&](const drift::EffectParamSpec &o) {
            return o.key == spec.enables && o.isBoolean();
        });
        if (!found) {
            fail(errorOut, QStringLiteral("parameter '%1' enables '%2', which is not a bool parameter")
                               .arg(spec.key, spec.enables));
            return false;
        }
    }
    // An audio param's scalars are named after it, so they must not shadow another parameter or
    // a uniform the engine already binds.
    for (const drift::EffectParamSpec &spec : std::as_const(*out)) {
        if (spec.type != drift::EffectParamType::Audio)
            continue;
        for (const char *suffix : {"_level", "_bass", "_beat"}) {
            const QString scalar = spec.key + QLatin1String(suffix);
            const bool taken = std::any_of(out->cbegin(), out->cend(), [&](const drift::EffectParamSpec &o) {
                return o.key == scalar;
            });
            if (taken || (gpuBackend && drift::isReservedGpuUniform(scalar))) {
                fail(errorOut, QStringLiteral("audio parameter '%1' collides with '%2'")
                                   .arg(spec.key, scalar));
                return false;
            }
        }
    }
    return true;
}

void collectRuntimeParams(const QList<drift::EffectParamSpec> &params, drift::GpuEffectDefinition *out)
{
    out->samplerParams.clear();
    out->regionParams.clear();
    for (const drift::EffectParamSpec &spec : params) {
        if (spec.isSampler())
            out->samplerParams.append({spec.key, spec.type, spec.curveMode == QLatin1String("hue")});
        else if (spec.isRegion())
            out->regionParams.append(spec.key);
    }
}

bool checkNextFeatures(const QJsonObject &root, QString *errorOut)
{
    static const QSet<QString> kSupported{
        QStringLiteral("param:int"),        QStringLiteral("param:point"),
        QStringLiteral("param:choice"),     QStringLiteral("param:seed"),
        QStringLiteral("param:region"),     QStringLiteral("param:image"),
        QStringLiteral("param:gradient"),   QStringLiteral("param:curve"),
        QStringLiteral("param:lut"),        QStringLiteral("curve:spline"),
        QStringLiteral("param:track"),      QStringLiteral("param:trackMask"),
        QStringLiteral("param:audio"),      QStringLiteral("param:color-alpha"),
        QStringLiteral("uniform:audio"),    QStringLiteral("requires:mask"),
        QStringLiteral("requires:depth"),   QStringLiteral("requires:face"),
    };
    QStringList unsupported;
    for (const QJsonValue &v : root.value(QStringLiteral("nextFeatures")).toArray()) {
        const QString feature = v.toString();
        // Forge's graph nodes are internal to it, so every node entry is accepted.
        if (feature.startsWith(QLatin1String("node:")) || kSupported.contains(feature))
            continue;
        unsupported.append(feature);
    }
    if (unsupported.isEmpty())
        return true;
    fail(errorOut, QStringLiteral("needs a newer Drift (unsupported: %1)")
                       .arg(unsupported.join(QLatin1String(", "))));
    return false;
}

void parseFixedParams(const QJsonObject &obj, QMap<QString, QVariant> *out)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
        out->insert(it.key(), jsonToVariant(it.value()));
}

bool loadGpuPipeline(const QJsonObject &root, const QString &packageDir, int maxSourceIndex,
                     drift::GpuEffectDefinition *out, QString *errorOut)
{
    const QJsonObject pipeline = root.value(QStringLiteral("pipeline")).toObject();

    const QJsonArray buffers = pipeline.value(QStringLiteral("intermediateBuffers")).toArray();
    for (const QJsonValue &bv : buffers) {
        const QJsonObject b = bv.toObject();
        drift::GpuEffectBufferSpec buf;
        buf.id = b.value(QStringLiteral("id")).toString();
        buf.scale = b.value(QStringLiteral("scale")).toDouble(1.0);
        if (buf.id.isEmpty()) {
            fail(errorOut, QStringLiteral("intermediate buffer missing id"));
            return false;
        }
        if (buf.scale <= 0.0) {
            fail(errorOut, QStringLiteral("intermediate buffer scale must be > 0"));
            return false;
        }
        out->intermediateBuffers.append(buf);
    }

    QSet<QString> bufferIds;
    for (const drift::GpuEffectBufferSpec &b : out->intermediateBuffers)
        bufferIds.insert(b.id);

    const QJsonArray textures = pipeline.value(QStringLiteral("textures")).toArray();
    for (const QJsonValue &tv : textures) {
        const QJsonObject t = tv.toObject();
        drift::GpuEffectTextureSpec tex;
        tex.id = t.value(QStringLiteral("id")).toString();
        tex.file = t.value(QStringLiteral("file")).toString();
        if (tex.id.isEmpty() || tex.file.isEmpty()) {
            fail(errorOut, QStringLiteral("texture needs both id and file"));
            return false;
        }
        tex.path = QDir(packageDir).filePath(tex.file);
        if (!QFileInfo::exists(tex.path)) {
            fail(errorOut, QStringLiteral("missing texture file '%1'").arg(tex.file));
            return false;
        }
        out->textures.append(tex);
    }

    QSet<QString> textureIds;
    for (const drift::GpuEffectTextureSpec &t : out->textures)
        textureIds.insert(t.id);

    const QJsonArray passes = pipeline.value(QStringLiteral("passes")).toArray();
    if (passes.isEmpty()) {
        fail(errorOut, QStringLiteral("pipeline has no passes"));
        return false;
    }

    int index = 0;
    for (const QJsonValue &pv : passes) {
        const QJsonObject p = pv.toObject();
        drift::GpuEffectPass pass;
        pass.passIndex = p.value(QStringLiteral("passIndex")).toInt(index);
        pass.fragmentShaderFile = p.value(QStringLiteral("fragmentShader")).toString();
        if (pass.fragmentShaderFile.isEmpty()) {
            fail(errorOut, QStringLiteral("pass %1 missing fragmentShader").arg(index));
            return false;
        }

        const QString shaderPath = QDir(packageDir).filePath(pass.fragmentShaderFile);
        if (!QFileInfo::exists(shaderPath)) {
            fail(errorOut, QStringLiteral("missing shader file '%1'").arg(pass.fragmentShaderFile));
            return false;
        }
        QString shaderError;
        pass.fragmentShaderSource = readTextFile(shaderPath, &shaderError);
        if (pass.fragmentShaderSource.isEmpty()) {
            fail(errorOut,
                 shaderError.isEmpty() ? QStringLiteral("empty shader '%1'").arg(pass.fragmentShaderFile)
                                       : shaderError);
            return false;
        }

        const QJsonArray inputs = p.value(QStringLiteral("inputs")).toArray();
        if (inputs.isEmpty()) {
            pass.inputs.append(drift::GpuEffectPassInput{});
        } else {
            for (const QJsonValue &iv : inputs) {
                drift::GpuEffectPassInput input;
                if (!parsePassInput(iv.toObject(), maxSourceIndex, &input, errorOut))
                    return false;
                if (input.type == drift::GpuEffectPassInput::Type::Buffer
                    && !bufferIds.contains(input.bufferId)) {
                    fail(errorOut,
                         QStringLiteral("pass references unknown buffer '%1'").arg(input.bufferId));
                    return false;
                }
                if (input.type == drift::GpuEffectPassInput::Type::Texture
                    && !textureIds.contains(input.textureId)) {
                    fail(errorOut,
                         QStringLiteral("pass references unknown texture '%1'").arg(input.textureId));
                    return false;
                }
                pass.inputs.append(input);
            }
        }

        const QJsonObject outputObj = p.value(QStringLiteral("output")).toObject();
        if (outputObj.isEmpty()) {
            if (index == passes.size() - 1) {
                pass.output.type = drift::GpuEffectPassOutput::Type::Canvas;
            } else {
                fail(errorOut, QStringLiteral("pass %1 missing output").arg(index));
                return false;
            }
        } else if (!parsePassOutput(outputObj, &pass.output, errorOut)) {
            return false;
        }

        if (pass.output.type == drift::GpuEffectPassOutput::Type::Buffer
            && !bufferIds.contains(pass.output.bufferId)) {
            fail(errorOut, QStringLiteral("pass output references unknown buffer '%1'")
                               .arg(pass.output.bufferId));
            return false;
        }

        const QString geometry = p.value(QStringLiteral("geometry")).toString(QStringLiteral("quad"));
        if (geometry == QLatin1String("face111")) {
            // The mesh only covers the face, so the pass has to start from a copy of a frame
            // rather than a static texture.
            if (pass.inputs.first().type == drift::GpuEffectPassInput::Type::Texture) {
                fail(errorOut,
                     QStringLiteral("face111 pass %1 needs a source or buffer as input 0").arg(index));
                return false;
            }
            if (maxSourceIndex > 0) {
                fail(errorOut, QStringLiteral("face111 passes are only supported in effects"));
                return false;
            }
            const QJsonArray bounds = p.value(QStringLiteral("templateBounds")).toArray();
            if (bounds.size() != 4) {
                fail(errorOut,
                     QStringLiteral("face111 pass %1 needs templateBounds [x, y, w, h]").arg(index));
                return false;
            }
            pass.templateBounds = QRectF(bounds.at(0).toDouble(), bounds.at(1).toDouble(),
                                         bounds.at(2).toDouble(), bounds.at(3).toDouble());
            if (!(pass.templateBounds.width() > 0.0) || !(pass.templateBounds.height() > 0.0)) {
                fail(errorOut,
                     QStringLiteral("face111 pass %1 templateBounds needs a positive size").arg(index));
                return false;
            }
            pass.geometry = drift::GpuEffectPass::Geometry::Face111;
        } else if (geometry != QLatin1String("quad")) {
            fail(errorOut, QStringLiteral("pass %1 has unknown geometry '%2'").arg(index).arg(geometry));
            return false;
        }

        out->passes.append(pass);
        ++index;
    }

    static const QRegularExpression kGlobalAudioUniform(QStringLiteral("\\bu_audio(Level|Bass|Beat)\\b"));
    out->usesGlobalAudio = std::any_of(out->passes.cbegin(), out->passes.cend(),
                                       [](const drift::GpuEffectPass &pass) {
                                           return kGlobalAudioUniform.match(pass.fragmentShaderSource).hasMatch();
                                       });

    out->valid = true;
    return true;
}

QString resolvePackageAsset(const QString &packageDir, const QString &relOrAbs)
{
    if (relOrAbs.isEmpty())
        return {};
    const QString path = QFileInfo(relOrAbs).isAbsolute() ? QDir::cleanPath(relOrAbs)
                                                          : QDir(packageDir).filePath(relOrAbs);
    return QFileInfo::exists(path) ? path : QString();
}

} // namespace GpuPackageParse
