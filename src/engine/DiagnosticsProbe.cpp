#include "DiagnosticsProbe.h"

#include "ClipReader.h"
#include "GlRuntime.h"

namespace drift::diag {

ProbeScope::ProbeScope()
{
    m_decodeBackend = ClipReader::activeDecodeBackendRaw();
    m_hwFallbackCount = ClipReader::hardwareFallbackCount();
    m_uploadPath = static_cast<int>(drift::gl::GlRuntime::lastPreviewUploadPath());
    m_declineReason = drift::gl::GlRuntime::lastZeroCopyDeclineReason();
}

ProbeScope::~ProbeScope()
{
    ClipReader::setActiveDecodeBackendRaw(m_decodeBackend);
    ClipReader::setHardwareFallbackCount(m_hwFallbackCount);
    drift::gl::GlRuntime::restorePreviewUploadPath(
        static_cast<drift::gl::GlRuntime::PreviewUploadPath>(m_uploadPath), m_declineReason);
}

} // namespace drift::diag
