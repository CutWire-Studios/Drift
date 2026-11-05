#pragma once

// The live preview renders the timeline as independent streams: the scene camera's picture and,
// while a 3D viewport is open, the editor's free viewpoint. Plain ints cross into QML.
enum class PreviewStream : int
{
    Camera = 0,
    Editor = 1,
};

inline constexpr int kPreviewStreamCount = 2;
