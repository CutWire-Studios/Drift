#include "core/project/folder/ProjectLint.h"

#include "core/project/folder/ProjectFormat.h"
#include "core/project/folder/ProjectSchema.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace drift::projdir {

namespace {

struct ClipInfo
{
    QString file;
    QString id;
    QString trackId;
    QString assetId;
    QString sequenceId;
    QString type;
    double start = 0;
    double duration = 0;
    bool hasDuration = false;
};

QString typeName(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null: return QStringLiteral("null");
    case QJsonValue::Bool: return QStringLiteral("boolean");
    case QJsonValue::Double: return QStringLiteral("number");
    case QJsonValue::String: return QStringLiteral("string");
    case QJsonValue::Array: return QStringLiteral("array");
    case QJsonValue::Object: return QStringLiteral("object");
    default: return QStringLiteral("undefined");
    }
}

bool matchesType(const FieldDesc &field, const QJsonValue &v)
{
    const QString &t = field.type;
    if (t == QLatin1String("any"))
        return true;
    if (t == QLatin1String("string"))
        return v.isString();
    if (t == QLatin1String("boolean"))
        return v.isBool();
    if (t == QLatin1String("number"))
        return v.isDouble();
    if (t == QLatin1String("integer"))
        return v.isDouble() && v.toDouble() == std::floor(v.toDouble());
    if (t == QLatin1String("array") || t == QLatin1String("effects"))
        return v.isArray();
    if (t == QLatin1String("object") || t == QLatin1String("keyframes"))
        return v.isObject();
    return true;
}

class Linter
{
public:
    Linter(const FileMap &files, const QString &root, const LintContext &ctx)
        : m_files(files), m_root(root), m_ctx(ctx)
    {
    }

    QList<Problem> run()
    {
        collect();
        checkStructure();
        checkReferences();
        checkTimelines();
        checkSidecars();
        checkEffectsAndKeyframes();
        checkMedia();
        std::stable_sort(m_out.begin(), m_out.end(), [](const Problem &a, const Problem &b) {
            if (a.file != b.file)
                return a.file < b.file;
            if (a.line != b.line)
                return a.line < b.line;
            return a.column < b.column;
        });
        return m_out;
    }

private:
    void add(const QString &file, const QString &severity, const QString &message, const QString &key = {},
             const QString &value = {})
    {
        Problem p;
        p.file = file;
        p.severity = severity;
        p.message = message;
        p.line = 1;
        p.column = 1;
        if (!key.isEmpty())
            locateKey(m_files.value(file), key, value, &p.line, &p.column);
        m_out.append(p);
    }

    QJsonObject parsed(const QString &file) const { return m_parsed.value(file); }

