#include "app/forge/doc/ForgeDoc.h"

#include <QJsonDocument>

namespace drift::forge {

namespace {

constexpr struct { ParamType type; const char *name; } kParamTypes[] = {
    {ParamType::Float, "float"}, {ParamType::Bool, "bool"}, {ParamType::Color, "color"},
    {ParamType::Int, "int"}, {ParamType::Point, "point"}, {ParamType::Choice, "choice"},
    {ParamType::Seed, "seed"}, {ParamType::Region, "region"}, {ParamType::Image, "image"},
    {ParamType::Track, "track"}, {ParamType::TrackMask, "trackMask"}, {ParamType::Audio, "audio"},
    {ParamType::Gradient, "gradient"}, {ParamType::Curve, "curve"},
};

QJsonObject without(const QJsonObject &o, std::initializer_list<const char *> known)
{
    QJsonObject out = o;
    for (const char *k : known)
        out.remove(QLatin1String(k));
    return out;
}

void putExtra(QJsonObject &o, const QJsonObject &extra)
{
    for (auto it = extra.begin(); it != extra.end(); ++it)
        o.insert(it.key(), it.value());
}

QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &s : v.toArray())
        out << s.toString();
    return out;
}

QJsonArray fromStringList(const QStringList &l)
{
    QJsonArray a;
    for (const QString &s : l)
        a.append(s);
    return a;
}

ParamDef paramFromJsonImpl(const QJsonObject &o, bool upgradeClip)
{
    ParamDef p;
    p.identifier = o.value("identifier").toString();
    p.displayName = o.value("displayName").toString();
    QString type = o.value("type").toString();
    if (upgradeClip && type == QLatin1String("clip"))
        type = QStringLiteral("track");
    paramTypeFromName(type, &p.type);
    p.min = o.value("min").toDouble();
    p.max = o.value("max").toDouble();
    if (o.contains("group"))
        p.group = o.value("group").toString();
    if (o.contains("options"))
        p.options = toStringList(o.value("options"));
    if (o.contains("alpha"))
        p.alpha = o.value("alpha").toBool();
    if (o.contains("shape"))
        p.shape = o.value("shape").toString();
    p.ui = o.value("ui").toObject();
    if (o.contains("showWhen")) {
        const QJsonObject s = o.value("showWhen").toObject();
        p.showWhen = ShowWhen{s.value("param").toString(), s.value("equals").toBool()};
    }
    p.defaultValue = o.value("default");
    p.extra = without(o, {"identifier", "displayName", "type", "min", "max", "group", "options", "alpha", "shape",
                          "ui", "showWhen", "default"});
    return p;
}

}

ParamDef paramFromJson(const QJsonObject &o)
{
    return paramFromJsonImpl(o, false);
}

bool isParamRef(const QJsonValue &v)
{
    return v.isObject() && v.toObject().contains("param");
}

QStringList paramRefNames(const QJsonValue &v)
{
    if (!isParamRef(v))
        return {};
    const QJsonValue p = v.toObject().value("param");
    if (p.isArray())
        return toStringList(p);
    return {p.toString()};
}

QJsonObject makeParamRef(const QString &name)
{
    return QJsonObject{{"param", name}};
}

QString paramTypeName(ParamType t)
{
    for (const auto &e : kParamTypes)
        if (e.type == t)
            return QLatin1String(e.name);
    return {};
}

bool paramTypeFromName(const QString &name, ParamType *out)
{
    for (const auto &e : kParamTypes) {
        if (name == QLatin1String(e.name)) {
            if (out)
                *out = e.type;
            return true;
        }
    }
    return false;
}

