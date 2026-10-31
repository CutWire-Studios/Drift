#include "app/mcp/AgentImageLog.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int kMaxEntries = 200;
constexpr int kMaxArgsChars = 200;
constexpr qint64 kStaleSessionSecs = 24 * 3600;

QString extensionFor(const QString &mime)
{
    if (mime == QLatin1String("image/png"))
        return QStringLiteral("png");
    if (mime == QLatin1String("image/webp"))
        return QStringLiteral("webp");
    return QStringLiteral("jpg");
}

bool isImagePath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png") || suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");
}
} // namespace

AgentImageLog::AgentImageLog(QObject *parent)
    : QAbstractListModel(parent)
{
}

AgentImageLog::~AgentImageLog()
{
    if (!m_dir.isEmpty())
        QDir(m_dir).removeRecursively();
}

int AgentImageLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant AgentImageLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case EntryIdRole: return e.id;
    case CallIdRole: return e.callId;
    case ToolRole: return e.tool;
    case ArgsRole: return e.args;
    case TimeRole: return e.time;
    case PathRole: return e.path;
    case UrlRole: return QUrl::fromLocalFile(e.path);
    case WidthRole: return e.width;
    case HeightRole: return e.height;
    case TimesRole: {
        QVariantList list;
        for (double t : e.times)
            list.append(t);
        return list;
    }
    case RangeStartRole: return e.rangeStart;
    case RangeEndRole: return e.rangeEnd;
    }
    return {};
}

QHash<int, QByteArray> AgentImageLog::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},
        {CallIdRole, "callId"},
        {ToolRole, "tool"},
        {ArgsRole, "args"},
        {TimeRole, "time"},
        {PathRole, "path"},
        {UrlRole, "url"},
        {WidthRole, "imageWidth"},
        {HeightRole, "imageHeight"},
        {TimesRole, "times"},
        {RangeStartRole, "rangeStart"},
        {RangeEndRole, "rangeEnd"},
    };
}

QString AgentImageLog::sessionDir() const
{
    if (!m_dir.isEmpty())
        return m_dir;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        return {};
    const QDir root(QDir(base).filePath(QStringLiteral("agent-images")));
    if (!QDir().mkpath(root.path()))
        return {};
    const QDateTime now = QDateTime::currentDateTime();
    for (const QFileInfo &old : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (old.lastModified().secsTo(now) > kStaleSessionSecs)
            QDir(old.absoluteFilePath()).removeRecursively();
    }
    const QString dir = root.filePath(QString::number(QCoreApplication::applicationPid()));
    if (!QDir().mkpath(dir))
        return {};
    m_dir = dir;
    return m_dir;
}

void AgentImageLog::add(Entry entry)
{
    entry.id = m_nextId++;
    beginInsertRows({}, 0, 0);
    m_entries.prepend(entry);
    endInsertRows();
    while (m_entries.size() > kMaxEntries) {
        const int last = static_cast<int>(m_entries.size()) - 1;
        beginRemoveRows({}, last, last);
        const Entry gone = m_entries.takeLast();
        endRemoveRows();
        if (gone.owned)
            QFile::remove(gone.path);
    }
    emit countChanged();
}