    void collect()
    {
        for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
            const QString kind = kindForPath(it.key());
            if (kind.isEmpty())
                continue;
            QJsonValue value;
            Problem problem;
            if (!parseJsonFile(it.key(), it.value(), &value, &problem)) {
                m_out.append(problem);
                continue;
            }
            if (!value.isObject()) {
                add(it.key(), QStringLiteral("error"), QStringLiteral("Expected a JSON object"));
                continue;
            }
            m_parsed.insert(it.key(), value.toObject());
        }

        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            const QString kind = kindForPath(it.key());
            if (kind == QLatin1String("asset")) {
                const QString id = it->value(QStringLiteral("id")).toString();
                if (m_assets.contains(id))
                    add(it.key(), QStringLiteral("error"),
                        QStringLiteral("Duplicate asset id \"%1\" (also in %2)").arg(id, m_assets.value(id)),
                        QStringLiteral("id"), id);
                m_assets.insert(id, it.key());
            } else if (kind == QLatin1String("sequence")) {
                m_sequences.insert(it->value(QStringLiteral("id")).toString(), it.key());
            }
        }
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            if (kindForPath(it.key()) != QLatin1String("track"))
                continue;
            const QString trackId = it->value(QStringLiteral("id")).toString();
            if (m_tracks.contains(trackId))
                add(it.key(), QStringLiteral("error"),
                    QStringLiteral("Duplicate track id \"%1\" (also in %2)").arg(trackId, m_tracks.value(trackId)),
                    QStringLiteral("id"), trackId);
            m_tracks.insert(trackId, it.key());
            for (const QJsonValue &v : it->value(QStringLiteral("clips")).toArray()) {
                const QJsonObject clip = v.toObject();
                ClipInfo info;
                info.file = it.key();
                info.id = clip.value(QStringLiteral("id")).toString();
                info.trackId = trackId;
                info.assetId = clip.value(QStringLiteral("assetId")).toString();
                info.sequenceId = clip.value(QStringLiteral("sequenceId")).toString();
                info.type = clip.value(QStringLiteral("type")).toString();
                info.start = clip.value(QStringLiteral("timelineStartUs")).toDouble();
                info.duration = clip.value(QStringLiteral("timelineDurationUs")).toDouble();
                info.hasDuration = clip.contains(QStringLiteral("timelineDurationUs"));
                if (!info.id.isEmpty() && m_clips.contains(info.id))
                    add(it.key(), QStringLiteral("error"),
                        QStringLiteral("Duplicate clip id \"%1\" (also in %2)").arg(info.id, m_clips.value(info.id).file),
                        QStringLiteral("id"), info.id);
                m_clips.insert(info.id, info);
                m_clipOrder[it.key()].append(info);
            }
        }
    }

    void checkFields(const QString &file, const QString &kindName, const QJsonObject &object,
                     const QString &path = {})
    {
        const KindDesc *kind = kindDescription(kindName);
        if (!kind)
            return;
        for (const FieldDesc &field : kind->fields) {
            if (field.required && !object.contains(field.key))
                add(file, QStringLiteral("error"),
                    QStringLiteral("%1is missing the required key \"%2\"").arg(path, field.key));
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key() == QLatin1String("$schema"))
                continue;
            const FieldDesc *field = kind->field(it.key());
            if (!field) {
                if (kind->closed)
                    add(file, QStringLiteral("warning"),
                        QStringLiteral("%1unknown key \"%2\"").arg(path, it.key()), it.key());
                continue;
            }
            if (!matchesType(*field, it.value())) {
                add(file, QStringLiteral("error"),
                    QStringLiteral("%1\"%2\" should be %3 but is %4")
                        .arg(path, it.key(), field->type, typeName(it.value())),
                    it.key());
                continue;
            }
            if (!field->enumValues.isEmpty() && it.value().isString()
                && !field->enumValues.contains(it.value().toString()))
                add(file, QStringLiteral("error"),
                    QStringLiteral("%1\"%2\" is \"%3\"; expected one of: %4")
                        .arg(path, it.key(), it.value().toString(), field->enumValues.join(QStringLiteral(", "))),
                    it.key(), it.value().toString());
        }
    }

    void checkStructure()
    {
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            const QString kind = kindForPath(it.key());
            if (kind == QLatin1String("transcript"))
                continue;
            checkFields(it.key(), kind, it.value());
            if (kind == QLatin1String("track")) {
                const QJsonArray clips = it->value(QStringLiteral("clips")).toArray();
                for (int i = 0; i < clips.size(); ++i) {
                    const QJsonObject clip = clips.at(i).toObject();
                    checkFields(it.key(), QStringLiteral("clip"), clip,
                                QStringLiteral("clip %1: ").arg(clip.value(QStringLiteral("id")).toString(QString::number(i))));
                }
            }
        }
    }

    void checkReferences()
    {
        for (auto it = m_clips.constBegin(); it != m_clips.constEnd(); ++it) {
            const ClipInfo &clip = it.value();
            if (!clip.assetId.isEmpty() && !m_assets.contains(clip.assetId))
                add(clip.file, QStringLiteral("error"),
                    QStringLiteral("Clip \"%1\" uses unknown asset \"%2\"").arg(clip.id, clip.assetId),
                    QStringLiteral("assetId"), clip.assetId);
            if (!clip.sequenceId.isEmpty() && !m_sequences.contains(clip.sequenceId))
                add(clip.file, QStringLiteral("error"),
                    QStringLiteral("Clip \"%1\" plays unknown sequence \"%2\"").arg(clip.id, clip.sequenceId),
                    QStringLiteral("sequenceId"), clip.sequenceId);
        }
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            const QString kind = kindForPath(it.key());
            if (kind == QLatin1String("track")) {
                for (const char *key : {"parentTrackId", "spanEndTrackId"}) {
                    const QString ref = it->value(QLatin1String(key)).toString();
                    if (!ref.isEmpty() && !m_tracks.contains(ref))
                        add(it.key(), QStringLiteral("error"),
                            QStringLiteral("\"%1\" names unknown track \"%2\"").arg(QLatin1String(key), ref),
                            QLatin1String(key), ref);
                }
                for (const QJsonValue &v : it->value(QStringLiteral("clips")).toArray()) {
                    const QJsonObject clip = v.toObject();
                    const QString linked = clip.value(QStringLiteral("linkedClipId")).toString();
                    if (!linked.isEmpty() && !m_clips.contains(linked))
                        add(it.key(), QStringLiteral("error"),
                            QStringLiteral("Clip \"%1\" is linked to unknown clip \"%2\"")
                                .arg(clip.value(QStringLiteral("id")).toString(), linked),
                            QStringLiteral("linkedClipId"), linked);
                }
            } else if (kind == QLatin1String("transcript")) {
                const QString id = it.key().section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
                bool known = false;
                for (auto a = m_assets.constBegin(); a != m_assets.constEnd(); ++a)
                    known = known || safeId(a.key()) == id;
                if (!known)
                    add(it.key(), QStringLiteral("warning"),
                        QStringLiteral("Transcript for unknown asset \"%1\"").arg(id));
            }
        }
        const QJsonObject bin = m_parsed.value(QStringLiteral("bin.json"));
        for (const QJsonValue &v : bin.value(QStringLiteral("assetOrder")).toArray()) {
            if (!m_assets.contains(v.toString()))
                add(QStringLiteral("bin.json"), QStringLiteral("warning"),
                    QStringLiteral("assetOrder lists unknown asset \"%1\"").arg(v.toString()),
                    QStringLiteral("assetOrder"), {});
        }
    }

    void checkTimelines()
    {
        for (auto it = m_clipOrder.constBegin(); it != m_clipOrder.constEnd(); ++it) {
            QList<ClipInfo> clips = it.value();
            std::stable_sort(clips.begin(), clips.end(),
                             [](const ClipInfo &a, const ClipInfo &b) { return a.start < b.start; });
            const QString kind = m_parsed.value(it.key()).value(QStringLiteral("type")).toString();
            // Adjustment and shape tracks stack by design; everything else plays one clip at a time.
            if (kind == QLatin1String("adjustment") || kind == QLatin1String("shape"))
                continue;
            for (int i = 1; i < clips.size(); ++i) {
                const ClipInfo &prev = clips.at(i - 1);
                if (!prev.hasDuration)
                    continue;
                if (clips.at(i).start < prev.start + prev.duration)
                    add(it.key(), QStringLiteral("warning"),
                        QStringLiteral("Clip \"%1\" overlaps clip \"%2\" on this track")
                            .arg(clips.at(i).id, prev.id),
                        QStringLiteral("id"), clips.at(i).id);
            }
        }
    }

    void checkSidecars()
    {
        QSet<QString> dirs;
        for (auto it = m_clips.constBegin(); it != m_clips.constEnd(); ++it)
            dirs.insert(safeId(it.key()));
        for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1String("sidecars/")))
                continue;
            const QStringList parts = it.key().split(QLatin1Char('/'));
            if (parts.size() != 3)
                continue;
            if (!dirs.contains(parts.at(1)))
                add(it.key(), QStringLiteral("warning"),
                    QStringLiteral("Sidecar belongs to no clip (no clip has id \"%1\")").arg(parts.at(1)));
            else if (kindForPath(it.key()).isEmpty())
                add(it.key(), QStringLiteral("warning"),
                    QStringLiteral("Unknown sidecar \"%1\"; use keyframes, masks, effects or payload").arg(parts.at(2)));
        }
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            if (kindForPath(it.key()) != QLatin1String("track"))
                continue;
            for (const QJsonValue &v : it->value(QStringLiteral("clips")).toArray()) {
                const QJsonObject clip = v.toObject();
                const QString id = clip.value(QStringLiteral("id")).toString();
                for (const QJsonValue &g : clip.value(QStringLiteral("sidecars")).toArray()) {
                    const QString path = QStringLiteral("sidecars/%1/%2.json").arg(safeId(id), g.toString());
                    if (!m_files.contains(path))
                        add(it.key(), QStringLiteral("warning"),
                            QStringLiteral("Clip \"%1\" lists sidecar \"%2\" but %3 is missing").arg(id, g.toString(), path),
                            QStringLiteral("id"), id);
                }
            }
        }
    }

    void checkKeyframes(const QString &file, const QString &clipId, const QString &key, const QJsonValue &track,
                        const ClipInfo &clip)
    {
        if (!track.isObject())
            return;
        for (const QJsonValue &k : track.toObject().value(QStringLiteral("keyframes")).toArray()) {
            const double t = k.toObject().value(QStringLiteral("timeUs")).toDouble();
            if (t < 0 || (clip.hasDuration && t > clip.duration)) {
                add(file, QStringLiteral("warning"),
                    QStringLiteral("Clip \"%1\": %2 keyframe at %3 us is outside the clip (0 to %4 us)")
                        .arg(clipId, key).arg(qint64(t)).arg(qint64(clip.duration)),
                    QStringLiteral("timeUs"), QString::number(qint64(t)));
                return;
            }
        }
    }

    void checkEffectList(const QString &file, const QString &clipId, const QString &key, const QJsonValue &list,
                         const ClipInfo &clip)
    {
        for (const QJsonValue &v : list.toArray()) {
            const QJsonObject effect = v.toObject();
            const QString catalogId = effect.value(QStringLiteral("catalogId")).toString();
            if (m_ctx.effectParams && !catalogId.isEmpty()) {
                const std::optional<QStringList> params = m_ctx.effectParams(catalogId);
                if (!params) {
                    add(file, QStringLiteral("error"),
                        QStringLiteral("Clip \"%1\": unknown effect \"%2\" in %3").arg(clipId, catalogId, key),
                        QStringLiteral("catalogId"), catalogId);
                } else {
                    const QJsonObject set = effect.value(QStringLiteral("parameters")).toObject();
                    for (auto p = set.constBegin(); p != set.constEnd(); ++p) {
                        if (!params->contains(p.key()))
                            add(file, QStringLiteral("warning"),
                                QStringLiteral("Clip \"%1\": effect \"%2\" has no parameter \"%3\"")
                                    .arg(clipId, catalogId, p.key()),
                                p.key(), {});
                    }
                    for (const QString &param : *params) {
                        if (!set.contains(param) && !effect.value(QStringLiteral("paramKeyframes")).toObject().contains(param))
                            add(file, QStringLiteral("info"),
                                QStringLiteral("Clip \"%1\": effect \"%2\" doesn't set \"%3\" (the default is used)")
                                    .arg(clipId, catalogId, param),
                                QStringLiteral("catalogId"), catalogId);
                    }
                }
            }
            const QJsonObject pk = effect.value(QStringLiteral("paramKeyframes")).toObject();
            for (auto p = pk.constBegin(); p != pk.constEnd(); ++p)
                checkKeyframes(file, clipId, key + QLatin1Char('.') + p.key(), p.value(), clip);
        }
    }

    void checkClipPayload(const QString &file, const QJsonObject &clip, const ClipInfo &info)
    {
        for (const FieldDesc &field : kindDescription(QStringLiteral("clip"))->fields) {
            if (!clip.contains(field.key))
                continue;
            if (field.type == QLatin1String("keyframes"))
                checkKeyframes(file, info.id, field.key, clip.value(field.key), info);
            else if (field.type == QLatin1String("effects"))
                checkEffectList(file, info.id, field.key, clip.value(field.key), info);
        }
    }

    void checkEffectsAndKeyframes()
    {
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            const QString kind = kindForPath(it.key());
            if (kind == QLatin1String("track")) {
                for (const QJsonValue &v : it->value(QStringLiteral("clips")).toArray()) {
                    const QJsonObject clip = v.toObject();
                    checkClipPayload(it.key(), clip, m_clips.value(clip.value(QStringLiteral("id")).toString()));
                }
            } else if (kind.startsWith(QLatin1String("sidecar."))) {
                const QString dir = it.key().section(QLatin1Char('/'), 1, 1);
                for (auto c = m_clips.constBegin(); c != m_clips.constEnd(); ++c) {
                    if (safeId(c.key()) == dir) {
                        checkClipPayload(it.key(), it.value(), c.value());
                        break;
                    }
                }
            }
        }
    }

    void checkMedia()
    {
        if (!m_ctx.checkMedia)
            return;
        for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
            if (kindForPath(it.key()) != QLatin1String("asset"))
                continue;
            const QString path = it->value(QStringLiteral("path")).toString();
            if (path.isEmpty() || path.startsWith(QLatin1Char(':')) || path.contains(QLatin1String("://")))
                continue;
            const QString full = QDir::isRelativePath(path) ? QDir(m_root).filePath(path) : path;
            if (!QFileInfo::exists(full))
                add(it.key(), QStringLiteral("warning"), QStringLiteral("Media file not found: %1").arg(path),
                    QStringLiteral("path"), {});
        }
    }

    const FileMap &m_files;
    QString m_root;
    LintContext m_ctx;
    QList<Problem> m_out;
    QMap<QString, QJsonObject> m_parsed;
    QHash<QString, QString> m_assets;
    QHash<QString, QString> m_tracks;
    QHash<QString, QString> m_sequences;
    QHash<QString, ClipInfo> m_clips;
    QMap<QString, QList<ClipInfo>> m_clipOrder;
};

} // namespace

void locateKey(const QByteArray &bytes, const QString &key, const QString &value, int *line, int *column)
{
    *line = 1;
    *column = 1;
    QString pattern = QStringLiteral("\"%1\"\\s*:\\s*").arg(QRegularExpression::escape(key));
    if (!value.isEmpty())
        pattern += QStringLiteral("\"?%1\"?").arg(QRegularExpression::escape(value));
    const QString text = QString::fromUtf8(bytes);
    const QRegularExpressionMatch match = QRegularExpression(pattern).match(text);
    if (!match.hasMatch())
        return;
    const int offset = match.capturedStart();
    int l = 1;
    int lastBreak = -1;
    for (int i = 0; i < offset; ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++l;
            lastBreak = i;
        }
    }
    *line = l;
    *column = offset - lastBreak;
}

QList<Problem> lintProject(const FileMap &files, const QString &rootDir, const LintContext &context)
{
    return Linter(files, rootDir, context).run();
}

QString formatProblems(const QList<Problem> &problems)
{
    QString out;
    for (const Problem &p : problems)
        out += QStringLiteral("%1:%2:%3: %4: %5\n").arg(p.file).arg(p.line).arg(p.column).arg(p.severity, p.message);
    return out;
}

} // namespace drift::projdir
