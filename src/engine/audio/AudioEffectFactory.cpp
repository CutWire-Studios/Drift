#include "engine/audio/AudioEffectFactory.h"

#include "engine/audio/AudioEffectProcessor.h"
#include "engine/audio/DynamicsProcessors.h"
#include "engine/audio/FilterProcessors.h"
#include "engine/audio/ModulationProcessors.h"
#include "engine/audio/SoundTouchPitchProcessor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>

namespace drift::audiofx {

namespace {

using Builder = std::function<void(ChainProcessor &)>;

// Manifests whose parameter is a linear amplitude where the stage wants dB. The avfilter chains
// were inconsistent about this — utility.compressor wrote "{threshold}dB" while texture.tape wrote
// a bare linear threshold — and saved projects store whichever the manifest used, so the
// conversion belongs here rather than in a rewritten manifest.
float linearToDb(float linear)
{
    return 20.0f * std::log10(std::max(linear, 1.0e-5f));
}

// ---- utility ----------------------------------------------------------------------------

void buildEq3(ChainProcessor &chain)
{
    auto *eq = chain.addStage<ThreeBandEqProcessor>();
    chain.bind("low", eq, &ThreeBandEqProcessor::setLowGainDb);
    chain.bind("mid", eq, &ThreeBandEqProcessor::setMidGainDb);
    chain.bind("high", eq, &ThreeBandEqProcessor::setHighGainDb);
}

void buildCompressor(ChainProcessor &chain)
{
    auto *compressor = chain.addStage<CompressorProcessor>();
    chain.bind("threshold", compressor, &CompressorProcessor::setThresholdDb);
    chain.bind("ratio", compressor, &CompressorProcessor::setRatio);
    chain.bind("attack", compressor, &CompressorProcessor::setAttackMs);
    chain.bind("release", compressor, &CompressorProcessor::setReleaseMs);
    chain.bind("makeup", compressor, &CompressorProcessor::setMakeupLinear);
}

// texture.tape: the same compressor, but its manifest stores a linear threshold.
void buildTape(ChainProcessor &chain)
{
    auto *compressor = chain.addStage<CompressorProcessor>();
    chain.bind("threshold",
               [compressor](float v) { compressor->setThresholdDb(linearToDb(v)); });
    chain.bind("ratio", compressor, &CompressorProcessor::setRatio);
    chain.bind("attack", compressor, &CompressorProcessor::setAttackMs);
    chain.bind("release", compressor, &CompressorProcessor::setReleaseMs);
}

void buildLimiter(ChainProcessor &chain)
{
    auto *limiter = chain.addStage<LimiterProcessor>();
    chain.bind("drive", limiter, &LimiterProcessor::setDriveLinear);
    chain.bind("ceiling", limiter, &LimiterProcessor::setCeilingLinear);
}

void buildGate(ChainProcessor &chain)
{
    auto *gate = chain.addStage<GateProcessor>();
    chain.bind("threshold", gate, &GateProcessor::setThresholdLinear);
    chain.bind("ratio", gate, &GateProcessor::setRatio);
    chain.bind("attack", gate, &GateProcessor::setAttackMs);
    chain.bind("release", gate, &GateProcessor::setReleaseMs);
}

void buildDeEsser(ChainProcessor &chain)
{
    auto *deesser = chain.addStage<DeEsserProcessor>();
    chain.bind("intensity", deesser, &DeEsserProcessor::setIntensity);
    chain.bind("amount", deesser, &DeEsserProcessor::setAmount);
    chain.bind("frequency", deesser, &DeEsserProcessor::setFrequency);
}

void buildLeveler(ChainProcessor &chain)
{
    auto *leveler = chain.addStage<LevelerProcessor>();
    chain.bind("strength", leveler, &LevelerProcessor::setStrength);
    chain.bind("peak", leveler, &LevelerProcessor::setPeak);
}

// ---- voice ------------------------------------------------------------------------------

void buildPitch(ChainProcessor &chain)
{
    auto *pitch = chain.addStage<SoundTouchPitchProcessor>();
    chain.bind("pitch", pitch, &SoundTouchPitchProcessor::setRatio);
}

// voice.vader: asetrate/aresample/atempo + aecho=0.8:0.9 + acrusher=bits=10.
void buildDarkLord(ChainProcessor &chain)
{
    auto *pitch = chain.addStage<SoundTouchPitchProcessor>();
    auto *echo = chain.addStage<EchoProcessor>();
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 10.0f);

    echo->setInGain(0.8f);
    echo->setOutGain(0.9f);

