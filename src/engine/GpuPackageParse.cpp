#include "GpuPackageParse.h"

#include "AddonRegistry.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

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
        else
            spec.type = drift::EffectParamType::Float;
        spec.min = p.value(QStringLiteral("minValue")).toDouble(p.value(QStringLiteral("min")).toDouble(0.0));
        spec.max = p.value(QStringLiteral("maxValue")).toDouble(p.value(QStringLiteral("max")).toDouble(1.0));
        spec.defaultValue =
            p.value(QStringLiteral("defaultValue")).toDouble(p.value(QStringLiteral("default")).toDouble(0.0));

        if (spec.key.isEmpty()) {
            fail(errorOut, QStringLiteral("parameter missing identifier"));
            return false;
        }
        if (spec.type == drift::EffectParamType::Color) {
            QString hex = p.value(QStringLiteral("defaultValue")).toString();
            if (hex.isEmpty())
                hex = p.value(QStringLiteral("default")).toString();
            const QColor color(hex);
            if (!hex.startsWith(QLatin1Char('#')) || !color.isValid()) {
                fail(errorOut, QStringLiteral("parameter '%1' has an invalid colour default '%2'")
                                   .arg(spec.key, hex));
                return false;
            }
            // Normalized here so the project file, the swatch and the uniform all agree on one
            // spelling. Alpha is dropped on purpose: colours bind as vec3, and a package that
            // wants transparency declares a separate opacity float.
            spec.defaultColorHex = color.name(QColor::HexRgb);
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
        if (gpuBackend && !spec.isFilePath() && drift::isReservedGpuUniform(spec.key)) {
            fail(errorOut,
                 QStringLiteral("parameter '%1' collides with reserved uniform").arg(spec.key));
            return false;
        }
        out->append(spec);
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

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty())
        roots.append(QDir(appDir).filePath(subdir));

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty())
        roots.append(QDir(appData).filePath(subdir));

    roots.removeDuplicates();
    return roots;
}

} // namespace GpuPackageParse
