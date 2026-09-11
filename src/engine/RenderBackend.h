#pragma once

namespace drift {

enum class VectorBackend { Qt, Skia };

// Which renderer draws shapes (and, as the Skia port proceeds, text). Skia when it is compiled in,
// unless DRIFT_VECTOR_RENDERER=qt|skia or the "render/vectorBackend" setting says otherwise; the
// Qt path stays as the fallback until parity is proven for every content kind.
VectorBackend vectorBackend();

} // namespace drift
