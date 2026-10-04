#include "GpuPackageParse.h"

#include "AddonRegistry.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

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
    return true;
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

QStringList defaultSearchPaths(const QString &envVar, const QString &subdir,
                               const QString &addonKind)
{
    QStringList roots;

    const QByteArray env = qgetenv(envVar.toUtf8().constData());
    if (!env.isEmpty()) {
        const QStringList parts = QString::fromLocal8Bit(env).split(QDir::listSeparator(),
                                                                    Qt::SkipEmptyParts);
        roots.append(parts);
    }

    // Ahead of the bundled copy: effects and transitions ship with the build as a baseline, and an
    // installed addon of the same id is meant to supersede it.
    if (!addonKind.isEmpty())
        roots.append(drift::addon::addonRootsForKind(addonKind));

#ifdef Q_OS_ANDROID
    // applicationDirPath() is the APK's lib directory and holds nothing but .so files, so the
    // bundled packages ride inside the binary as Qt resources instead (see qt_add_resources in
    // CMakeLists.txt). QDir enumerates and QFile reads ":/" paths exactly as real ones, which is
    // why every catalog and shader loader downstream works against them unchanged.
    roots.append(QStringLiteral(":/packages/") + subdir);
#else
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        roots.append(QDir(appDir).filePath(subdir));
#ifdef Q_OS_MACOS
        // In a bundle applicationDirPath() is Contents/MacOS; packages ship in Contents/Resources.
        roots.append(QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("../Resources/%1").arg(subdir))));
#endif
    }
#endif

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty())
        roots.append(QDir(appData).filePath(subdir));

    roots.removeDuplicates();
    return roots;
}

} // namespace GpuPackageParse
