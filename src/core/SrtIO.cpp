#include "SrtIO.h"

#include "Time.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace drift {
namespace {

QString formatSrtTimestamp(TimeUs us)
{
    if (us < 0)
        us = 0;
    const qint64 totalMs = us / kUsPerMs;
    const qint64 hours = totalMs / 3'600'000;
    const qint64 minutes = (totalMs / 60'000) % 60;
    const qint64 seconds = (totalMs / 1'000) % 60;
    const qint64 millis = totalMs % 1'000;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

bool parseSrtTimestamp(QStringView token, TimeUs *outUs)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(\d{1,2}):(\d{2}):(\d{2})[,.](\d{1,3})\s*$)"));
    const auto match = re.matchView(token);
    if (!match.hasMatch())
        return false;

    const qint64 hours = match.captured(1).toLongLong();
    const qint64 minutes = match.captured(2).toLongLong();
    const qint64 seconds = match.captured(3).toLongLong();
    QString msStr = match.captured(4);
    while (msStr.size() < 3)
        msStr.append(QLatin1Char('0'));
    if (msStr.size() > 3)
        msStr = msStr.left(3);
    const qint64 millis = msStr.toLongLong();
    if (minutes >= 60 || seconds >= 60 || millis >= 1000)
        return false;

    *outUs = ((hours * 3600 + minutes * 60 + seconds) * kUsPerSecond) + (millis * kUsPerMs);
    return true;
}

QString normalizeNewlines(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

bool isIndexLine(const QString &line)
{
    if (line.isEmpty())
        return false;
    for (const QChar ch : line) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

} // namespace

bool parseSrt(const QString &content, QList<SubtitleCue> *outCues, QString *error)
{
    if (!outCues) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "Missing output");
        return false;
    }

    const QString text = normalizeNewlines(content).trimmed();
    if (text.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "Subtitle file is empty");
        return false;
    }

    static const QRegularExpression timingRe(
        QStringLiteral(R"(^\s*(.+?)\s*-->\s*(.+?)\s*(?:X1:.*)?$)"));

    QList<SubtitleCue> cues;
    const QStringList blocks = text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &rawBlock : blocks) {
        QStringList lines = rawBlock.split(QLatin1Char('\n'));
        while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
            lines.removeFirst();
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        if (lines.isEmpty())
            continue;

        int timingLine = 0;
        if (isIndexLine(lines.first().trimmed()) && lines.size() >= 2)
            timingLine = 1;

        const auto match = timingRe.match(lines.at(timingLine).trimmed());
        if (!match.hasMatch()) {
            if (error)
                *error = QCoreApplication::translate("SrtIO", "Invalid subtitle timing line");
            return false;
        }

        TimeUs startUs = 0;
        TimeUs endUs = 0;
        if (!parseSrtTimestamp(match.capturedView(1), &startUs)
            || !parseSrtTimestamp(match.capturedView(2), &endUs)) {
            if (error)
                *error = QCoreApplication::translate("SrtIO", "Invalid subtitle timestamp");
            return false;
        }
        if (endUs <= startUs)
            endUs = startUs + kUsPerMs;

        QStringList body;
        for (int i = timingLine + 1; i < lines.size(); ++i)
            body.append(lines.at(i));
        // Keep intentional blank lines inside a cue, but trim the whole body.
        while (!body.isEmpty() && body.first().trimmed().isEmpty())
            body.removeFirst();
        while (!body.isEmpty() && body.last().trimmed().isEmpty())
            body.removeLast();

        SubtitleCue cue;
        cue.startUs = startUs;
        cue.endUs = endUs;
        cue.text = body.join(QLatin1Char('\n'));
        cues.append(cue);
    }

    if (cues.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "No subtitle cues found");
        return false;
    }

    sortSubtitleCues(cues);
    *outCues = std::move(cues);
    return true;
}

bool parseSrtFile(const QString &path, QList<SubtitleCue> *outCues, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "Could not open subtitle file");
        return false;
    }

    const QByteArray bytes = file.readAll();
    // Prefer UTF-8 (with or without BOM); fall back to Latin-1 for older files.
    QString content = QString::fromUtf8(bytes);
    if (content.contains(QChar::ReplacementCharacter))
        content = QString::fromLatin1(bytes);
    return parseSrt(content, outCues, error);
}

QString writeSrt(const QList<SubtitleCue> &cues)
{
    QList<SubtitleCue> sorted = cues;
    sortSubtitleCues(sorted);

    QString out;
    int index = 1;
    for (const SubtitleCue &cue : sorted) {
        if (cue.endUs <= cue.startUs && cue.text.trimmed().isEmpty())
            continue;
        TimeUs endUs = cue.endUs;
        if (endUs <= cue.startUs)
            endUs = cue.startUs + kUsPerMs;

        out += QString::number(index++);
        out += QLatin1Char('\n');
        out += formatSrtTimestamp(cue.startUs);
        out += QStringLiteral(" --> ");
        out += formatSrtTimestamp(endUs);
        out += QLatin1Char('\n');
        out += cue.text;
        out += QStringLiteral("\n\n");
    }
    return out;
}

bool writeSrtFile(const QString &path, const QList<SubtitleCue> &cues, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "Could not write subtitle file");
        return false;
    }

    const QByteArray bytes = writeSrt(cues).toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = QCoreApplication::translate("SrtIO", "Could not write subtitle file");
        return false;
    }
    return true;
}

} // namespace drift
