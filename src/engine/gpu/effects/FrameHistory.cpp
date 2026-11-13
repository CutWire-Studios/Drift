#include "engine/gpu/effects/FrameHistory.h"

#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>

#include <algorithm>
#include <cmath>

namespace drift::gl {

namespace {

#if defined(Q_OS_ANDROID)
constexpr qint64 kTotalBudgetBytes = 256ll << 20;
constexpr qint64 kRingBudgetBytes = 96ll << 20;
#else
constexpr qint64 kTotalBudgetBytes = 768ll << 20;
constexpr qint64 kRingBudgetBytes = 256ll << 20;
#endif
constexpr int kMaxAtlasEdge = 8192;
constexpr int kMinTileEdge = 16;
// Playback that drops a frame lands two project frames on; it keeps its history rather than restarting.
constexpr qint64 kMaxFrameGap = 2;

enum class Step { Reset, Repeat, Advance };

struct RingPlan
{
    bool ok = false;
    QSize tile;
    QSize atlas;
    int columns = 1;
    qint64 bytes = 0;
};

// The largest tile, scaled down from the frame in steps, whose atlas fits the texture limit and
// the byte budget.
RingPlan planRing(const QSize &frame, int ringSize, qint64 budget, int maxEdge)
{
    RingPlan plan;
    int tw = frame.width();
    int th = frame.height();
    while (tw >= kMinTileEdge && th >= kMinTileEdge) {
        if (tw <= maxEdge && th <= maxEdge) {
            const int columns = std::min(ringSize, maxEdge / tw);
            const int rows = (ringSize + columns - 1) / columns;
            if (rows * th <= maxEdge) {
                const qint64 bytes = qint64(columns) * tw * rows * th * 4;
                if (bytes <= budget) {
                    plan.ok = true;
                    plan.tile = {tw, th};
                    plan.columns = columns;
                    plan.atlas = {columns * tw, rows * th};
                    plan.bytes = bytes;
                    return plan;
                }
            }
        }
        tw = int(tw * 0.7);
        th = int(th * 0.7);
    }
    return plan;
}

void blitToRect(GlRuntime &rt, QOpenGLExtraFunctions *gl, GLuint srcTex, GlTarget &dest, const QRect &rect)
{
    dest.fbo->bind();
    gl->glViewport(rect.x(), rect.y(), rect.width(), rect.height());
    gl->glDisable(GL_BLEND);
    rt.copyProgram->bind();
    rt.copyProgram->setUniformValue("u_currentTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, srcTex);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    rt.copyProgram->release();
    dest.fbo->release();
}

void releaseSource(GlRuntime &rt, FrameHistoryStore::SourceRing &ring)
{
    rt.releaseTarget(std::move(ring.atlas));
    ring = {};
}

void releaseOutput(GlRuntime &rt, FrameHistoryStore::OutputRing &ring)
{
    for (GlTarget &target : ring.texture)
        rt.releaseTarget(std::move(target));
    ring.newest = 0;
    ring.count = 0;
    ring.bytes = 0;
}

} // namespace

qint64 FrameHistoryStore::totalBytes() const
{
    qint64 total = 0;
    for (const auto &entry : entries) {
        total += entry.second.source.bytes;
        for (const auto &output : entry.second.outputs)
            total += output.second.bytes;
    }
    return total;
}

void FrameHistoryStore::evictOthers(const Entry *keep, qint64 needed)
{
    while (totalBytes() + needed > kTotalBudgetBytes) {
        auto victim = entries.end();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (&it->second == keep || it->second.lastUse == serial)
                continue;
            if (victim == entries.end() || it->second.lastUse < victim->second.lastUse)
                victim = it;
        }
        if (victim == entries.end())
            return;
        entries.erase(victim);
    }
}

FrameHistoryStore &frameHistory(GlRuntime &rt)
{
    if (!rt.frameHistory)
        rt.frameHistory = std::make_shared<FrameHistoryStore>();
    return *rt.frameHistory;
}

void releaseFrameHistory(GlRuntime &rt)
{
    if (rt.frameHistory)
        rt.frameHistory->entries.clear();
}

HistoryComposite::HistoryComposite(GlRuntime &rt, quint64 scope)
    : m_store(frameHistory(rt)), m_scope(scope)
{
    if (m_store.depth++ == 0)
        ++m_store.serial;
}

HistoryComposite::~HistoryComposite()
{
    if (--m_store.depth > 0 || m_scope == 0)
        return;
    for (auto it = m_store.entries.begin(); it != m_store.entries.end();) {
        if (it->first.first == m_scope && it->second.lastUse != m_store.serial)
            it = m_store.entries.erase(it);
        else
            ++it;
    }
}

ClipHistory::ClipHistory(GlRuntime &rt, QOpenGLExtraFunctions *gl, quint64 scope, quint64 key,
                         drift::TimeUs clipTimeUs, int fps, const GlTarget &source, int sourceFrames,
                         const QMap<int, QString> &outputEffects)
{
    if (key == 0 || (sourceFrames <= 0 && outputEffects.isEmpty()) || !source.isValid())
        return;

    FrameHistoryStore &store = frameHistory(rt);
    FrameHistoryStore::Entry &entry = store.entries[{scope, key}];
    entry.lastUse = store.serial;
    m_store = &store;
    m_entry = &entry;

    const qint64 frame = std::llround(double(clipTimeUs) * (fps > 0 ? fps : 30) / 1'000'000.0);
    Step step = Step::Reset;
    if (entry.hasFrame) {
        const qint64 gap = frame - entry.lastFrame;
        if (gap == 0)
            step = Step::Repeat;
        else if (gap >= 1 && gap <= kMaxFrameGap)
            step = Step::Advance;
    }
    entry.lastFrame = frame;
    entry.hasFrame = true;

    for (auto it = entry.outputs.begin(); it != entry.outputs.end();) {
        FrameHistoryStore::OutputRing &ring = it->second;
        const auto wanted = outputEffects.constFind(it->first);
        if (wanted == outputEffects.constEnd()) {
            releaseOutput(rt, ring);
            it = entry.outputs.erase(it);
            continue;
        }
        if (ring.effectId != *wanted) {
            releaseOutput(rt, ring);
            ring.effectId = *wanted;
        } else if (step == Step::Reset) {
            ring.count = 0;
        } else if (step == Step::Repeat && ring.count > 0) {
            --ring.count;
            ring.newest = 1 - ring.newest;
        }
        ++it;
    }
    for (auto it = outputEffects.constBegin(); it != outputEffects.constEnd(); ++it) {
        if (!entry.outputs.count(it.key()))
            entry.outputs[it.key()].effectId = it.value();
    }

    FrameHistoryStore::SourceRing &ring = entry.source;
    if (sourceFrames <= 0) {
        releaseSource(rt, ring);
        return;
    }

    const int ringSize = sourceFrames + 1;
    if (ring.ringSize != ringSize || ring.frame != source.size()) {
        releaseSource(rt, ring);
        int hw = 0;
        gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &hw);
        const int maxEdge = hw > 0 ? std::min(hw, kMaxAtlasEdge) : 4096;
        store.evictOthers(&entry, kRingBudgetBytes);
        const qint64 budget =
            std::min(kRingBudgetBytes, kTotalBudgetBytes - store.totalBytes());
        const RingPlan plan = planRing(source.size(), ringSize, budget, maxEdge);
        if (!plan.ok)
            return;
        GlTarget atlas = rt.acquireTarget(plan.atlas.width(), plan.atlas.height());
        if (!atlas.isValid())
            return;
        ring.atlas = std::move(atlas);
        ring.frame = source.size();
        ring.tile = plan.tile;
        ring.columns = plan.columns;
        ring.ringSize = ringSize;
        ring.bytes = plan.bytes;
    } else if (step == Step::Reset) {
        ring.count = 0;
        ring.head = -1;
    } else if (step == Step::Repeat && ring.count > 0) {
        --ring.count;
        ring.head = (ring.head + ring.ringSize - 1) % ring.ringSize;
    }

