// Headless smoke test for Chatterbox text-to-speech.
// Usage: tts [--lang CODE] [--voice ref.wav|.flac] [--exaggeration 0.5] "text" out.flac
//        Without --voice the model's bundled default voice is used.

#include "engine/AudioFileWriter.h"
#include "engine/ChatterboxTts.h"
#include "engine/SpeechAudio.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString lang = QStringLiteral("en");
    QString voicePath;
    float exaggeration = 0.5f;
    QStringList positional;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--lang") && i + 1 < args.size())
            lang = args.at(++i);
        else if (args.at(i) == QLatin1String("--voice") && i + 1 < args.size())
            voicePath = args.at(++i);
        else if (args.at(i) == QLatin1String("--exaggeration") && i + 1 < args.size())
            exaggeration = args.at(++i).toFloat();
        else
            positional << args.at(i);
    }
    if (positional.size() != 2) {
        fprintf(stderr, "usage: tts [--lang CODE] [--voice ref.wav] [--exaggeration 0.5] \"text\" out.flac\n");
        return 1;
    }

    drift::ChatterboxTts tts;
    QString err;
    QElapsedTimer timer;
    timer.start();
    if (!tts.load(&err)) {
        fprintf(stderr, "load failed: %s\n", qPrintable(err));
        return 1;
    }
    fprintf(stderr, "load %.2fs\n", timer.restart() / 1000.0);

    if (voicePath.isEmpty())
        voicePath = QDir(tts.modelDir()).filePath(QStringLiteral("default_voice.wav"));
    const std::vector<float> ref = drift::readMono(voicePath, 0, -1, drift::ChatterboxTts::kSampleRate);
    if (ref.empty()) {
        fprintf(stderr, "could not read voice %s\n", qPrintable(voicePath));
        return 1;
    }
    const auto voice = tts.encodeVoice(ref, &err);
    if (!voice) {
        fprintf(stderr, "encode failed: %s\n", qPrintable(err));
        return 1;
    }
    fprintf(stderr, "encode %.2fs (%zu ref samples)\n", timer.restart() / 1000.0, ref.size());

    const std::vector<float> wav = tts.synthesize(
        positional.at(0), lang, *voice, exaggeration,
        [](double p) {
            fprintf(stderr, "\r%d%%", int(p * 100));
            return true;
        },
        &err);
    fprintf(stderr, "\n");
    if (wav.empty()) {
        fprintf(stderr, "synthesis failed: %s\n", qPrintable(err));
        return 1;
    }
    const double synthSec = timer.restart() / 1000.0;
    const double audioSec = double(wav.size()) / drift::ChatterboxTts::kSampleRate;
    fprintf(stderr, "synth %.2fs for %.2fs audio, RTF %.2f\n", synthSec, audioSec, synthSec / audioSec);

    drift::AudioFileWriter writer;
    if (!writer.open(positional.at(1), drift::ChatterboxTts::kSampleRate, 1, &err)
        || !writer.writeFrames(wav.data(), int(wav.size()), &err) || !writer.finish(&err)) {
        fprintf(stderr, "write failed: %s\n", qPrintable(err));
        return 1;
    }
    return 0;
}
