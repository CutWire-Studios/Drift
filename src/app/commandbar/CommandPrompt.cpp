#include "app/commandbar/CommandPrompt.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocale>
#include <QPair>
#include <QRegularExpression>

#include <cmath>

namespace drift::commandbar {

const char kCommandSystemPrompt[] =
    "You are the command bar of a video editor. Call the tool(s) that carry out the user's request, using the "
    "selected clip and playhead from the context for any clip reference or time. If the request needs several "
    "actions, call several tools. If no tool fits the request, do not call any tool.";
const char kCommandRefusal[] = "No tool fits this request.";

namespace {

constexpr int kTopDesc = 200;
constexpr int kPropDesc = 70;
constexpr int kMaxDepth = 2;
constexpr int kDescDepth = 2;
constexpr int kMaxNestedProps = 14;
constexpr int kMaxEnum = 16;

using Fields = QList<QPair<QString, QString>>;

QString quote(const QString &s)
{
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (const QChar c : s) {
        const ushort u = c.unicode();
        switch (u) {
        case '"': out += QLatin1String("\\\""); break;
        case '\\': out += QLatin1String("\\\\"); break;
        case '\n': out += QLatin1String("\\n"); break;
        case '\r': out += QLatin1String("\\r"); break;
        case '\t': out += QLatin1String("\\t"); break;
        case '\b': out += QLatin1String("\\b"); break;
        case '\f': out += QLatin1String("\\f"); break;
        default:
            if (u < 0x20)
                out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
            else
                out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString number(double d)
{
    if (std::isfinite(d) && std::floor(d) == d && std::fabs(d) < 1e15)
        return QString::number(qint64(d));
    return QString::number(d, 'g', QLocale::FloatingPointShortest);
}

QString dumps(const QJsonValue &v);

QString object(const Fields &fields)
{
    QString out = QStringLiteral("{");
    for (int i = 0; i < fields.size(); ++i) {
        if (i)
            out += QLatin1String(", ");
        out += quote(fields.at(i).first) + QLatin1String(": ") + fields.at(i).second;
    }
    return out + QLatin1Char('}');
}

QString dumps(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return QStringLiteral("null");
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: return number(v.toDouble());
    case QJsonValue::String: return quote(v.toString());
    case QJsonValue::Array: {
        QStringList parts;
        for (const QJsonValue &e : v.toArray())
            parts.append(dumps(e));
        return QLatin1Char('[') + parts.join(QLatin1String(", ")) + QLatin1Char(']');
    }
    case QJsonValue::Object: {
        Fields fields;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            fields.append({it.key(), dumps(it.value())});
        return object(fields);
    }
    }
    return QStringLiteral("null");
}

// Python's str.isspace set, which is what re's \s and str.strip use on text.
bool pySpace(char32_t c)
{
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

// compact_schema.cut, on code points (Python len/slicing count those, QString counts UTF-16 units).
QString cut(const QString &input, int n)
{
    const std::u32string src = input.toStdU32String();
    std::u32string s;
    bool pendingSpace = false;
    for (char32_t c : src) {
        if (pySpace(c)) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !s.empty())
            s.push_back(U' ');
        pendingSpace = false;
        s.push_back(c);
    }
    if (s.compare(0, 6, U"When: ") == 0)
        s.erase(0, 6);
    if (int(s.size()) <= n)
        return QString::fromStdU32String(s);
    std::u32string t = s.substr(0, n);
    long long k = -1;
    for (const char32_t *sep : {U". ", U"; ", U", ", U" "}) {
        const size_t at = t.rfind(sep);
        if (at != std::u32string::npos)
            k = std::max<long long>(k, static_cast<long long>(at));
    }
    if (k > n / 2)
        t.erase(size_t(k));
    while (!t.empty() && (t.back() == U' ' || t.back() == U',' || t.back() == U';' || t.back() == U'.'))
        t.pop_back();
    return QString::fromStdU32String(t);
}

bool truthy(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined: return false;
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: return v.toDouble() != 0;
    case QJsonValue::String: return !v.toString().isEmpty();
    case QJsonValue::Array: return !v.toArray().isEmpty();
    case QJsonValue::Object: return !v.toObject().isEmpty();
    }
    return false;
}

Fields compactFields(const QJsonObject &s, int depth, const QString &name);

QString compactValue(const QJsonValue &v, int depth, const QString &name)
{
    return v.isObject() ? object(compactFields(v.toObject(), depth, name)) : dumps(v);
}

Fields compactFields(const QJsonObject &s, int depth, const QString &name)
{
    Fields out;
    const QJsonValue t = s.value(QStringLiteral("type"));
    if (truthy(t))
        out.append({QStringLiteral("type"), dumps(t)});
    if (s.contains(QStringLiteral("enum"))) {
        QJsonArray e;
        const QJsonArray all = s.value(QStringLiteral("enum")).toArray();
        for (int i = 0; i < all.size() && i < kMaxEnum; ++i)
            e.append(all.at(i));
        out.append({QStringLiteral("enum"), dumps(e)});
    }
    for (const char *key : {"minimum", "maximum"}) {
        if (s.contains(QLatin1String(key)))
            out.append({QLatin1String(key), dumps(s.value(QLatin1String(key)))});
    }
    const QString d = s.value(QStringLiteral("description")).toString();
    if (depth == kDescDepth && !d.isEmpty()
        && (d.startsWith(QLatin1String("Clip UUID")) || d.startsWith(QLatin1String("Clip index"))
            || d.startsWith(QLatin1String("Track index")))
        && (name == QLatin1String("clip") || name == QLatin1String("track") || name == QLatin1String("index"))) {
        const QString boiler = name == QLatin1String("clip") ? QStringLiteral("Clip UUID")
            : name == QLatin1String("track")                 ? QStringLiteral("Track index")
                                                             : QStringLiteral("Clip index on its track");
        out.append({QStringLiteral("description"), quote(boiler)});
    } else if (!d.isEmpty() && depth <= kDescDepth) {
        out.append({QStringLiteral("description"), quote(cut(d, kPropDesc))});
    }
    if (t.isString() && t.toString() == QLatin1String("array") && s.contains(QStringLiteral("items"))) {
        const QJsonValue items = s.value(QStringLiteral("items"));
        if (depth < kMaxDepth) {
            out.append({QStringLiteral("items"), compactValue(items, depth + 1, QString())});
        } else {
            const QJsonValue it = items.toObject().value(QStringLiteral("type"));
            out.append({QStringLiteral("items"),
                        object({{QStringLiteral("type"), it.isUndefined() ? quote(QStringLiteral("string")) : dumps(it)}})});
        }
    }
    if (s.contains(QStringLiteral("properties"))) {
        if (depth >= kMaxDepth + 1)
            return {{QStringLiteral("type"), quote(QStringLiteral("object"))}};
        const QJsonObject props = s.value(QStringLiteral("properties")).toObject();
        const QJsonArray req = s.value(QStringLiteral("required")).toArray();
        QStringList keys = props.keys();
        if (depth >= 2 && keys.size() > kMaxNestedProps) {
            QStringList ordered;
            for (const QString &k : keys) {
                if (req.contains(k))
                    ordered.append(k);
            }
            for (const QString &k : keys) {
                if (!req.contains(k))
                    ordered.append(k);
            }
            keys = ordered.mid(0, kMaxNestedProps);
        }
        Fields propFields;
        for (const QString &k : keys)
            propFields.append({k, compactValue(props.value(k), depth + 1, k)});
        out.append({QStringLiteral("properties"), object(propFields)});
        QStringList kept;
        for (const QJsonValue &r : req) {
            if (keys.contains(r.toString()))
                kept.append(quote(r.toString()));
        }
        if (!req.isEmpty())
            out.append({QStringLiteral("required"), QLatin1Char('[') + kept.join(QLatin1String(", ")) + QLatin1Char(']')});
    }
    return out;
}

QString pyFloat(double v)
{
    const double r = std::round(v * 100.0) / 100.0;
    QString s = QString::number(r, 'g', QLocale::FloatingPointShortest);
    if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')))
        s += QLatin1String(".0");
    return s;
}

QString stripControl(QString q)
{
    for (const char *token : {"<|im_start|>", "<|im_end|>", "<|endoftext|>"})
        q.remove(QLatin1String(token));
    return q.trimmed();
}

} // namespace

QString commandContextLine(const CommandContext &c)
{
    const QString sel = c.hasSelection
        ? QStringLiteral("Selected clip: %1, track %2, index %3.").arg(c.clip).arg(c.track).arg(c.index)
        : QStringLiteral("No clip is selected.");
    return sel + QStringLiteral(" Playhead: ") + pyFloat(c.playhead) + QStringLiteral(" seconds.");
}

QString commandToolJson(const CommandOp &op)
{
    Fields params = compactFields(op.schema, 1, QString());
    bool hasType = false;
    for (const auto &f : params)
        hasType |= f.first == QLatin1String("type");
    if (!hasType)
        params.append({QStringLiteral("type"), quote(QStringLiteral("object"))});
    const QString fn = object({{QStringLiteral("name"), quote(op.name)},
                               {QStringLiteral("description"), quote(cut(op.description, kTopDesc))},
                               {QStringLiteral("parameters"), object(params)}});
    return object({{QStringLiteral("type"), quote(QStringLiteral("function"))}, {QStringLiteral("function"), fn}});
}

QString commandPrompt(const QString &query, const CommandContext &context, const QList<CommandOp> &tools)
{
    QStringList lines;
    for (const CommandOp &op : tools)
        lines.append(commandToolJson(op));
    return QStringLiteral("<|im_start|>system\n") + QString::fromLatin1(kCommandSystemPrompt) + QLatin1Char(' ')
        + commandContextLine(context)
        + QStringLiteral("\n\n# Tools\n\nYou may call one or more functions to assist with the user query.\n\n"
                         "You are provided with function signatures within <tools></tools> XML tags:\n<tools>\n")
        + lines.join(QLatin1Char('\n'))
        + QStringLiteral("\n</tools>\n\nFor each function call, return a json object with function name and arguments "
                         "within <tool_call></tool_call> XML tags:\n<tool_call>\n{\"name\": <function-name>, "
                         "\"arguments\": <args-json-object>}\n</tool_call><|im_end|>\n<|im_start|>user\n")
        + stripControl(query)
        + QStringLiteral("<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n");
}

ParsedCalls parseCommandCalls(const QString &completion)
{
    ParsedCalls result;
    QString text = completion;
    text.remove(QLatin1String("<|im_end|>"));
    if (text.trimmed() == QLatin1String(kCommandRefusal)) {
        result.refusal = true;
        return result;
    }

    QStringList candidates;
    const QLatin1String open("<tool_call>"), close("</tool_call>");
    for (int at = text.indexOf(open); at >= 0;) {
        const int from = at + open.size();
        const int end = text.indexOf(close, from);
        if (end < 0) {
            candidates.append(text.mid(from).trimmed());
            break;
        }
        candidates.append(text.mid(from, end - from).trimmed());
        at = text.indexOf(open, end + close.size());
    }
    if (candidates.isEmpty())
        candidates.append(text.trimmed());

    for (QString c : candidates) {
        if (c.startsWith(QLatin1String("```"))) {
            c.remove(QRegularExpression(QStringLiteral("^```\\w*")));
            if (c.endsWith(QLatin1String("```")))
                c.chop(3);
            c = c.trimmed();
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(c.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError)
            continue;
        QJsonArray items;
        if (doc.isArray())
            items = doc.array();
        else
            items.append(doc.object());
        for (const QJsonValue &v : items) {
            const QJsonObject o = v.toObject();
            if (!o.contains(QStringLiteral("name")))
                continue;
            QJsonValue args = o.contains(QStringLiteral("arguments")) ? o.value(QStringLiteral("arguments"))
                                                                      : o.value(QStringLiteral("parameters"));
            if (args.isString()) {
                QJsonParseError e2;
                const QJsonDocument inner = QJsonDocument::fromJson(args.toString().toUtf8(), &e2);
                if (e2.error != QJsonParseError::NoError || !inner.isObject())
                    continue;
                args = inner.object();
            }
            result.calls.append({o.value(QStringLiteral("name")).toString(), args.toObject()});
        }
    }
    return result;
}

} // namespace drift::commandbar
