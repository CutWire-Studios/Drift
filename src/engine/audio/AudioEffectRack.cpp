#include "engine/audio/AudioEffectRack.h"

#include "engine/audio/GraphProcessor.h"

#include <QHash>

#include <algorithm>
#include <vector>

namespace drift {

namespace {

// Stages are prepared once for this block size and everything is processed in chunks of it. Two of
// mix()'s callers (the subtitle waveform at 8 kHz and beat detection at 22 kHz) hand over an entire
// timeline range in a single call, so the block size the rack sees must not decide its allocations.
constexpr int kSubBlock = 1024;

} // namespace

// A built chain and the spec it came from. Parameter ids resolve to binding indices on first use
// and stay cached, so applying values per block is a hash hit rather than a string conversion.
struct RackChain
{
    std::unique_ptr<audiofx::GraphProcessor> processor;
    int specIndex = 0;
    QHash<QString, int> indices;

    int indexOf(const QString &paramId)
    {
        const auto it = indices.constFind(paramId);
        if (it != indices.constEnd())
            return it.value();
        const int index = processor->parameterIndex(paramId.toStdString());
        indices.insert(paramId, index);
        return index;
    }
};

struct AudioEffectRack::Impl
{
    QString signature;
    int sampleRate = 0;
    int primeFrames = 0;
    drift::TimeUs lastTimelineEndUs = -1;
    std::vector<RackChain> chains;
    juce::AudioBuffer<float> scratch;

    void run(const float *in, float *out, int frames);
};

void AudioEffectRack::Impl::run(const float *in, float *out, int frames)
{
    float *left = scratch.getWritePointer(0);
    float *right = scratch.getWritePointer(1);
    float *channels[2] = {left, right};

    for (int offset = 0; offset < frames;) {
        const int count = std::min(kSubBlock, frames - offset);

        for (int i = 0; i < count; ++i) {
            left[i] = in[(offset + i) * 2];
            right[i] = in[(offset + i) * 2 + 1];
        }

        juce::dsp::AudioBlock<float> block(channels, 2, 0, static_cast<size_t>(count));
        for (auto &chain : chains)
            chain.processor->process(block);

        if (out) {
            for (int i = 0; i < count; ++i) {
                out[(offset + i) * 2] = left[i];
                out[(offset + i) * 2 + 1] = right[i];
            }
        }

        offset += count;
    }
}

AudioEffectRack::AudioEffectRack()
    : m_impl(std::make_unique<Impl>())
{
}

AudioEffectRack::~AudioEffectRack() = default;
AudioEffectRack::AudioEffectRack(AudioEffectRack &&other) noexcept = default;
AudioEffectRack &AudioEffectRack::operator=(AudioEffectRack &&other) noexcept = default;

bool AudioEffectRack::configure(const QVector<AudioEffectSpec> &specs, int sampleRate, bool *rebuilt)
{
    if (rebuilt)
        *rebuilt = false;
    if (specs.isEmpty() || sampleRate <= 0) {
        reset();
        m_impl->chains.clear();
        m_impl->signature.clear();
        return false;
    }

    // Only the structure goes into the signature. Values are pushed into live stages below, which
    // is the whole point: changing one must not tear the DSP down. The graph's address joins the
    // id so a reloaded catalog (an addon updated in place) rebuilds too.
    QString signature = QString::number(sampleRate);
    for (const AudioEffectSpec &spec : specs) {
        signature += QLatin1Char('|') + spec.key + QLatin1Char('@')
                     + QString::number(reinterpret_cast<quintptr>(spec.graph.get()), 16);
    }

    const bool didRebuild = signature != m_impl->signature;
    if (rebuilt)
        *rebuilt = didRebuild;
    if (didRebuild) {
        m_impl->chains.clear();
        m_impl->signature = signature;
        m_impl->sampleRate = sampleRate;

        juce::dsp::ProcessSpec processSpec;
        processSpec.sampleRate = sampleRate;
        processSpec.maximumBlockSize = static_cast<juce::uint32>(kSubBlock);
        processSpec.numChannels = 2;

        m_impl->scratch.setSize(2, kSubBlock, false, true, true);

        int latency = 0;
        int preroll = 0;
        for (int i = 0; i < specs.size(); ++i) {
            const AudioEffectSpec &spec = specs[i];
            if (!spec.graph)
                continue;
            auto chain = std::make_unique<audiofx::GraphProcessor>(spec.graph);
            chain->prepare(processSpec);
            latency += chain->latencySamples();
            preroll = std::max(preroll,
                               static_cast<int>((static_cast<int64_t>(spec.prerollMs) * sampleRate) / 1000));
            m_impl->chains.push_back(RackChain{std::move(chain), i, {}});
        }

        // Latency has to be covered or output arrives late; preroll has to be covered or tails
        // start cold. Feeding the larger of the two satisfies both.
        m_impl->primeFrames = std::max(latency, preroll);
    }

    if (m_impl->chains.empty())
        return false;

    // An unchanged signature means the same specs in the same order, so specIndex still holds.
    for (RackChain &chain : m_impl->chains) {
        const AudioEffectSpec &spec = specs[chain.specIndex];
        for (auto it = spec.parameters.constBegin(); it != spec.parameters.constEnd(); ++it)
            chain.processor->setParameter(chain.indexOf(it.key()), it.value());
    }

    // Stages were prepared before their values arrived, so a new chain would otherwise open by
    // gliding up from its defaults. Nothing to lose here: these chains have no state yet.
    if (didRebuild) {
        for (auto &chain : m_impl->chains)
            chain.processor->reset();
    }

    return true;
}

int AudioEffectRack::primeFrames() const
{
    return m_impl->chains.empty() ? 0 : m_impl->primeFrames;
}

void AudioEffectRack::warmUp(const float *interleavedStereo, int frames)
{
    if (!interleavedStereo || frames <= 0 || m_impl->chains.empty())
        return;
    m_impl->run(interleavedStereo, nullptr, frames);
}

void AudioEffectRack::process(float *interleavedStereo, int frames)
{
    if (!interleavedStereo || frames <= 0 || m_impl->chains.empty())
        return;
    m_impl->run(interleavedStereo, interleavedStereo, frames);
}

void AudioEffectRack::reset()
{
    for (auto &chain : m_impl->chains)
        chain.processor->reset();
    m_impl->lastTimelineEndUs = -1;
}

drift::TimeUs AudioEffectRack::lastTimelineEndUs() const
{
    return m_impl->lastTimelineEndUs;
}

void AudioEffectRack::setLastTimelineEndUs(drift::TimeUs us)
{
    m_impl->lastTimelineEndUs = us;
}

} // namespace drift