const ForgeNode *ForgeDoc::node(const QString &id) const
{
    for (const ForgeNode &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

ForgeNode *ForgeDoc::node(const QString &id)
{
    return const_cast<ForgeNode *>(std::as_const(*this).node(id));
}

const ParamDef *ForgeDoc::param(const QString &identifier) const
{
    for (const ParamDef &p : params)
        if (p.identifier == identifier)
            return &p;
    return nullptr;
}

ParamDef *ForgeDoc::param(const QString &identifier)
{
    return const_cast<ParamDef *>(std::as_const(*this).param(identifier));
}

ForgeDoc ForgeDoc::fromJsonText(const QByteArray &text, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument jd = QJsonDocument::fromJson(text, &pe);
    if (pe.error != QJsonParseError::NoError || !jd.isObject()) {
        if (error)
            *error = QStringLiteral("The file isn't valid JSON.");
        return {};
    }
    return fromJson(jd.object(), error);
}

ForgeDoc ForgeDoc::fromJson(const QJsonObject &o, QString *error)
{
    const auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return ForgeDoc{};
    };
    if (error)
        error->clear();
    if (!o.contains("forge"))
        return fail(QStringLiteral("This isn't a Drift Forge document."));
    const int schema = o.value("forge").toInt();
    if (schema > kForgeSchema)
        return fail(QStringLiteral("This was made with a newer Drift Forge."));
    ForgeDoc d;
    if (!kindFromName(o.value("kind").toString(), &d.kind))
        return fail(QStringLiteral("Unknown document kind."));
    if (!o.value("nodes").isArray() || !o.value("edges").isArray() || !o.value("params").isArray()
        || !o.value("meta").isObject())
        return fail(QStringLiteral("The document is incomplete."));

    const QJsonObject m = o.value("meta").toObject();
    d.meta.id = m.value("id").toString();
    d.meta.displayName = m.value("displayName").toString();
    d.meta.category = m.value("category").toString();
    d.meta.description = m.value("description").toString();
    d.meta.author = m.value("author").toString();
    d.meta.version = m.value("version").toString();
    if (m.contains("directions"))
        d.meta.directions = toStringList(m.value("directions"));
    if (m.contains("defaultDurationMs"))
        d.meta.defaultDurationMs = m.value("defaultDurationMs").toDouble();
    d.meta.extra = without(m, {"id", "displayName", "category", "description", "author", "version", "directions",
                               "defaultDurationMs"});

    for (const QJsonValue &v : o.value("params").toArray())
        d.params.append(paramFromJsonImpl(v.toObject(), schema < 3));
    for (const QJsonValue &v : o.value("nodes").toArray()) {
        const QJsonObject n = v.toObject();
        ForgeNode node;
        node.id = n.value("id").toString();
        node.type = n.value("type").toString();
        node.x = n.value("x").toDouble();
        node.y = n.value("y").toDouble();
        node.inputs = n.value("inputs").toObject();
        node.data = n.value("data").toObject();
        node.extra = without(n, {"id", "type", "x", "y", "inputs", "data"});
        d.nodes.append(node);
    }
    for (const QJsonValue &v : o.value("edges").toArray()) {
        const QJsonObject e = v.toObject();
        d.edges.append({e.value("id").toString(), e.value("from").toString(), e.value("fromSocket").toString(),
                        e.value("to").toString(), e.value("toSocket").toString(),
                        without(e, {"id", "from", "fromSocket", "to", "toSocket"})});
    }
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        d.assets.append({a.value("id").toString(), a.value("name").toString(), a.value("mime").toString(),
                         a.value("data").toString(), a.value("width").toDouble(), a.value("height").toDouble(),
                         without(a, {"id", "name", "mime", "data", "width", "height"})});
    }
    if (o.contains("presets")) {
        QList<Preset> presets;
        for (const QJsonValue &v : o.value("presets").toArray()) {
            const QJsonObject p = v.toObject();
            presets.append({p.value("name").toString(), p.value("values").toObject(), without(p, {"name", "values"})});
        }
        d.presets = presets;
    }

    if (o.contains("preview")) {
        const QJsonObject p = o.value("preview").toObject();
        d.preview.thumbTime = p.value("thumbTime").toDouble(0.5);
        if (p.contains("customThumb"))
            d.preview.customThumb = p.value("customThumb").toString();
        if (p.contains("clip"))
            d.preview.clip = p.value("clip").toString();
        if (p.contains("input"))
            d.preview.input = p.value("input").toObject();
        d.preview.extra = without(p, {"thumbTime", "customThumb", "clip", "input"});
    }

    if (o.contains("audio"))
        d.audio = o.value("audio").toObject();
    if (d.kind == Kind::Audio) {
        if (d.audio && !d.audio->contains("rack") && d.audio->contains("processor"))
            return fail(QStringLiteral("Schema-1 audio documents aren't supported; open and re-save it in Drift Forge on the web."));
        if (!d.audio)
            d.audio = QJsonObject{{"rack", QJsonObject{{"chain", QJsonArray()}, {"modulators", QJsonArray()}, {"routes", QJsonArray()}}}};
    }

    d.extra = without(o, {"forge", "kind", "meta", "params", "nodes", "edges", "assets", "presets", "audio", "preview"});
    return d;
}

