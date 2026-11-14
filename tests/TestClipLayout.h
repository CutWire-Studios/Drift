#pragma once

#include "core/project/clip/Clip.h"

// A static layout box in project px: the frame is the box, scale is 100%, and the position is its
// centre, so the clip draws exactly in that rect.
inline void setTestRect(drift::Clip &clip, double x, double y, double w, double h)
{
    clip.frameW = w;
    clip.frameH = h;
    clip.positionX.setKeyframe(0, x + w / 2.0);
    clip.positionY.setKeyframe(0, y + h / 2.0);
}