    chain.bind("pitch", pitch, &SoundTouchPitchProcessor::setRatio);
    chain.bind("echo_delay", echo, &EchoProcessor::setDelayMs);
    chain.bind("echo_decay", echo, &EchoProcessor::setDecay);
    chain.bind("grit", crusher, &BitCrusherProcessor::setMix);
}

// space.tremolo uses "rate", voice.robot uses "freq"; both drive the same LFO.
void buildTremolo(ChainProcessor &chain)
{
    auto *tremolo = chain.addStage<TremoloProcessor>();
    chain.bind("rate", tremolo, &TremoloProcessor::setRate);
    chain.bind("freq", tremolo, &TremoloProcessor::setRate);
    chain.bind("depth", tremolo, &TremoloProcessor::setDepth);
}

void buildVibrato(ChainProcessor &chain)
{
    auto *vibrato = chain.addStage<VibratoProcessor>();
    chain.bind("rate", vibrato, &VibratoProcessor::setRate);
    chain.bind("depth", vibrato, &VibratoProcessor::setDepth);
}

// voice.alien binds speed/delay/depth/regen; space.flanger adds rate/mix/phase/invert.
void buildFlanger(ChainProcessor &chain)
{
    auto *flanger = chain.addStage<FlangerProcessor>();
    chain.bind("speed", flanger, &FlangerProcessor::setRate);
    chain.bind("rate", flanger, &FlangerProcessor::setRate);
    chain.bind("delay", flanger, &FlangerProcessor::setDelayMs);
    chain.bind("depth", flanger, &FlangerProcessor::setDepthMs);
    chain.bind("regen", flanger, &FlangerProcessor::setRegenPercent);
    chain.bind("mix", flanger, &FlangerProcessor::setWidthPercent);
    chain.bind("phase", flanger, &FlangerProcessor::setPhaseDegrees);
    chain.bind("invert", flanger, &FlangerProcessor::setInvertRight);
}

// ---- transmission -----------------------------------------------------------------------

void buildBandLimit(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 300.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 3400.0f);
    chain.bind("low_cut", highPass, &BandFilterProcessor::setFrequency);
    chain.bind("high_cut", lowPass, &BandFilterProcessor::setFrequency);
}

void buildWalkie(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 400.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 3000.0f);
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 6.0f);
    chain.bind("low_cut", highPass, &BandFilterProcessor::setFrequency);
    chain.bind("high_cut", lowPass, &BandFilterProcessor::setFrequency);
    chain.bind("grit", crusher, &BitCrusherProcessor::setMix);
}

void buildMegaphone(ChainProcessor &chain)
{
    auto *band = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::BandPass, 1500.0f);
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 8.0f);
    chain.bind("center", band, &BandFilterProcessor::setFrequency);
    chain.bind("width", band, &BandFilterProcessor::setWidthHz);
    chain.bind("grit", crusher, &BitCrusherProcessor::setMix);
}

// transmission.underwater: lowpass + chorus={wet}:0.9:50:0.4:{motion}:2.
void buildUnderwater(ChainProcessor &chain)
{
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 500.0f);
    auto *chorus = chain.addStage<ChorusProcessor>();

    chorus->setOutGain(0.9f);
    chorus->setDelayMs(50.0f);
    chorus->setDecay(0.4f);
    chorus->setDepthMs(2.0f);

    chain.bind("cutoff", lowPass, &BandFilterProcessor::setFrequency);
    chain.bind("wet", chorus, &ChorusProcessor::setInGain);
    chain.bind("motion", chorus, &ChorusProcessor::setRate);
}

void buildMuffled(ChainProcessor &chain)
{
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 800.0f);
    auto *gain = chain.addStage<GainProcessor>();
    chain.bind("cutoff", lowPass, &BandFilterProcessor::setFrequency);
    chain.bind("gain", gain, &GainProcessor::setGain);
}

// ---- texture ----------------------------------------------------------------------------

Builder makeBitCrushBuilder(BitCrusherProcessor::Mode mode)
{
    return [mode](ChainProcessor &chain) {
        auto *crusher = chain.addStage<BitCrusherProcessor>(mode);
        chain.bind("bits", crusher, &BitCrusherProcessor::setBits);
        chain.bind("samples", crusher, &BitCrusherProcessor::setSampleReduction);
        chain.bind("mix", crusher, &BitCrusherProcessor::setMix);
    };
}

void buildVinyl(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 200.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 6000.0f);
    auto *vibrato = chain.addStage<VibratoProcessor>();
    chain.bind("highpass", highPass, &BandFilterProcessor::setFrequency);
    chain.bind("lowpass", lowPass, &BandFilterProcessor::setFrequency);
    chain.bind("wobble", vibrato, &VibratoProcessor::setRate);
    chain.bind("flutter", vibrato, &VibratoProcessor::setDepth);
}