QJsonObject paramToJson(const ParamDef &p)
{
    QJsonObject o = p.extra;
    o["identifier"] = p.identifier;
    o["displayName"] = p.displayName;
    o["type"] = paramTypeName(p.type);
    o["min"] = p.min;
    o["max"] = p.max;
    o["default"] = p.defaultValue;
    if (p.group)
        o["group"] = *p.group;
    if (p.options)
        o["options"] = fromStringList(*p.options);
    if (p.alpha)
        o["alpha"] = *p.alpha;
    if (p.shape)
        o["shape"] = *p.shape;
    if (!p.ui.isEmpty())
        o["ui"] = p.ui;
    if (p.showWhen)
        o["showWhen"] = QJsonObject{{"param", p.showWhen->param}, {"equals", p.showWhen->equals}};
    return o;
}

QJsonObject ForgeDoc::toJson() const
{
    QJsonObject o = extra;
    o["forge"] = kForgeSchema;
    o["kind"] = kindInfo(kind).name;

    QJsonObject m = meta.extra;
    m["id"] = meta.id;
    m["displayName"] = meta.displayName;
    m["category"] = meta.category;
    m["description"] = meta.description;
    m["author"] = meta.author;
    m["version"] = meta.version;
    if (meta.directions)
        m["directions"] = fromStringList(*meta.directions);
    if (meta.defaultDurationMs)
        m["defaultDurationMs"] = *meta.defaultDurationMs;
    o["meta"] = m;

    QJsonArray ps;
    for (const ParamDef &p : params)
        ps.append(paramToJson(p));
    o["params"] = ps;

    QJsonArray ns;
    for (const ForgeNode &n : nodes) {
        QJsonObject j = n.extra;
        j["id"] = n.id;
        j["type"] = n.type;
        j["x"] = n.x;
        j["y"] = n.y;
        j["inputs"] = n.inputs;
        j["data"] = n.data;
        ns.append(j);
    }
    o["nodes"] = ns;

    QJsonArray es;
    for (const ForgeEdge &e : edges) {
        QJsonObject j = e.extra;
        j["id"] = e.id;
        j["from"] = e.from;
        j["fromSocket"] = e.fromSocket;
        j["to"] = e.to;
        j["toSocket"] = e.toSocket;
        es.append(j);
    }
    o["edges"] = es;

    QJsonArray as;
    for (const ForgeAsset &a : assets) {
        QJsonObject j = a.extra;
        j["id"] = a.id;
        j["name"] = a.name;
        j["mime"] = a.mime;
        j["data"] = a.data;
        j["width"] = a.width;
        j["height"] = a.height;
        as.append(j);
    }
    o["assets"] = as;

    if (presets) {
        QJsonArray pr;
        for (const Preset &p : *presets) {
            QJsonObject j = p.extra;
            j["name"] = p.name;
            j["values"] = p.values;
            pr.append(j);
        }
        o["presets"] = pr;
    }
    if (audio)
        o["audio"] = *audio;

    QJsonObject pv = preview.extra;
    pv["thumbTime"] = preview.thumbTime;
    if (preview.customThumb)
        pv["customThumb"] = *preview.customThumb;
    if (preview.clip)
        pv["clip"] = *preview.clip;
    if (preview.input)
        pv["input"] = *preview.input;
    o["preview"] = pv;
    return o;
}

}
