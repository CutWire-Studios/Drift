#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace drift {

// A vecN[] uniform carried through the parameter map. Only the engine produces these — the face
// contour loops are the one thing too large to spell out as named scalars, and 128 points would
// otherwise mean 256 uniformLocation() lookups per pass.
//
// Never reaches Effect::parameters, so it never has to survive JSON: applyFaceUniforms injects it
// into the transient map that resolvedEffectParameters hands to the executor.
struct GpuFloatArray
{
    QVector<float> values;
    int tupleSize = 2; // 2 -> vec2[], 3 -> vec3[]
};

// Uniforms the executor binds itself, from its own state rather than from the parameter map.
// Binding them again from a parameter of the same name would just overwrite the real value.
inline bool isEngineBoundGpuUniform(const QString &name)
{
    if (name == QLatin1String("u_resolution") || name == QLatin1String("u_time")
        || name == QLatin1String("u_timeUs") || name == QLatin1String("u_frameIndex")
        || name == QLatin1String("u_currentTexture") || name == QLatin1String("u_progress")
        || name == QLatin1String("u_fromTexture") || name == QLatin1String("u_toTexture")
        || name == QLatin1String("u_hasDepth")) {
        return true;
    }
    // The depth map and its size, bound for "requires": "depth" packages.
    if (name.startsWith(QLatin1String("u_depth")))
        return true;
    // Extra samplers bound for multi-input passes: u_texture1, u_texture2, ...
    return name.startsWith(QLatin1String("u_texture"));
}

// Names package JSON must not claim. Wider than the above: the per-frame face anchors travel to
// the shader *through* the parameter map, so they must still be bound from it, but a package
// declaring its own u_face* slider would collide with what the engine injects.
inline bool isReservedGpuUniform(const QString &name)
{
    return isEngineBoundGpuUniform(name) || name.startsWith(QLatin1String("u_face"));
}

struct GpuEffectBufferSpec
{
    QString id;
    double scale = 1.0;
};

// Static image asset loaded from the package dir and bound as a sampler.
struct GpuEffectTextureSpec
{
    QString id;
    QString file; // relative to packageDir
    QString path; // resolved absolute path
};

struct GpuEffectPassInput
{
    enum class Type { SourceTexture, Buffer, Texture };
    Type type = Type::SourceTexture;
    QString bufferId;    // Buffer
    QString textureId;   // Texture
    int sourceIndex = 0; // SourceTexture: 0 = from/primary, 1 = to, ...
};

struct GpuEffectPassOutput
{
    enum class Type { Buffer, Canvas };
    Type type = Type::Canvas;
    QString bufferId;
};

struct GpuEffectPass
{
    int passIndex = 0;
    QString fragmentShaderFile;   // relative filename from the package JSON
    QString fragmentShaderSource; // loaded GLSL
    QList<GpuEffectPassInput> inputs;
    GpuEffectPassOutput output;
};

// Parsed, validated GPU package pipeline (effect.json / transition.json + shaders).
struct GpuEffectDefinition
{
    QString packageDir;
    QList<GpuEffectBufferSpec> intermediateBuffers;
    QList<GpuEffectTextureSpec> textures;
    QList<GpuEffectPass> passes;
    // "requires": "depth": the depth prelude is compiled into every pass and the clip's depth map
    // is bound beside the declared inputs. See docs/gpu-effects.md.
    bool needsDepth = false;
    bool valid = false;
    QString errorMessage;
};

} // namespace drift

Q_DECLARE_METATYPE(drift::GpuFloatArray)