    ring.head = (ring.head + 1) % ring.ringSize;
    ring.count = std::min(ring.count + 1, ring.ringSize);
    ring.premultiplied = source.premultiplied;
    blitToRect(rt, gl, source.texture(), ring.atlas,
               QRect((ring.head % ring.columns) * ring.tile.width(),
                     (ring.head / ring.columns) * ring.tile.height(), ring.tile.width(),
                     ring.tile.height()));
}

HistoryBinding ClipHistory::sourceBinding() const
{
    HistoryBinding binding;
    if (!m_entry || m_entry->source.ringSize == 0 || !m_entry->source.atlas.isValid())
        return binding;
    const FrameHistoryStore::SourceRing &ring = m_entry->source;
    binding.atlas = ring.atlas.texture();
    binding.atlasSize = ring.atlas.size();
    binding.tileSize = ring.tile;
    binding.columns = ring.columns;
    binding.ringSize = ring.ringSize;
    binding.count = ring.count - 1;
    binding.newest = (ring.head + ring.ringSize - 1) % ring.ringSize;
    return binding;
}

GLuint ClipHistory::previousOutput(int effectIndex) const
{
    if (!m_entry)
        return 0;
    const auto it = m_entry->outputs.find(effectIndex);
    if (it == m_entry->outputs.end() || it->second.count == 0)
        return 0;
    return it->second.texture[it->second.newest].texture();
}

