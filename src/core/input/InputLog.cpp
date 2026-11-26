#include "core/input/InputLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace drift {

namespace {

// Bumped only on a change old readers could misread. New optional fields do not need it.
constexpr int kFormatVersion = 1;

} // namespace

QString inputEventKindToString(InputEventKind kind)
{
    switch (kind) {
    case InputEventKind::Release:
        return QStringLiteral("release");
    case InputEventKind::Key:
        return QStringLiteral("key");
    case InputEventKind::Press:
        break;
    }
    return QStringLiteral("press");
}

InputEventKind inputEventKindFromString(const QString &text)
{
    if (text == QLatin1String("release"))
        return InputEventKind::Release;
    if (text == QLatin1String("key"))
        return InputEventKind::Key;
    return InputEventKind::Press;
}

QList<InputEvent> InputLog::presses() const
{
    QList<InputEvent> out;
    for (const InputEvent &event : events) {
        if (event.kind == InputEventKind::Press)
            out.append(event);
    }
    return out;
}

QJsonObject inputLogToJson(const InputLog &log)
{
    QJsonArray events;
    for (const InputEvent &event : log.events) {
        QJsonObject item{{QStringLiteral("kind"), inputEventKindToString(event.kind)},
                         {QStringLiteral("t"), qint64(event.timeUs)}};
        if (event.kind != InputEventKind::Key) {
            item.insert(QStringLiteral("x"), event.position.x());
            item.insert(QStringLiteral("y"), event.position.y());
            if (event.button != 0)
                item.insert(QStringLiteral("button"), event.button);
        } else if (!event.label.isEmpty()) {
            item.insert(QStringLiteral("label"), event.label);
        }
        events.append(item);
    }
    return QJsonObject{{QStringLiteral("version"), kFormatVersion},
                       {QStringLiteral("events"), events}};
}

InputLog inputLogFromJson(const QJsonObject &object)
{
    InputLog log;
    const QJsonArray events = object.value(QStringLiteral("events")).toArray();
    log.events.reserve(events.size());
    for (const QJsonValue &value : events) {
        const QJsonObject item = value.toObject();
        InputEvent event;
        event.kind = inputEventKindFromString(item.value(QStringLiteral("kind")).toString());
        event.timeUs = TimeUs(item.value(QStringLiteral("t")).toInteger(0));
        event.position = QPointF(item.value(QStringLiteral("x")).toDouble(),
                                 item.value(QStringLiteral("y")).toDouble());
        event.button = item.value(QStringLiteral("button")).toInt(0);
        event.label = item.value(QStringLiteral("label")).toString();
        log.events.append(event);
    }
    // Sorted on read rather than trusted: the writer appends as events arrive, and a platform that
    // batches them can hand over a pair out of order. Everything downstream assumes time order.
    std::stable_sort(log.events.begin(), log.events.end(),
                     [](const InputEvent &a, const InputEvent &b) { return a.timeUs < b.timeUs; });
    return log;
}

bool writeInputLog(const QString &path, const InputLog &log, QString *errorOut)
{
    if (path.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("The input log has nowhere to go");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(inputLogToJson(log)).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not finish writing %1").arg(path);
        return false;
    }
    return true;
}

bool readInputLog(const QString &path, InputLog *out, QString *errorOut)
{
    if (!out)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not open %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("%1 is not a valid input log").arg(path);
        return false;
    }
    const QJsonObject root = document.object();
    // Reject the future, accept the past, as the face track does: a newer writer may have changed
    // what a field means, and guessing would put rings in the wrong places.
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > kFormatVersion) {
        if (errorOut)
            *errorOut = QStringLiteral("%1 has an unsupported format version").arg(path);
        return false;
    }
    *out = inputLogFromJson(root);
    return true;
}

QString inputLogPathFor(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return {};
    const QFileInfo info(videoPath);
    const QString stem = info.completeBaseName();
    if (stem.isEmpty())
        return {};
    return info.dir().filePath(stem + QStringLiteral(".input.json"));
}

} // namespace drift
