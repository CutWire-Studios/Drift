#pragma once

#include "engine/compositor/GpuCompositor.h"
#include "engine/render3d/scene/EditorView3d.h"
#include "core/project/Project.h"
#include "core/base/Time.h"
#include "engine/render2d/text/TextLayout.h"

#include <QColor>
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
        // Read low-res preview proxies where they exist. Only live playback sets it; export and
        // every other caller must see the original pixels.
        bool allowProxies = false;
        // Reuse converted video frames across composites. Only the paused preview sets it: an
        // edit there re-renders the same source frames, where playback never repeats one.
        bool cacheVideoSources = false;
        // Width of the scope readback (GpuFrameTexture::scopeImage); 0 means none is wanted.
        int scopeReadbackWidth = 0;
        // Scrubbing: a video layer may show the nearest frame that is cheap to get (cached, or
        // the keyframe at or before the time) instead of decoding forward to the exact one. The
        // caller asks again without it once the gesture settles.
        bool approximateSeek = false;
        // Audio params: a source whose onsets are not computed yet is analysed in the background
        // and its beat reads 0 until it is. Only the preview sets it; export and every other caller
        // wait, so a frame never depends on how fast the analysis happened to run.
        bool deferAudioAnalysis = false;
        // The preview's 3D mode: the scene seen from the editor's free viewpoint, with the camera
        // and the stage drawn as guides. Only the preview sets it; export never does.
        struct EditorView
        {
            bool active = false;
            drift::EditorView3d view;
            // Bumped by the preview on every view change, so two requests at the same playhead
            // with different views are not taken for the same frame.
            quint64 serial = 0;
            // The selected clip, whose box, and the camera's frame on whose plane, the view draws.
            QString selectedClipId;
            // The view is being dragged: the preview drops its supersampling to keep up.
            bool navigating = false;
            // The size of the frame to render, in px: the panel's aspect rather than the
            // project's, supersampled while still. Empty renders the project's frame.
            QSize target;
        };
        EditorView editor;

        // The text clip being edited in place on the preview. It stays visible, at rest (no
        // in/out/loop animation), with a caret, a selection and a preedit underline drawn over
        // it by a separate layer that the clip's effects do not touch. Offsets are UTF-16 into
        // the text as rendered, preedit included. Only the preview sets it; export never does.
        struct TextEditDecor
        {
            QString clipId;
            int cursor = -1;
            int selStart = -1;
            int selEnd = -1;
            int preeditStart = -1;
            int preeditLength = 0;
            QColor caretColor;     // invalid = white
            QColor selectionColor; // invalid = translucent blue

            bool active() const { return !clipId.isEmpty(); }
            bool operator==(const TextEditDecor &other) const = default;
        };
        TextEditDecor textEdit;

        // The preview's mask view of one clip: its mask drawn as a tinted overlay or as a matte.
        // Honoured by the GPU path only; export and thumbnails never set it.
        struct MaskView
        {
            QString clipId;
            MaskViewMode mode = MaskViewMode::Off;
            bool operator==(const MaskView &other) const = default;
        };
        MaskView maskView;
        // The Overlay view's tint for the part the mask cuts away; its alpha is the strength.
        QColor maskViewTint = QColor(0xFF, 0x30, 0x30, 0x80);
    };

    // The media time a mask reads at (before looping); exposed so tests can pin the time bases.
    static drift::TimeUs maskMediaSourceTimeUs(const drift::Clip &host, const drift::Mask &mask,
                                               drift::TimeUs adjustmentStartUs, drift::TimeUs adjustmentSrcInUs,
                                               drift::TimeUs timelineUs);

    // How a text clip is laid out at `timelineUs`, as the compositor will draw it: the layout rect
    // (canvas px at renderScale, before any rotation about its centre) and the shaped block for
    // `renderedText`. The inline editor maps its caret and selection geometry through this, so
    // the two cannot disagree.
    struct TextEditGeometry
    {
        std::shared_ptr<const drift::text::FragmentSet> set;
        QRectF layoutRect;
        double rotation = 0.0; // degrees clockwise about layoutRect.center()
    };
    static TextEditGeometry textEditGeometry(const drift::Clip &clip, const QString &renderedText,
                                             drift::TimeUs timelineUs, int projectWidth, int projectHeight,
                                             double renderScale);

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
