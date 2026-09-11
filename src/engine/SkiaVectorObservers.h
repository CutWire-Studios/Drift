#pragma once

#include "VectorInspect.h"

#include <QList>
#include <QStringList>

#include "modules/skottie/include/Skottie.h"
#include "modules/skottie/include/SkottieProperty.h"

// Skottie build-time observers behind Drift-shaped accessors. They live in a Skia*.cpp because
// those are compiled without RTTI like Skia itself: a class deriving from a Skia interface with
// an out-of-line virtual (PropertyObserver, ImageAsset) needs the base's typeinfo, which Skia's
// archives do not carry.

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
