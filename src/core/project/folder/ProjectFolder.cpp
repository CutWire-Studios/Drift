#include "core/project/folder/ProjectFolder.h"

#include "core/project/folder/ProjectFormat.h"
#include "core/project/folder/ProjectSchema.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace drift::projdir {

namespace {

const QString kSchemaKey = QStringLiteral("$schema");

bool keyBefore(const QString &a, const QString &b)
{
    static const QStringList priority{QStringLiteral("$schema"), QStringLiteral("id"),
                                      QStringLiteral("type"), QStringLiteral("kind"),
                                      QStringLiteral("name")};
    const int pa = priority.indexOf(a);
    const int pb = priority.indexOf(b);
    if (pa >= 0 || pb >= 0) {
        if (pa < 0)
            return false;
        if (pb < 0)
            return true;
        return pa < pb;
    }
    return a < b;
}

void appendString(QByteArray &out, const QString &s)
{
    out += '"';
    const QByteArray utf8 = s.toUtf8();
    for (const char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void appendNumber(QByteArray &out, double d)
{
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        out += QByteArray::number(static_cast<qint64>(d));
        return;
    }
    out += QByteArray::number(d, 'g', QLocale::FloatingPointShortest);
}

bool isScalar(const QJsonValue &v)
{
    return !v.isObject() && !v.isArray();
}

void appendValue(QByteArray &out, const QJsonValue &v, int depth, bool oneLine);

// Arrays of scalars that fit in 80 columns stay on one line, so keyframe times and vertex lists
// don't run to a line per number.
bool fitsOnOneLine(const QJsonArray &array, int depth)
{
    if (array.size() > 16)
        return false;
    for (const QJsonValue &v : array) {
        if (!isScalar(v))
            return false;
    }
    QByteArray line;
    appendValue(line, array, depth, true);
    return line.size() + depth * 2 <= 80;
}

void appendValue(QByteArray &out, const QJsonValue &v, int depth, bool oneLine)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        out += "null";
        return;
    case QJsonValue::Bool:
        out += v.toBool() ? "true" : "false";
        return;
    case QJsonValue::Double:
        appendNumber(out, v.toDouble());
        return;
    case QJsonValue::String:
        appendString(out, v.toString());
        return;
    case QJsonValue::Array: {
        const QJsonArray array = v.toArray();
        if (array.isEmpty()) {
            out += "[]";
            return;
        }
        const bool flat = oneLine || fitsOnOneLine(array, depth);
        out += '[';
        for (int i = 0; i < array.size(); ++i) {
            if (flat) {
                if (i)
                    out += ", ";
            } else {
                out += i ? ",\n" : "\n";
                out += QByteArray((depth + 1) * 2, ' ');
            }
            appendValue(out, array.at(i), depth + 1, flat);
        }
        if (!flat) {
            out += '\n';
            out += QByteArray(depth * 2, ' ');
        }
        out += ']';
        return;
    }
    case QJsonValue::Object: {
        const QJsonObject object = v.toObject();
        if (object.isEmpty()) {
            out += "{}";
            return;
        }
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), keyBefore);
        out += '{';
        for (int i = 0; i < keys.size(); ++i) {
            if (oneLine) {
                if (i)
                    out += ", ";
            } else {
                out += i ? ",\n" : "\n";
                out += QByteArray((depth + 1) * 2, ' ');
            }
            appendString(out, keys.at(i));
            out += ": ";
            appendValue(out, object.value(keys.at(i)), depth + 1, oneLine);
        }
        if (!oneLine) {
            out += '\n';
            out += QByteArray(depth * 2, ' ');
        }
        out += '}';
        return;
    }
    }
}

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString pathOf(const QString &dir, const QString &file)
{
    return QDir::cleanPath(dir + QLatin1Char('/') + file);
}