void buildCrystalizer(ChainProcessor &chain)
{
    auto *crystal = chain.addStage<CrystalizerProcessor>();
    chain.bind("intensity", crystal, &CrystalizerProcessor::setIntensity);
    chain.bind("colors", crystal, &CrystalizerProcessor::setColors);
}

// ---- space ------------------------------------------------------------------------------

void buildEcho(ChainProcessor &chain)
{
    auto *echo = chain.addStage<EchoProcessor>();
    chain.bind("delay", echo, &EchoProcessor::setDelayMs);
    chain.bind("decay", echo, &EchoProcessor::setDecay);
    chain.bind("in_gain", echo, &EchoProcessor::setInGain);
    chain.bind("out_gain", echo, &EchoProcessor::setOutGain);
}

void buildChorus(ChainProcessor &chain)
{
    auto *chorus = chain.addStage<ChorusProcessor>();
    chain.bind("in_gain", chorus, &ChorusProcessor::setInGain);
    chain.bind("out_gain", chorus, &ChorusProcessor::setOutGain);
    chain.bind("delay", chorus, &ChorusProcessor::setDelayMs);
    chain.bind("decay", chorus, &ChorusProcessor::setDecay);
    chain.bind("speed", chorus, &ChorusProcessor::setRate);
    chain.bind("depth", chorus, &ChorusProcessor::setDepthMs);
}

void buildPhaser(ChainProcessor &chain)
{
    auto *phaser = chain.addStage<PhaserProcessor>();
    chain.bind("in_gain", phaser, &PhaserProcessor::setInGain);
    chain.bind("out_gain", phaser, &PhaserProcessor::setOutGain);
    chain.bind("delay", phaser, &PhaserProcessor::setDelayMs);
    chain.bind("decay", phaser, &PhaserProcessor::setDecay);
    chain.bind("speed", phaser, &PhaserProcessor::setRate);
}

void buildAutoPan(ChainProcessor &chain)
{
    auto *pan = chain.addStage<AutoPanProcessor>();
    chain.bind("rate", pan, &AutoPanProcessor::setRate);
    chain.bind("amount", pan, &AutoPanProcessor::setAmount);
    chain.bind("level_in", pan, &AutoPanProcessor::setLevelIn);
    chain.bind("level_out", pan, &AutoPanProcessor::setLevelOut);
}

void buildStereoWiden(ChainProcessor &chain)
{
    auto *widen = chain.addStage<StereoWidenProcessor>();
    chain.bind("delay", widen, &StereoWidenProcessor::setDelayMs);
    chain.bind("feedback", widen, &StereoWidenProcessor::setFeedback);
    chain.bind("crossfeed", widen, &StereoWidenProcessor::setCrossfeed);
    chain.bind("drymix", widen, &StereoWidenProcessor::setDryMix);
}

const std::unordered_map<std::string, Builder> &registry()
{
    static const std::unordered_map<std::string, Builder> builders{
        {"eq3", buildEq3},
        {"compressor", buildCompressor},
        {"tape", buildTape},
        {"limiter", buildLimiter},
        {"gate", buildGate},
        {"deesser", buildDeEsser},
        {"leveler", buildLeveler},
        {"pitch", buildPitch},
        {"darklord", buildDarkLord},
        {"tremolo", buildTremolo},
        {"vibrato", buildVibrato},
        {"flanger", buildFlanger},
        {"bandlimit", buildBandLimit},
        {"walkie", buildWalkie},
        {"megaphone", buildMegaphone},
        {"underwater", buildUnderwater},
        {"muffled", buildMuffled},
        {"bitcrush", makeBitCrushBuilder(BitCrusherProcessor::Mode::Linear)},
        {"bitcrush_log", makeBitCrushBuilder(BitCrusherProcessor::Mode::Logarithmic)},
        {"vinyl", buildVinyl},
        {"crystalizer", buildCrystalizer},
        {"echo", buildEcho},
        {"chorus", buildChorus},
        {"phaser", buildPhaser},
        {"autopan", buildAutoPan},
        {"stereowiden", buildStereoWiden},
    };
    return builders;
}

} // namespace

std::unique_ptr<ChainProcessor> createProcessor(std::string_view processorId)
{
    const auto it = registry().find(std::string(processorId));
    if (it == registry().end())
        return nullptr;

    auto chain = std::make_unique<ChainProcessor>();
    it->second(*chain);
    return chain;
}

bool hasProcessor(std::string_view processorId)
{
    return registry().count(std::string(processorId)) > 0;
}

std::vector<std::string> processorIds()
{
    std::vector<std::string> ids;
    for (const auto &[id, builder] : registry())
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool bindsParameter(std::string_view processorId, std::string_view paramId)
{
    const auto chain = createProcessor(processorId);
    return chain && chain->parameterIndex(paramId) >= 0;
}

} // namespace drift::audiofx