void ClipHistory::recordOutput(GlRuntime &rt, QOpenGLExtraFunctions *gl, int effectIndex,
                               const GlTarget &out)
{
    Q_UNUSED(gl)
    if (!m_entry || !out.isValid())
        return;
    const auto it = m_entry->outputs.find(effectIndex);
    if (it == m_entry->outputs.end())
        return;
    FrameHistoryStore::OutputRing &ring = it->second;

    if (ring.texture[0].isValid() && ring.texture[0].size() != out.size())
        releaseOutput(rt, ring);
    if (!ring.texture[0].isValid()) {
        const qint64 bytes = 2ll * out.width * out.height * 4;
        m_store->evictOthers(m_entry, bytes);
        if (m_store->totalBytes() + bytes > kTotalBudgetBytes)
            return;
        ring.texture[0] = rt.acquireTarget(out.width, out.height);
        ring.texture[1] = rt.acquireTarget(out.width, out.height);
        if (!ring.texture[0].isValid() || !ring.texture[1].isValid()) {
            releaseOutput(rt, ring);
            return;
        }
        ring.bytes = bytes;
        ring.count = 0;
    }

    const int write = ring.count == 0 ? ring.newest : 1 - ring.newest;
    if (!blitTextureToTarget(rt, gl, out.texture(), ring.texture[write]))
        return;
    ring.newest = write;
    ring.count = std::min(ring.count + 1, 2);
}

PastFrame pastSourceFrame(GlRuntime &rt, quint64 scope, quint64 key, int age)
{
    if (!rt.frameHistory || age < 1)
        return {};
    const auto it = rt.frameHistory->entries.find({scope, key});
    if (it == rt.frameHistory->entries.end())
        return {};
    const FrameHistoryStore::SourceRing &ring = it->second.source;
    if (ring.ringSize == 0 || !ring.atlas.isValid() || age > ring.count - 1)
        return {};

    const int slot = ((ring.head - age) % ring.ringSize + ring.ringSize) % ring.ringSize;
    PastFrame frame;
    frame.texture = ring.atlas.texture();
    frame.atlasSize = ring.atlas.size();
    frame.size = ring.tile;
    frame.premultiplied = ring.premultiplied;
    frame.uv = QRectF(double((slot % ring.columns) * ring.tile.width()) / frame.atlasSize.width(),
                      double((slot / ring.columns) * ring.tile.height()) / frame.atlasSize.height(),
                      double(ring.tile.width()) / frame.atlasSize.width(),
                      double(ring.tile.height()) / frame.atlasSize.height());
    return frame;
}

} // namespace drift::gl