// Absolute media path under the folder -> relative; relative -> absolute. Resource (":/") and URL
// paths are left alone.
void relativize(QJsonObject &object, const QString &key, const QString &root)
{
    const QString value = object.value(key).toString();
    if (value.isEmpty() || root.isEmpty() || QDir::isRelativePath(value))
        return;
    const QString cleanRoot = QDir::cleanPath(root);
    const QString clean = QDir::cleanPath(value);
    if (clean.startsWith(cleanRoot + QLatin1Char('/')))
        object.insert(key, clean.mid(cleanRoot.size() + 1));
}

void absolutize(QJsonObject &object, const QString &key, const QString &root)
{
    const QString value = object.value(key).toString();
    if (value.isEmpty() || root.isEmpty() || !QDir::isRelativePath(value)
        || value.contains(QLatin1String("://")))
        return;
    object.insert(key, QDir::cleanPath(root + QLatin1Char('/') + value));
}

QString trackFileName(int index, int width, const QJsonObject &track)
{
    QString slug = slugify(track.value(QStringLiteral("name")).toString());
    QString name = QStringLiteral("%1-%2").arg(index + 1, width, 10, QLatin1Char('0'))
                       .arg(slugify(track.value(QStringLiteral("type")).toString(QStringLiteral("track"))));
    if (!slug.isEmpty())
        name += QLatin1Char('-') + slug;
    return name + QStringLiteral(".json");
}

// "07-video-intro.json" sorts by 7, then by the rest, so a hand-added "10-..." lands after "9-...".
bool naturalLess(const QString &a, const QString &b)
{
    const auto lead = [](const QString &s) {
        int n = 0;
        while (n < s.size() && s.at(n).isDigit())
            ++n;
        return n ? s.left(n).toLongLong() : -1;
    };
    const qint64 na = lead(a);
    const qint64 nb = lead(b);
    if (na != nb)
        return na < nb;
    return a < b;
}

// Every file names its schema, relative to itself, so any JSON language server validates and completes it.
void put(FileMap &files, const QString &path, QJsonObject object)
{
    const QString ref = schemaRefFor(path);
    if (!ref.isEmpty())
        object.insert(kSchemaKey, ref);
    files.insert(path, canonicalJson(object));
}

void splitTracks(const QJsonArray &tracks, const QString &dir, const QString &root, FileMap &files,
                 QSet<QString> &sidecarDirs)
{
    const int width = qMax(2, QString::number(tracks.size()).size());
    for (int i = 0; i < tracks.size(); ++i) {
        QJsonObject track = tracks.at(i).toObject();
        QList<QJsonObject> clips;
        for (const QJsonValue &v : track.value(QStringLiteral("clips")).toArray())
            clips.append(v.toObject());
        std::stable_sort(clips.begin(), clips.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("timelineStartUs")).toDouble()
                < b.value(QStringLiteral("timelineStartUs")).toDouble();
        });

        QJsonArray outClips;
        for (QJsonObject clip : std::as_const(clips)) {
            relativize(clip, QStringLiteral("path"), root);
            const QString clipId = clip.value(QStringLiteral("id")).toString();
            const QString sidecar = safeId(clipId);
            // A duplicate id would have two clips share one folder; the second stays inline.
            if (!clipId.isEmpty() && !sidecarDirs.contains(sidecar)) {
                QStringList used;
                for (const SidecarGroup &group : sidecarGroups()) {
                    QJsonObject payload;
                    for (const QString &key : group.keys) {
                        if (clip.contains(key))
                            payload.insert(key, clip.value(key));
                    }
                    if (payload.isEmpty() || compact(payload).size() <= kSidecarThresholdBytes)
                        continue;
                    for (auto it = payload.begin(); it != payload.end(); ++it)
                        clip.remove(it.key());
                    put(files, QStringLiteral("sidecars/%1/%2.json").arg(sidecar, group.name), payload);
                    used.append(group.name);
                }
                if (!used.isEmpty()) {
                    sidecarDirs.insert(sidecar);
                    used.sort();
                    clip.insert(QStringLiteral("sidecars"), QJsonArray::fromStringList(used));
                }
            }
            outClips.append(clip);
        }
        track.insert(QStringLiteral("clips"), outClips);
        put(files, dir + QLatin1Char('/') + trackFileName(i, width, track), track);
    }
}

} // namespace

