#include "app/crash/LogScrubber.h"

#include <QDir>
#include <QRegularExpression>

namespace drift::crash {

namespace {

// Control characters never occur in a log line, so they can stand in for a root that contains
// spaces ("C:\Program Files\Drift") while the path pattern, which stops at whitespace, runs.
const QChar kApp(0x01);
const QChar kHome(0x02);

void replaceRoot(QString &text, const QString &root, QChar marker)
{
    if (root.isEmpty())
        return;
    const QString native = QDir::toNativeSeparators(root);
    const QString portable = QDir::fromNativeSeparators(root);
    for (const QString &variant : {native, portable}) {
        if (!variant.isEmpty())
            text.replace(variant, QString(marker), Qt::CaseInsensitive);
    }
}

QString lastPathComponent(const QString &path, qsizetype *start)
{
    const qsizetype slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
    *start = slash + 1;
    return path.mid(slash + 1);
}

QString fileLabel(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return QString();
    return QStringLiteral("<file>") + name.mid(dot);
}

QString rewritePath(const QString &path)
{
    if (path.size() == 1)
        return path;
    qsizetype nameStart = 0;
    const QString name = lastPathComponent(path, &nameStart);
    const QString label = fileLabel(name);
    const QString directory = path.left(nameStart);

    if (path.startsWith(kApp)) {
        // Inside the install: the layout is the app's own, not the user's.
        if (label.isEmpty())
            return path;
        return directory + label;
    }
    const QString root = path.startsWith(kHome) ? QStringLiteral("<user>/...") : QStringLiteral("<path>");
    return label.isEmpty() ? root : root + QLatin1Char('/') + label;
}

} // namespace

QString scrubLog(const QString &text, const ScrubContext &context)
{
    static const QRegularExpression email(
        QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)+"));
    // Drive paths, root-relative paths with at least two components (so "and/or" survives, and
    // "qrc:/..." and URLs are skipped by the look-behind), and the root markers above.
    static const QRegularExpression path(QStringLiteral(
        "[\\x01\\x02](?:[\\\\/][^\\s\"'<>|*?\\x01\\x02]*)?"
        "|(?<![A-Za-z])[A-Za-z]:[\\\\/][^\\s\"'<>|*?]*"
        "|(?<![\\w:/.])/(?:[^\\s\"'<>|:*?/]+/)+[^\\s\"'<>|:*?/]+"));

    QString out = text;
    out.replace(email, QStringLiteral("<email>"));

    // The install first: it may sit under the home folder.
    replaceRoot(out, context.appDir, kApp);
    replaceRoot(out, context.homeDir, kHome);

    QString rewritten;
    rewritten.reserve(out.size());
    qsizetype last = 0;
    auto matches = path.globalMatch(out);
    while (matches.hasNext()) {
        const auto match = matches.next();
        rewritten += QStringView(out).mid(last, match.capturedStart() - last);
        rewritten += rewritePath(match.captured());
        last = match.capturedEnd();
    }
    rewritten += QStringView(out).mid(last);
    out = rewritten;

    // The install's own location is public, unless it sits under the home folder.
    QString appShown = context.appDir;
    replaceRoot(appShown, context.homeDir, kHome);
    appShown.replace(kHome, QStringLiteral("<user>"));
    out.replace(kApp, appShown);
    out.replace(kHome, QStringLiteral("<user>"));
    if (context.userName.size() >= 2)
        out.replace(context.userName, QStringLiteral("<user>"), Qt::CaseInsensitive);
    return out;
}

QByteArray tailOfLog(const QString &text, qsizetype maxBytes)
{
    const QByteArray bytes = text.toUtf8();
    if (bytes.size() <= maxBytes)
        return bytes;
    QByteArray tail = bytes.right(maxBytes);
    const qsizetype newline = tail.indexOf('\n');
    return newline >= 0 ? tail.mid(newline + 1) : tail;
}

} // namespace drift::crash
