#include "engine/audio/WavReader.h"

#include <cstring>

namespace drift::audiofx {

namespace {

uint32_t le32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t le16(const uint8_t *p)
{
    return uint16_t(p[0] | (p[1] << 8));
}

bool fail(std::string *error, const char *message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

bool readWav(const uint8_t *data, size_t size, IrData *out, std::string *error)
{
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return fail(error, "not a WAV file");

    uint16_t format = 0;
    int channels = 0;
    int bits = 0;
    uint32_t rate = 0;
    const uint8_t *pcm = nullptr;
    size_t pcmSize = 0;

    for (size_t pos = 12; pos + 8 <= size;) {
        const uint8_t *chunk = data + pos;
        const size_t chunkSize = le32(chunk + 4);
        const size_t body = pos + 8;
        if (chunkSize > size - body)
            return fail(error, "truncated WAV chunk");

        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = le32(chunk + 12);
            bits = le16(chunk + 22);
            // WAVE_FORMAT_EXTENSIBLE keeps the real format in the sub-format GUID's first word.
            if (format == 0xFFFE && chunkSize >= 40)
                format = le16(chunk + 32);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            pcm = data + body;
            pcmSize = chunkSize;
        }
        pos = body + chunkSize + (chunkSize & 1);
    }

    if (!pcm || channels == 0)
        return fail(error, "WAV has no fmt or data chunk");
    if (channels > 2)
        return fail(error, "WAV must be mono or stereo");
    if (rate < 8000 || rate > 384000)
        return fail(error, "unsupported WAV sample rate");
    const bool isFloat = format == 3 && bits == 32;
    const bool isPcm = format == 1 && (bits == 16 || bits == 24 || bits == 32);
    if (!isFloat && !isPcm)
        return fail(error, "WAV must be 16/24/32-bit PCM or 32-bit float");

    const int bytes = bits / 8;
    const size_t frames = pcmSize / (size_t(bytes) * size_t(channels));
    out->channels = channels;
    out->frames = int(frames);
    out->sampleRate = rate;
    out->samples.resize(frames * size_t(channels));

    for (size_t i = 0; i < out->samples.size(); ++i) {
        const uint8_t *p = pcm + i * size_t(bytes);
        float v = 0.0f;
        if (isFloat) {
            const uint32_t word = le32(p);
            std::memcpy(&v, &word, sizeof v);
        } else if (bits == 16) {
            v = float(int16_t(le16(p))) / 32768.0f;
        } else if (bits == 24) {
            const int32_t s = int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8;
            v = float(s) / 8388608.0f;
        } else {
            v = float(double(int32_t(le32(p))) / 2147483648.0);
        }
        out->samples[i] = v;
    }
    return true;
}

} // namespace drift::audiofx