QByteArray canonicalJson(const QJsonValue &value)
{
    QByteArray out;
    appendValue(out, value, 0, false);
    out += '\n';
    return out;
}

void lineColumnAt(const QByteArray &bytes, int offset, int *line, int *column)
{
    int l = 1;
    int lastBreak = -1;
    const int end = qMin(offset, int(bytes.size()));
    for (int i = 0; i < end; ++i) {
        if (bytes.at(i) == '\n') {
            ++l;
            lastBreak = i;
        }
    }
    *line = l;
    *column = end - lastBreak;
}

bool parseJsonFile(const QString &file, const QByteArray &bytes, QJsonValue *out, Problem *problem)
{
    QByteArray body = bytes;
    if (body.startsWith("\xEF\xBB\xBF"))
        body.remove(0, 3);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError) {
        if (problem) {
            problem->file = file;
            problem->message = error.errorString();
            lineColumnAt(body, error.offset, &problem->line, &problem->column);
        }
        return false;
    }
    *out = doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    return true;
}

QString slugify(const QString &text, int maxLength)
{
    QString out;
    bool dash = false;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.unicode() < 128 && c.isLetterOrNumber()) {
            out += c.toLower();
            dash = false;
        } else if (c.category() == QChar::Mark_NonSpacing) {
            continue;
        } else if (!dash && !out.isEmpty()) {
            out += QLatin1Char('-');
            dash = true;
        }
    }
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    if (out.size() > maxLength) {
        out.truncate(maxLength);
        while (out.endsWith(QLatin1Char('-')))
            out.chop(1);
    }
    return out;
}

QString safeId(const QString &id)
{
    QString out;
    for (const QChar c : id) {
        if (c.unicode() < 128 && (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')
                                  || c == QLatin1Char('.')))
            out += c;
        else
            out += QLatin1Char('%') + QStringLiteral("%1").arg(int(c.unicode()), 2, 16, QLatin1Char('0'));
    }
    if (out.isEmpty() || out.startsWith(QLatin1Char('.')))
        out.prepend(QLatin1Char('_'));
    return out;
}

