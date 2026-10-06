#pragma once

#include "engine/SceneCamera3d.h"

#include <QColor>
#include <QList>
#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector3D>

// The guides the preview's 3D view draws into the world: a ground grid, the stage (the canvas at
// z = 0), the scene camera as a frustum with its projection rectangle, and the selection's box.
// Pure geometry in world space, so it is testable without GL; GlEditorOverlay draws it.
namespace drift {

struct EditorLine
{
    QVector3D a;
    QVector3D b;
    QColor color;
    float widthPx = 1.f;
    // Drawn before the clips, so they cover it: the floor grid. Everything else goes on top.
    bool under = false;
};

struct EditorOverlayInput
{
    QSizeF canvas; // in the same px as the world (render scale applied)
    bool cameraActive = false;
    SceneCamera3d camera; // likewise in render-scale px
    bool cameraSelected = false;
    // The selected clip's unit quad ([-1, 1]^2) to world, when one is selected and placed in 3D.
    bool hasSelectedQuad = false;
    QMatrix4x4 selectedQuad;
    // A selected model's rest box: model space to world, and the box in model space.
    bool hasSelectedModel = false;
    QMatrix4x4 selectedModel;
    QVector3D selectedModelMin;
    QVector3D selectedModelMax;
};

QList<EditorLine> buildEditorOverlay(const EditorOverlayInput &input);

// The part of segment a-b in front of the viewpoint's near plane, in canvas px. False when all of
// it is behind.
bool clipEditorSegment(const QMatrix4x4 &viewProj, const QVector3D &a, const QVector3D &b,
                       QPointF *pa, QPointF *pb);

// Where the ray from `eye` through `through` meets the plane of a unit quad placed by `quad`
// (its z = 0 plane), or false when it runs parallel or the plane is behind the eye.
bool rayHitsQuadPlane(const QVector3D &eye, const QVector3D &through, const QMatrix4x4 &quad,
                      QVector3D *hit);

} // namespace drift
