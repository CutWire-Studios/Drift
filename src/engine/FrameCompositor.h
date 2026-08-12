#pragma once

#include "GpuCompositor.h"
#include "core/Project.h"
#include "core/Time.h"

#include <QImage>
#include <QString>

// Composites all visible tracks into a single RGBA frame at timeline time T.
class FrameCompositor
{
public:
    struct RenderOptions
    {
        double previewScale = 1.0;
        int maxTimeEchoHistoryFrames = -1;
        // How far ahead of this frame the decoders keep buffering, in source
        // time. Only realtime playback sets it; a paused preview would just be
        // decoding frames an edit is about to invalidate.
        drift::TimeUs readAheadUs = 0;
        // Clip id to omit from the frame (the text clip being edited in place on
        // the preview). Empty renders everything.
        QString skipClipId;
        // Clip id whose mask coverage should be shown instead of the composite, as a
        // black-and-white frame. Preview only — the exporter builds its own RenderOptions and
        // never sets this, which is the whole reason it lives here rather than on the
        // controller where the compositor could reach it.
        QString maskViewClipId;
    };

    void setProject(const drift::Project *project) { m_project = project; }

    QImage compositeAt(drift::TimeUs timelineUs) const;
    QImage compositeAt(drift::TimeUs timelineUs, const RenderOptions &options) const;

    // Composite and leave the frame on the GPU. Returns an invalid handle when
    // OpenGL is unavailable, in which case callers should use compositeAt.
    GpuFrameTexture compositeToTextureAt(drift::TimeUs timelineUs, const RenderOptions &options) const;

private:
    // Shared by both entry points: resolves the canvas size, warms the decoders
    // and builds the scene. Returns false when there is nothing to render.
    bool prepare(drift::TimeUs timelineUs, const RenderOptions &options, GpuScene *sceneOut, int *widthOut,
                 int *heightOut, double *renderScaleOut) const;


    const drift::Project *m_project = nullptr;
};

Q_DECLARE_METATYPE(FrameCompositor::RenderOptions)