FileMap splitProject(const QJsonObject &doc, const QString &rootDir)
{
    FileMap files;
    QJsonObject project = doc;
    for (const char *key : {"assets", "binFolders", "tracks", "bookmarks", "sequences", "guides"})
        project.remove(QLatin1String(key));
    put(files, QStringLiteral("project.json"), project);

    QJsonArray assetOrder;
    QSet<QString> usedNames;
    for (const QJsonValue &v : doc.value(QStringLiteral("assets")).toArray()) {
        QJsonObject asset = v.toObject();
        const QString id = asset.value(QStringLiteral("id")).toString();
        assetOrder.append(id);
        if (asset.contains(QStringLiteral("transcript"))) {
            put(files, QStringLiteral("transcripts/%1.json").arg(safeId(id)),
                asset.value(QStringLiteral("transcript")).toObject());
            asset.remove(QStringLiteral("transcript"));
        }
        relativize(asset, QStringLiteral("path"), rootDir);
        QString name = safeId(id);
        const QString slug = slugify(asset.value(QStringLiteral("name")).toString());
        if (!slug.isEmpty())
            name += QLatin1Char('-') + slug;
        QString candidate = name;
        for (int n = 2; usedNames.contains(candidate); ++n)
            candidate = QStringLiteral("%1~%2").arg(name).arg(n);
        usedNames.insert(candidate);
        put(files, QStringLiteral("assets/%1.json").arg(candidate), asset);
    }

    put(files, QStringLiteral("bin.json"),
        {{QStringLiteral("assetOrder"), assetOrder},
         {QStringLiteral("folders"), doc.value(QStringLiteral("binFolders")).toArray()}});
    put(files, QStringLiteral("markers.json"),
        {{QStringLiteral("markers"), doc.value(QStringLiteral("bookmarks")).toArray()}});
    if (doc.contains(QStringLiteral("guides")))
        put(files, QStringLiteral("guides.json"), doc.value(QStringLiteral("guides")).toObject());

    QSet<QString> sidecarDirs;
    splitTracks(doc.value(QStringLiteral("tracks")).toArray(), QStringLiteral("timeline/tracks"),
                rootDir, files, sidecarDirs);

    QList<QJsonObject> sequences;
    for (const QJsonValue &v : doc.value(QStringLiteral("sequences")).toArray())
        sequences.append(v.toObject());
    std::sort(sequences.begin(), sequences.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
    });
    QSet<QString> usedSlugs;
    for (const QJsonObject &sequence : std::as_const(sequences)) {
        const QString id = sequence.value(QStringLiteral("id")).toString();
        QString slug = slugify(id);
        if (slug.isEmpty())
            slug = QStringLiteral("sequence");
        QString candidate = slug;
        for (int n = 2; usedSlugs.contains(candidate); ++n)
            candidate = QStringLiteral("%1-%2").arg(slug).arg(n);
        usedSlugs.insert(candidate);
        const QString dir = QStringLiteral("sequences/") + candidate;
        put(files, dir + QStringLiteral("/sequence.json"), {{QStringLiteral("id"), id}});
        splitTracks(sequence.value(QStringLiteral("tracks")).toArray(), dir + QStringLiteral("/tracks"),
                    rootDir, files, sidecarDirs);
    }
    return files;
}

namespace {

QStringList filesUnder(const FileMap &files, const QString &prefix)
{
    QStringList out;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key().startsWith(prefix) && it.key().endsWith(QLatin1String(".json"))
            && !it.key().mid(prefix.size()).contains(QLatin1Char('/')))
            out.append(it.key());
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return naturalLess(a.section(QLatin1Char('/'), -1), b.section(QLatin1Char('/'), -1));
    });
    return out;
}

struct Reader
{
    const FileMap &files;
    QList<Problem> &problems;
    bool ok = true;

    bool read(const QString &file, QJsonObject *out)
    {
        QJsonValue value;
        Problem problem;
        if (!parseJsonFile(file, files.value(file), &value, &problem)) {
            problems.append(problem);
            ok = false;
            return false;
        }
        if (!value.isObject()) {
            problems.append({file, 1, 1, QStringLiteral("Expected a JSON object")});
            ok = false;
            return false;
        }
        *out = value.toObject();
        out->remove(kSchemaKey);
        return true;
    }
};

QJsonArray joinTracks(Reader &reader, const FileMap &files, const QString &dir, const QString &root,
                      QSet<QString> &claimedSidecars)
{
    QJsonArray tracks;
    for (const QString &file : filesUnder(files, dir + QLatin1Char('/'))) {
        QJsonObject track;
        if (!reader.read(file, &track))
            continue;
        QJsonArray clips;
        for (const QJsonValue &v : track.value(QStringLiteral("clips")).toArray()) {
            QJsonObject clip = v.toObject();
            const QString clipId = clip.value(QStringLiteral("id")).toString();
            const QString sidecar = safeId(clipId);
            const QStringList declared = [&] {
                QStringList l;
                for (const QJsonValue &g : clip.value(QStringLiteral("sidecars")).toArray())
                    l.append(g.toString());
                return l;
            }();
            clip.remove(QStringLiteral("sidecars"));
            if (!clipId.isEmpty()) {
                for (const SidecarGroup &group : sidecarGroups()) {
                    const QString path = QStringLiteral("sidecars/%1/%2.json").arg(sidecar, group.name);
                    if (!files.contains(path)) {
                        if (declared.contains(group.name))
                            reader.problems.append({file, 1, 1,
                                                    QStringLiteral("Clip %1 lists sidecar \"%2\" but %3 is missing")
                                                        .arg(clipId, group.name, path),
                                                    QStringLiteral("warning")});
                        continue;
                    }
                    QJsonObject payload;
                    if (!reader.read(path, &payload))
                        continue;
                    claimedSidecars.insert(path);
                    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it)
                        clip.insert(it.key(), it.value());
                }
            }
            absolutize(clip, QStringLiteral("path"), root);
            clips.append(clip);
        }
        track.insert(QStringLiteral("clips"), clips);
        tracks.append(track);
    }
    return tracks;
}

} // namespace

