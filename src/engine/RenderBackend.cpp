#include "RenderBackend.h"

#include <QSettings>
#include <QString>

#include <mutex>

namespace drift {

VectorBackend vectorBackend()
{
#ifndef DRIFT_WITH_SKIA
    return VectorBackend::Qt;
#else
    // Env is read live so tests can qputenv between scenes; QSettings is what must not run per
    // frame.
    if (qEnvironmentVariableIsSet("DRIFT_VECTOR_RENDERER"))
        return qgetenv("DRIFT_VECTOR_RENDERER").toLower() == "qt" ? VectorBackend::Qt
                                                                  : VectorBackend::Skia;
    static VectorBackend backend = VectorBackend::Skia;
    static std::once_flag once;
    std::call_once(once, [] {
        const QString value = QSettings().value(QStringLiteral("render/vectorBackend")).toString();
        if (value.compare(QStringLiteral("qt"), Qt::CaseInsensitive) == 0)
            backend = VectorBackend::Qt;
    });
    return backend;
#endif
}

} // namespace drift
