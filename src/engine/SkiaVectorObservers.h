#pragma once

#include "VectorInspect.h"

#include <QList>
#include <QStringList>

#include "modules/skottie/include/Skottie.h"
#include "modules/skottie/include/SkottieProperty.h"

// Skottie build-time observers behind Drift-shaped accessors, keeping the Skia subclasses out of
// the Vector*.cpp callers.

namespace drift::skia {

class InspectObservers
{
public:
    InspectObservers();
    ~InspectObservers();

    void attach(skottie::Animation::Builder &builder) const;

    QStringList loggedLines() const;                      // deduplicated warnings and errors
    QList<vec::VectorNamedProperty> namedProperties() const;
    QList<vec::VectorMarker> markers() const;

private:
    struct Impl;
    Impl *d;
};

} // namespace drift::skia