JoinResult joinProject(const FileMap &files, const QString &rootDir)
{
    JoinResult result;
    Reader reader{files, result.problems};

    QJsonObject doc;
    if (!files.contains(QStringLiteral("project.json"))) {
        result.problems.append({QStringLiteral("project.json"), 1, 1, QStringLiteral("project.json is missing")});
        result.ok = false;
        return result;
    }
    reader.read(QStringLiteral("project.json"), &doc);

    QJsonObject bin;
    if (files.contains(QStringLiteral("bin.json")))
        reader.read(QStringLiteral("bin.json"), &bin);
    doc.insert(QStringLiteral("binFolders"), bin.value(QStringLiteral("folders")).toArray());

    QJsonObject markers;
    if (files.contains(QStringLiteral("markers.json")))
        reader.read(QStringLiteral("markers.json"), &markers);
    doc.insert(QStringLiteral("bookmarks"), markers.value(QStringLiteral("markers")).toArray());

    if (files.contains(QStringLiteral("guides.json"))) {
        QJsonObject guides;
        if (reader.read(QStringLiteral("guides.json"), &guides))
            doc.insert(QStringLiteral("guides"), guides);
    }

    // Order from bin.json; files it does not list (added by hand) follow in file-name order.
    QHash<QString, QJsonObject> byId;
    QStringList fileOrder;
    for (const QString &file : filesUnder(files, QStringLiteral("assets/"))) {
        QJsonObject asset;
        if (!reader.read(file, &asset))
            continue;
        const QString id = asset.value(QStringLiteral("id")).toString();
        const QString transcript = QStringLiteral("transcripts/%1.json").arg(safeId(id));
        if (files.contains(transcript)) {
            QJsonObject body;
            if (reader.read(transcript, &body))
                asset.insert(QStringLiteral("transcript"), body);
        }
        absolutize(asset, QStringLiteral("path"), rootDir);
        byId.insert(id, asset);
        fileOrder.append(id);
    }
    QJsonArray assets;
    QSet<QString> placed;
    for (const QJsonValue &v : bin.value(QStringLiteral("assetOrder")).toArray()) {
        const QString id = v.toString();
        if (byId.contains(id) && !placed.contains(id)) {
            assets.append(byId.value(id));
            placed.insert(id);
        }
    }
    for (const QString &id : fileOrder) {
        if (!placed.contains(id)) {
            assets.append(byId.value(id));
            placed.insert(id);
        }
    }
    doc.insert(QStringLiteral("assets"), assets);

    QSet<QString> claimed;
    doc.insert(QStringLiteral("tracks"),
               joinTracks(reader, files, QStringLiteral("timeline/tracks"), rootDir, claimed));

    QJsonArray sequences;
    QStringList seqFiles;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("sequences/")) && it.key().endsWith(QLatin1String("/sequence.json")))
            seqFiles.append(it.key());
    }
    QList<QJsonObject> sequenceDocs;
    for (const QString &file : seqFiles) {
        QJsonObject sequence;
        if (!reader.read(file, &sequence))
            continue;
        const QString dir = file.left(file.size() - int(strlen("/sequence.json")));
        sequence.insert(QStringLiteral("tracks"),
                        joinTracks(reader, files, dir + QStringLiteral("/tracks"), rootDir, claimed));
        sequenceDocs.append(sequence);
    }
    std::sort(sequenceDocs.begin(), sequenceDocs.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
    });
    for (const QJsonObject &sequence : std::as_const(sequenceDocs))
        sequences.append(sequence);
    if (!sequences.isEmpty())
        doc.insert(QStringLiteral("sequences"), sequences);

    result.ok = reader.ok;
    if (result.ok)
        result.doc = doc;
    return result;
}

