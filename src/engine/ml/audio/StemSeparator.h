#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

// Splitting a mixture into stems — vocals and accompaniment at a minimum, which is what "isolate
// the voice" and "remove the vocals" both come down to — on ONNX Runtime.
//
// The network is not baked in. Stem separators of this family all have the same shape: a
// spectrogram goes in, one magnitude estimate per stem comes out, and the estimates become ratio
// masks over the mixture. So the model directory declares its own geometry in config.json and
// this class reads it:
//
//   {
//     "stems":      ["vocals", "accompaniment"],   // output order, >= 1
//     "sampleRate": 44100,
//     "fftSize":    4096,
//     "hop":        1024,
//     "modelBins":  1024,       // leading bins the graph sees; the rest are handled below
//     "frames":     512,        // time frames per inference window
//     "channels":   2,          // channels the graph takes (1 runs each channel separately)
//     "input":      "mix",      // input tensor name
//     "outputs":    ["vocals", "accompaniment"]    // one magnitude tensor per stem, same order
//   }
//
// That keeps the choice of network a packaging decision rather than a code change, the way the
// ONNX Runtime itself and every other model here already are.
//
// Masking: the per-stem magnitudes are turned into ratio masks, mask_i = mag_i / sum(mag), so the
// stems sum back to the mixture and nothing is lost or doubled. With a single declared stem the
// other side is its complement. Bins above `modelBins` — above about 11 kHz for a 4096-point
// transform at 44.1 kHz — are not predicted at all; each stem takes an equal share of them, which
// keeps that sum exact at the cost of a quiet copy of the mixture's top end in every stem. The
// alternative the reference implementations take is to discard those bins, which makes an isolated
// voice sound lowpassed and a backing track sound dull, and still loses the content for good.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class StemSeparator
{
public:
    static StemSeparator &instance();

    // Cheap file-existence check that constructs no ONNX session. UI gating must use this.
    static bool modelPresent();

    // Resolves the model directory and loads the session on first use. False when the model is
    // missing or malformed (see lastError()). Blocks; never call it from the GUI thread.
    bool available();
    QString lastError() const;

    // Valid once available() has returned true. Before that they report the defaults, which is
    // enough for a caller to resample ahead of asking.
    int sampleRate() const;
    // Declared stem names, in output order. A caller showing these to a user should translate
    // them; they are identifiers, not copy.
    QStringList stems() const;

    // Separates interleaved-by-channel audio: `channels` buffers of equal length, at
    // sampleRate(). Returns one entry per stem, each with the same channel count and length as
    // the input, or an empty vector when the model is unavailable or the caller cancelled.
    //
    // progress(fraction in [0,1]) returns false to request cancel.
    std::vector<std::vector<std::vector<float>>>
    separate(const std::vector<std::vector<float>> &channels,
             const std::function<bool(double)> &progress);

    StemSeparator(const StemSeparator &) = delete;
    StemSeparator &operator=(const StemSeparator &) = delete;

private:
    StemSeparator();
    ~StemSeparator();

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
