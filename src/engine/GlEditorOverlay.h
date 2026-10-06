#pragma once

#include "engine/EditorOverlay.h"
#include "engine/GlRuntime.h"

#include <QOpenGLExtraFunctions>

namespace drift::gl {

// Draws the 3D view's guide lines onto `canvas` through `viewProj` (world -> homogeneous canvas
// px). `under` picks the ones that go beneath the clips (the floor) or the ones on top. Lines are
// clipped at the near plane on the CPU and widened into screen-space quads, since GL line widths
// past one pixel are not portable.
void drawEditorLines(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &canvas,
                     const QMatrix4x4 &viewProj, const QList<EditorLine> &lines, bool under);

} // namespace drift::gl
