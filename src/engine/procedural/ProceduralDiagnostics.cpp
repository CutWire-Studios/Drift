#include "engine/procedural/ProceduralDiagnostics.h"

namespace drift {

namespace {
constexpr std::size_t kMaxClips = 256;
constexpr std::size_t kMaxPackages = 64;
}

ProceduralDiagnostics &ProceduralDiagnostics::shared()
{
    static ProceduralDiagnostics instance;
    return instance;
}

void ProceduralDiagnostics::setClip(const std::string &clipId, ProceduralClipReport report)
{
    std::lock_guard lock(m_mutex);
    if (m_clips.size() >= kMaxClips && !m_clips.count(clipId)) {
        auto oldest = m_clips.begin();
        for (auto it = m_clips.begin(); it != m_clips.end(); ++it) {
            if (it->second.serial < oldest->second.serial)
                oldest = it;
        }
        m_clips.erase(oldest);
    }
    report.serial = ++m_serial;
    m_clips[clipId] = std::move(report);
}

bool ProceduralDiagnostics::clip(const std::string &clipId, ProceduralClipReport *out) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_clips.find(clipId);
    if (it == m_clips.end())
        return false;
    if (out)
        *out = it->second;
    return true;
}

void ProceduralDiagnostics::clearClip(const std::string &clipId)
{
    std::lock_guard lock(m_mutex);
    m_clips.erase(clipId);
}

void ProceduralDiagnostics::setShader(const std::string &sourceHash, const std::string &shader,
                                      ProceduralShaderStatus status)
{
    std::lock_guard lock(m_mutex);
    if (m_shaders.size() >= kMaxPackages && !m_shaders.count(sourceHash))
        m_shaders.erase(m_shaders.begin());
    m_shaders[sourceHash][shader] = std::move(status);
}

std::map<std::string, ProceduralShaderStatus> ProceduralDiagnostics::shaders(const std::string &sourceHash) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_shaders.find(sourceHash);
    return it == m_shaders.end() ? std::map<std::string, ProceduralShaderStatus>{} : it->second;
}

} // namespace drift