namespace {

bool managed(const QString &rel)
{
    if (rel == QLatin1String("project.json") || rel == QLatin1String("bin.json")
        || rel == QLatin1String("markers.json") || rel == QLatin1String("guides.json"))
        return true;
    for (const char *dir : {"assets/", "timeline/", "sequences/", "sidecars/", "transcripts/"}) {
        if (rel.startsWith(QLatin1String(dir)))
            return rel.endsWith(QLatin1String(".json"));
    }
    return false;
}

} // namespace

FileMap readProjectFolder(const QString &dir)
{
    FileMap files;
    if (!isProjectFolder(dir))
        return files;
    QDirIterator it(dir, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
    const QDir base(dir);
    while (it.hasNext()) {
        const QString rel = base.relativeFilePath(it.next());
        if (!managed(rel))
            continue;
        QFile file(base.filePath(rel));
        if (file.open(QIODevice::ReadOnly))
            files.insert(rel, file.readAll());
    }
    return files;
}

bool isProjectFolder(const QString &dir)
{
    return QFileInfo(QDir(dir).filePath(QStringLiteral("project.json"))).isFile();
}

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    return out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit();
}

QByteArray readFile(const QString &path, bool *exists)
{
    QFile file(path);
    *exists = file.open(QIODevice::ReadOnly);
    return *exists ? file.readAll() : QByteArray();
}

} // namespace

WriteReport writeProjectFolder(const QString &dir, const FileMap &files, const FileMap *known,
                               const QString &conflictDir)
{
    WriteReport report;
    const QDir base(dir);
    if (!base.mkpath(QStringLiteral("."))) {
        report.error = QStringLiteral("Couldn't create %1").arg(dir);
        return report;
    }
    const auto conflict = [&](const QString &rel, const QByteArray &ours) {
        report.conflicts.append(rel);
        if (!conflictDir.isEmpty() && !ours.isNull())
            writeFile(QDir(conflictDir).filePath(rel), ours);
    };
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString path = base.filePath(it.key());
        bool exists = false;
        const QByteArray disk = readFile(path, &exists);
        if (exists && disk == it.value())
            continue;
        if (known) {
            const bool knew = known->contains(it.key());
            const bool untouched = knew ? (exists && disk == known->value(it.key())) : !exists;
            if (!untouched) {
                conflict(it.key(), it.value());
                continue;
            }
        }
        if (!writeFile(path, it.value())) {
            report.error = QStringLiteral("Couldn't write %1").arg(path);
            return report;
        }
        report.written.append(it.key());
    }
    // Managed files that dropped out of the set: a deleted track, asset or sidecar.
    const FileMap existing = readProjectFolder(dir);
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (files.contains(it.key()))
            continue;
        if (known && !known->contains(it.key()))
            continue;
        if (known && known->value(it.key()) != it.value()) {
            conflict(it.key(), QByteArray());
            continue;
        }
        if (QFile::remove(base.filePath(it.key()))) {
            report.deleted.append(it.key());
            QDir parent = QFileInfo(base.filePath(it.key())).dir();
            for (int i = 0; i < 3 && parent != base && parent.rmdir(QStringLiteral(".")); ++i)
                parent.cdUp();
        }
    }
    return report;
}

} // namespace drift::projdir