void AgentImageLog::recordResult(int callId, const QString &tool, const QJsonObject &args, const QJsonObject &result)
{
    if (result.value(QStringLiteral("isError")).toBool())
        return;
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QJsonObject meta;
    for (const QJsonValue &v : content) {
        if (v.toObject().value(QStringLiteral("type")).toString() != QLatin1String("text"))
            continue;
        meta = QJsonDocument::fromJson(v.toObject().value(QStringLiteral("text")).toString().toUtf8()).object();
        break;
    }

    Entry base;
    base.callId = callId;
    base.tool = tool;
    QString summary = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
    if (summary.size() > kMaxArgsChars)
        summary = summary.left(kMaxArgsChars - 1) + QChar(0x2026);
    base.args = summary;
    base.time = QDateTime::currentMSecsSinceEpoch();

    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    for (const QJsonValue &f : frames) {
        const QJsonObject row = f.toObject();
        const QJsonValue t = row.contains(QStringLiteral("tl")) ? row.value(QStringLiteral("tl")) : row.value(QStringLiteral("t"));
        if (t.isDouble())
            base.times.append(t.toDouble());
    }
    if (base.times.isEmpty() && meta.value(QStringLiteral("at")).isDouble())
        base.times.append(meta.value(QStringLiteral("at")).toDouble());
    if (!base.times.isEmpty()) {
        base.rangeStart = *std::min_element(base.times.begin(), base.times.end());
        base.rangeEnd = *std::max_element(base.times.begin(), base.times.end());
    } else if (meta.value(QStringLiteral("start")).isDouble()) {
        base.rangeStart = meta.value(QStringLiteral("start")).toDouble();
        base.rangeEnd = base.rangeStart + meta.value(QStringLiteral("duration")).toDouble();
        base.times.append(base.rangeStart);
    }

    for (const QJsonValue &v : content) {
        const QJsonObject item = v.toObject();
        if (item.value(QStringLiteral("type")).toString() != QLatin1String("image"))
            continue;
        const QByteArray bytes = QByteArray::fromBase64(item.value(QStringLiteral("data")).toString().toLatin1());
        const QString dir = sessionDir();
        if (bytes.isEmpty() || dir.isEmpty())
            continue;
        Entry entry = base;
        entry.path = QDir(dir).filePath(QStringLiteral("%1-%2.%3")
                                            .arg(m_nextId, 4, 10, QLatin1Char('0'))
                                            .arg(tool, extensionFor(item.value(QStringLiteral("mimeType")).toString())));
        QFile file(entry.path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            continue;
        file.close();
        entry.owned = true;
        QBuffer buffer;
        buffer.setData(bytes);
        buffer.open(QIODevice::ReadOnly);
        const QSize size = QImageReader(&buffer).size();
        entry.width = size.width();
        entry.height = size.height();
        add(entry);
    }

    const QString path = meta.value(QStringLiteral("path")).toString();
    const bool returnedPath = args.value(QStringLiteral("return")).toString() == QLatin1String("path")
                              || args.value(QStringLiteral("full")).toBool();
    if (returnedPath && isImagePath(path) && QFileInfo::exists(path)) {
        Entry entry = base;
        entry.path = path;
        const QSize size = QImageReader(path).size();
        entry.width = size.width();
        entry.height = size.height();
        add(entry);
    }
}

int AgentImageLog::entryForCall(int callId) const
{
    for (const Entry &e : m_entries) {
        if (e.callId == callId)
            return e.id;
    }
    return -1;
}

QVariantMap AgentImageLog::toMap(const Entry &e) const
{
    QVariantList times;
    for (double t : e.times)
        times.append(t);
    return {{QStringLiteral("entryId"), e.id},
            {QStringLiteral("callId"), e.callId},
            {QStringLiteral("tool"), e.tool},
            {QStringLiteral("args"), e.args},
            {QStringLiteral("time"), e.time},
            {QStringLiteral("path"), e.path},
            {QStringLiteral("url"), QUrl::fromLocalFile(e.path)},
            {QStringLiteral("imageWidth"), e.width},
            {QStringLiteral("imageHeight"), e.height},
            {QStringLiteral("times"), times},
            {QStringLiteral("rangeStart"), e.rangeStart},
            {QStringLiteral("rangeEnd"), e.rangeEnd}};
}

QVariantMap AgentImageLog::entry(int entryId) const
{
    for (const Entry &e : m_entries) {
        if (e.id == entryId)
            return toMap(e);
    }
    return {};
}

void AgentImageLog::copyPath(const QString &path) const
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(path);
}

bool AgentImageLog::saveCopy(const QString &path, const QUrl &destination) const
{
    const QString target = destination.toLocalFile();
    if (target.isEmpty() || !QFileInfo::exists(path))
        return false;
    QFile::remove(target);
    return QFile::copy(path, target);
}

QUrl AgentImageLog::folderUrl(const QString &path) const
{
    return QUrl::fromLocalFile(QFileInfo(path).absolutePath());
}
