#include "engine/MediaProbe.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

QString typeName(StreamInfo::Type type)
{
    switch (type) {
    case StreamInfo::Type::Video:
        return QStringLiteral("video");
    case StreamInfo::Type::Audio:
        return QStringLiteral("audio");
    case StreamInfo::Type::Subtitle:
        return QStringLiteral("subtitle");
    default:
        return QStringLiteral("other");
    }
}

QString formatDuration(int64_t us)
{
    const double seconds = us / 1'000'000.0;
    return QString::number(seconds, 'f', 3) + QStringLiteral("s");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    if (args.size() != 2) {
        err << "usage: probe <media-file>\n";
        return 1;
    }

    const MediaInfo info = MediaProbe::probe(args.at(1));
    if (!info.ok) {
        err << "probe failed: " << info.errorString << "\n";
        return 1;
    }

    out << "file: " << info.path << "\n";
    out << "duration: " << formatDuration(info.durationUs) << "\n";
    out << "streams: " << info.streams.size() << "\n";

    for (int i = 0; i < info.streams.size(); ++i) {
        const StreamInfo &s = info.streams.at(i);
        out << "  [" << i << "] " << typeName(s.type) << " codec=" << s.codecName
            << " duration=" << formatDuration(s.durationUs);
        if (s.type == StreamInfo::Type::Video) {
            out << " " << s.width << "x" << s.height << " fps=" << QString::number(s.fps, 'f', 2)
                << " rotation=" << s.rotationDegrees;
        } else if (s.type == StreamInfo::Type::Audio) {
            out << " sampleRate=" << s.sampleRate << " channels=" << s.channels;
        }
        out << "\n";
    }

    return 0;
}
