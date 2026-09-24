#pragma once

#include "GpuCompositor.h"
#include "core/Project.h"
#include "core/Time.h"

#include <QHash>
#include <QImage>
#include <QString>

// Preview canvas as a fraction of project resolution. Below this, text and effect
// radii collapse to subpixels. The compositor never upscales past 1.0.
inline constexpr double kMinPreviewScale = 0.02;
inline constexpr int kMinPreviewScalePercent = 2;

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
        // Read low-res preview proxies where they exist. Only live playback sets it; export and
        // every other caller must see the original pixels.
        bool allowProxies = false;
        // Reuse converted video frames across composites. Only the paused preview sets it: an
        // edit there re-renders the same source frames, where playback never repeats one.
        bool cacheVideoSources = false;
    };

    void setProject(const drift::Project *project) { m_project = project; }
    // Nested sequence views are built from the project and reused across composites of it. The
    // owner calls this whenever the project behind the pointer changes; comparing pointers here
    // cannot tell, since a new snapshot can land at a freed one's address.
    void clearNestedViewCache() const { m_nestedViews.clear(); }

    // Drop the decoded-still cache (image clips and stickers, scaled to the render canvas). Pure
    // derived pixels — a cleared entry costs one re-read of the file. For the Android
    // application-state handler. Safe from any thread.
    static void clearStillImageCache();

    QImage compositeAt(drift::TimeUs timelineUs) const;
    QImage compositeAt(drift::TimeUs timelineUs, const RenderOptions &options) const;

    // Composite and leave the frame on the GPU. Returns an invalid handle when
    // OpenGL is unavailable, in which case callers should use compositeAt.
    GpuFrameTexture compositeToTextureAt(drift::TimeUs timelineUs, const RenderOptions &options) const;

    // Builds the GPU scene for T without composing. Used by the export pipeline
    // so decode/scene-build can overlap the previous frame's GL work.
    bool buildSceneAt(drift::TimeUs timelineUs, const RenderOptions &options, GpuScene *sceneOut) const;

private:
    // Shared by both entry points: resolves the canvas size, warms the decoders
    // and builds the scene. Returns false when there is nothing to render.
    bool prepare(drift::TimeUs timelineUs, const RenderOptions &options, GpuScene *sceneOut, int *widthOut,
                 int *heightOut, double *renderScaleOut) const;


    const drift::Project *m_project = nullptr;
    mutable QHash<QString, std::shared_ptr<const drift::Project>> m_nestedViews;
};

Q_DECLARE_METATYPE(FrameCompositor::RenderOptions)
