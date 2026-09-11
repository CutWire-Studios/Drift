#pragma once

namespace drift {

enum class VectorBackend { Qt, Skia };

// Which renderer draws shapes. Skia when it is compiled in, unless DRIFT_VECTOR_RENDERER=qt|skia
// or the "render/vectorBackend" setting says otherwise. Text and vector clips are Skia-only: a
// build without Skia draws no text at all.
VectorBackend vectorBackend();

} // namespace drift
