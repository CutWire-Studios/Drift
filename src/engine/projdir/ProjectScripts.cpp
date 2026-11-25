#include "engine/projdir/ProjectScripts.h"

#include "core/project/folder/ProjectData.h"
#include "core/project/folder/ProjectSchema.h"
#include "engine/procedural/LuaSandbox.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace drift::projdir {

namespace {

// The drift API. Plain Lua over two C functions (data, param) so that what a script can do is easy to
// read here. Everything a script makes goes into __gen; C++ merges it into the project afterwards.
constexpr const char *kPrelude = R"LUA(
local gen = { tracks = {}, clips = {}, assets = {} }
__gen = gen
local rawProject = project
local script, slug = __script, __slug
local fps = rawProject.fps or 30

local function readonly(t)
  return setmetatable({}, {
    __index = t,
    __newindex = function() error("project is read-only; use track{} and clip{} to add to it", 2) end,
    __len = function() return #t end,
    __pairs = function() return next, t, nil end,
  })
end
project = readonly(rawProject)
for _, name in ipairs({ "tracks", "assets" }) do
  if rawProject[name] then rawProject[name] = readonly(rawProject[name]) end
end

function sec(s) return math.floor(s * 1000000 + 0.5) end
function frames(n) return math.floor(n * 1000000 / fps + 0.5) end
function beats(bpm, n) return math.floor(n * 60 * 1000000 / bpm + 0.5) end
function tc(text)
  local parts = {}
  for n in string.gmatch(text, "%d+") do parts[#parts + 1] = tonumber(n) end
  if #parts == 0 then error("bad timecode '" .. tostring(text) .. "'", 2) end
  local f = table.remove(parts)
  local s = 0
  for _, p in ipairs(parts) do s = s * 60 + p end
  return math.floor((s + f / fps) * 1000000 + 0.5)
end

local aliases = { start = "timelineStartUs", duration = "timelineDurationUs", srcIn = "srcInUs", srcOut = "srcOutUs" }
local clipForTrack = { video = "video", audio = "audio", text = "text", shape = "shape", subtitle = "subtitle", adjustment = "adjustment" }

local ownedTracks = {}
for _, t in ipairs(rawProject.tracks or {}) do ownedTracks[t.id] = t end

local Track = {}
Track.__index = Track

function track(spec)
  spec = spec or {}
  local id = spec.id or (slug .. "-track-" .. (#gen.tracks + 1))
  local existing = ownedTracks[id]
  local handle = setmetatable({ id = id, type = spec.type or (existing and existing.type) or "video" }, Track)
  if not existing then
    spec.id = id
    spec.type = handle.type
    spec.generatedBy = script
    spec.clips = nil
    gen.tracks[#gen.tracks + 1] = spec
    ownedTracks[id] = spec
  end
  return handle
end

function Track:clip(spec)
  spec = spec or {}
  for short, key in pairs(aliases) do
    if spec[short] ~= nil then spec[key] = spec[short]; spec[short] = nil end
  end
  spec.type = spec.type or clipForTrack[self.type] or "video"
  spec.id = spec.id or (slug .. "-clip-" .. (#gen.clips + 1))
  spec.generatedBy = script
  gen.clips[#gen.clips + 1] = { track = self.id, clip = spec }
  return spec
end

local titles
function title(spec)
  spec = spec or {}
  titles = titles or track { id = slug .. "-titles", name = "Titles", type = "text" }
  spec.type = "text"
  if spec.text ~= nil then spec.textContent = spec.text; spec.text = nil end
  return titles:clip(spec)
end

function asset(spec)
  spec = spec or {}
  spec.id = spec.id or (slug .. "-asset-" .. (#gen.assets + 1))
  spec.generatedBy = script
  gen.assets[#gen.assets + 1] = spec
  return spec
end

local dataGet, paramGet = __data, __param
function data(name) return dataGet(name) end
function param(name) return paramGet(name) end
__data, __param, __script, __slug = nil, nil, nil, nil
)LUA";

struct RunState
{
    const ScriptInput *input = nullptr;
    int dataBytes = 0;
};

constexpr int kMaxDepth = 24;
constexpr int kMaxDataBytes = 8 * 1024 * 1024;

void pushJson(lua_State *L, const QJsonValue &v, int depth = 0)
{
    if (depth > kMaxDepth) {
        lua_pushnil(L);
        return;
    }
    switch (v.type()) {
    case QJsonValue::Bool:
        lua_pushboolean(L, v.toBool());
        break;
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::fabs(d) < 9e15)
            lua_pushinteger(L, static_cast<lua_Integer>(d));
        else
            lua_pushnumber(L, d);
        break;
    }
    case QJsonValue::String: {
        const QByteArray s = v.toString().toUtf8();
        lua_pushlstring(L, s.constData(), static_cast<size_t>(s.size()));
        break;
    }
    case QJsonValue::Array: {
        const QJsonArray array = v.toArray();
        lua_createtable(L, int(array.size()), 0);
        for (int i = 0; i < array.size(); ++i) {
            pushJson(L, array.at(i), depth + 1);
            lua_rawseti(L, -2, i + 1);
        }
        break;
    }
    case QJsonValue::Object: {
        const QJsonObject object = v.toObject();
        lua_createtable(L, 0, int(object.size()));
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            pushJson(L, it.value(), depth + 1);
            const QByteArray key = it.key().toUtf8();
            lua_setfield(L, -2, key.constData());
        }
        break;
    }
    default:
        lua_pushnil(L);
    }
}

QJsonValue toJson(lua_State *L, int index, int depth = 0)
{
    index = lua_absindex(L, index);
    if (depth > kMaxDepth)
        return QJsonValue();
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
        return lua_isinteger(L, index) ? double(lua_tointeger(L, index)) : double(lua_tonumber(L, index));
    case LUA_TSTRING: {
        size_t len = 0;
        const char *s = lua_tolstring(L, index, &len);
        return QString::fromUtf8(s, int(len));
    }
    case LUA_TTABLE: {
        const lua_Unsigned n = lua_rawlen(L, index);
        lua_Unsigned count = 0;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            ++count;
            lua_pop(L, 1);
        }
        if (n > 0 && count == n) {
            QJsonArray array;
            for (lua_Unsigned i = 1; i <= n; ++i) {
                lua_rawgeti(L, index, lua_Integer(i));
                array.append(toJson(L, -1, depth + 1));
                lua_pop(L, 1);
            }
            return array;
        }
        QJsonObject object;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            QString key;
            if (lua_type(L, -2) == LUA_TSTRING)
                key = QString::fromUtf8(lua_tostring(L, -2));
            else if (lua_isinteger(L, -2))
                key = QString::number(lua_tointeger(L, -2));
            if (!key.isEmpty() && lua_type(L, -1) != LUA_TFUNCTION && lua_type(L, -1) != LUA_TUSERDATA)
                object.insert(key, toJson(L, -1, depth + 1));
            lua_pop(L, 1);
        }
        return object;
    }
    default:
        return QJsonValue();
    }
}

RunState *state(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "drift.projdir.state");
    auto *s = static_cast<RunState *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return s;
}

int luaData(lua_State *L)
{
    RunState *s = state(L);
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    if (name.contains(QLatin1String("..")) || name.startsWith(QLatin1Char('/')))
        return luaL_error(L, "data file names stay inside data/");
    const QString path = QStringLiteral("data/") + name;
    if (!s->input->data.contains(path))
        return luaL_error(L, "no such data file: %s", qPrintable(path));
    const QByteArray bytes = s->input->data.value(path);
    s->dataBytes += int(bytes.size());
    if (s->dataBytes > kMaxDataBytes)
        return luaL_error(L, "data files are too large to load");
    QString error;
    const QJsonArray rows = parseDataRows(name, bytes, &error);
    if (!error.isEmpty())
        return luaL_error(L, "%s: %s", qPrintable(path), qPrintable(error));
    pushJson(L, rows);
    return 1;
}

int luaParam(lua_State *L)
{
    RunState *s = state(L);
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    pushJson(L, s->input->params.value(name));
    return 1;
}

// What a script may read of the project: ids, types and times, not every style key.
QJsonObject projectView(const QJsonObject &doc)
{
    QJsonArray tracks;
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject track = t.toObject();
        QJsonArray clips;
        for (const QJsonValue &c : track.value(QStringLiteral("clips")).toArray()) {
            const QJsonObject clip = c.toObject();
            QJsonObject view;
            for (const char *key : {"id", "type", "name", "assetId", "timelineStartUs", "timelineDurationUs",
                                    "generatedBy"})
                view.insert(QLatin1String(key), clip.value(QLatin1String(key)));
            clips.append(view);
        }
        tracks.append(QJsonObject{{QStringLiteral("id"), track.value(QStringLiteral("id"))},
                                  {QStringLiteral("type"), track.value(QStringLiteral("type"))},
                                  {QStringLiteral("name"), track.value(QStringLiteral("name"))},
                                  {QStringLiteral("generatedBy"), track.value(QStringLiteral("generatedBy"))},
                                  {QStringLiteral("clips"), clips}});
    }
    QJsonArray assets;
    for (const QJsonValue &a : doc.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = a.toObject();
        QJsonObject view;
        for (const char *key : {"id", "name", "kind", "path", "durationUs", "width", "height"})
            view.insert(QLatin1String(key), asset.value(QLatin1String(key)));
        assets.append(view);
    }
    return {{QStringLiteral("fps"), doc.value(QStringLiteral("fps")).toInt(30)},
            {QStringLiteral("width"), doc.value(QStringLiteral("width")).toInt(1920)},
            {QStringLiteral("height"), doc.value(QStringLiteral("height")).toInt(1080)},
            {QStringLiteral("sampleRate"), doc.value(QStringLiteral("sampleRate")).toInt(48000)},
            {QStringLiteral("name"), doc.value(QStringLiteral("projectName")).toString()},
            {QStringLiteral("tracks"), tracks},
            {QStringLiteral("assets"), assets}};
}

// A table that came back empty from Lua is {} whatever it was meant to be; the schema knows which keys
// are lists.
void coerceLists(QJsonObject &object, const QString &kindName)
{
    const KindDesc *kind = kindDescription(kindName);
    if (!kind)
        return;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const FieldDesc *field = kind->field(it.key());
        if (field && (field->type == QLatin1String("array") || field->type == QLatin1String("effects"))
            && it.value().isObject() && it.value().toObject().isEmpty())
            it.value() = QJsonArray();
    }
}

void absolutizePath(QJsonObject &object, const QString &root)
{
    const QString path = object.value(QStringLiteral("path")).toString();
    if (!path.isEmpty() && !root.isEmpty() && QDir::isRelativePath(path) && !path.contains(QLatin1String("://")))
        object.insert(QStringLiteral("path"), QDir::cleanPath(root + QLatin1Char('/') + path));
}

QJsonObject withoutOutputOf(QJsonObject doc, const QString &script)
{
    QJsonArray tracks;
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray()) {
        QJsonObject track = t.toObject();
        if (track.value(QStringLiteral("generatedBy")).toString() == script)
            continue;
        QJsonArray clips;
        for (const QJsonValue &c : track.value(QStringLiteral("clips")).toArray()) {
            if (c.toObject().value(QStringLiteral("generatedBy")).toString() != script)
                clips.append(c);
        }
        track.insert(QStringLiteral("clips"), clips);
        tracks.append(track);
    }
    doc.insert(QStringLiteral("tracks"), tracks);
    QJsonArray assets;
    for (const QJsonValue &a : doc.value(QStringLiteral("assets")).toArray()) {
        if (a.toObject().value(QStringLiteral("generatedBy")).toString() != script)
            assets.append(a);
    }
    doc.insert(QStringLiteral("assets"), assets);
    return doc;
}

Problem scriptProblem(const QString &script, const QString &message)
{
    Problem p;
    p.file = script;
    p.line = 1;
    p.column = 1;
    p.message = message;
    // "scripts/x.lua:12: message" then a traceback.
    const QString first = message.section(QLatin1Char('\n'), 0, 0);
    static const QRegularExpression at(QStringLiteral("^[^:]+:(\\d+): (.*)$"));
    const QRegularExpressionMatch match = at.match(first);
    if (match.hasMatch()) {
        p.line = match.captured(1).toInt();
        p.message = match.captured(2);
    } else {
        p.message = first;
    }
    return p;
}

} // namespace

ScriptOutcome runScripts(const QJsonObject &startDoc, const ScriptInput &input)
{
    ScriptOutcome outcome;
    QJsonObject doc = startDoc;
    QStringList names = input.scripts.keys();
    names.removeIf([](const QString &n) {
        return !n.endsWith(QLatin1String(".lua")) || n.section(QLatin1Char('/'), 1).contains(QLatin1Char('/'));
    });
    std::sort(names.begin(), names.end());

    for (const QString &script : std::as_const(names)) {
        const QJsonObject previous = doc;
        const QJsonObject before = withoutOutputOf(doc, script);
        const QString slug = slugify(QFileInfo(script).completeBaseName());

        LuaSandbox::Limits limits;
        limits.memoryBytes = 128u * 1024u * 1024u;
        limits.deadline = std::chrono::milliseconds(3000);
        LuaSandbox sandbox(limits);
        lua_State *L = sandbox.state();
        RunState runState{&input};
        lua_pushlightuserdata(L, &runState);
        lua_setfield(L, LUA_REGISTRYINDEX, "drift.projdir.state");
        lua_pushcfunction(L, luaData);
        lua_setglobal(L, "__data");
        lua_pushcfunction(L, luaParam);
        lua_setglobal(L, "__param");
        pushJson(L, projectView(before));
        lua_setglobal(L, "project");
        const QByteArray scriptName = script.toUtf8();
        lua_pushlstring(L, scriptName.constData(), size_t(scriptName.size()));
        lua_setglobal(L, "__script");
        const QByteArray slugBytes = slug.toUtf8();
        lua_pushlstring(L, slugBytes.constData(), size_t(slugBytes.size()));
        lua_setglobal(L, "__slug");

        std::string error;
        bool ok = sandbox.run(kPrelude, "=drift", &error);
        if (ok) {
            const QByteArray chunk = "@" + scriptName;
            ok = sandbox.run(input.scripts.value(script).toStdString(), chunk.constData(), &error);
        }
        for (const std::string &line : sandbox.takeLog())
            outcome.log.append(script + QStringLiteral(": ") + QString::fromStdString(line));
        if (!ok) {
            outcome.problems.append(scriptProblem(script, QString::fromStdString(error)));
            continue;
        }

        lua_getglobal(L, "__gen");
        const QJsonObject gen = toJson(L, -1).toObject();
        lua_pop(L, 1);

        doc = before;
        QJsonArray tracks = doc.value(QStringLiteral("tracks")).toArray();
        QJsonArray assets = doc.value(QStringLiteral("assets")).toArray();
        QSet<QString> clipIds;
        QSet<QString> assetIds;
        for (const QJsonValue &t : tracks) {
            for (const QJsonValue &c : t.toObject().value(QStringLiteral("clips")).toArray())
                clipIds.insert(c.toObject().value(QStringLiteral("id")).toString());
        }
        for (const QJsonValue &a : assets)
            assetIds.insert(a.toObject().value(QStringLiteral("id")).toString());

        bool failed = false;
        const auto fail = [&](const QString &message) {
            outcome.problems.append({script, 1, 1, message});
            failed = true;
        };
        for (const QJsonValue &t : gen.value(QStringLiteral("tracks")).toArray()) {
            QJsonObject track = t.toObject();
            track.insert(QStringLiteral("clips"), QJsonArray());
            track.insert(QStringLiteral("transitions"), QJsonArray());
            coerceLists(track, QStringLiteral("track"));
            tracks.append(track);
        }
        QHash<QString, int> trackIndex;
        for (int i = 0; i < tracks.size(); ++i)
            trackIndex.insert(tracks.at(i).toObject().value(QStringLiteral("id")).toString(), i);
        QHash<int, QJsonArray> added;
        for (const QJsonValue &entry : gen.value(QStringLiteral("clips")).toArray()) {
            const QString trackId = entry.toObject().value(QStringLiteral("track")).toString();
            QJsonObject clip = entry.toObject().value(QStringLiteral("clip")).toObject();
            const QString id = clip.value(QStringLiteral("id")).toString();
            if (!trackIndex.contains(trackId)) {
                fail(QStringLiteral("clip \"%1\" is on unknown track \"%2\"").arg(id, trackId));
                break;
            }
            if (clipIds.contains(id)) {
                fail(QStringLiteral("clip id \"%1\" is already used").arg(id));
                break;
            }
            clipIds.insert(id);
            coerceLists(clip, QStringLiteral("clip"));
            absolutizePath(clip, input.rootDir);
            added[trackIndex.value(trackId)].append(clip);
        }
        for (const QJsonValue &a : gen.value(QStringLiteral("assets")).toArray()) {
            QJsonObject asset = a.toObject();
            const QString id = asset.value(QStringLiteral("id")).toString();
            if (assetIds.contains(id)) {
                fail(QStringLiteral("asset id \"%1\" is already used").arg(id));
                break;
            }
            assetIds.insert(id);
            coerceLists(asset, QStringLiteral("asset"));
            absolutizePath(asset, input.rootDir);
            assets.append(asset);
        }
        if (failed) {
            doc = previous;
            continue;
        }
        for (auto it = added.constBegin(); it != added.constEnd(); ++it) {
            QJsonObject track = tracks.at(it.key()).toObject();
            QList<QJsonObject> clips;
            for (const QJsonValue &c : track.value(QStringLiteral("clips")).toArray())
                clips.append(c.toObject());
            for (const QJsonValue &c : it.value())
                clips.append(c.toObject());
            std::stable_sort(clips.begin(), clips.end(), [](const QJsonObject &a, const QJsonObject &b) {
                return a.value(QStringLiteral("timelineStartUs")).toDouble()
                    < b.value(QStringLiteral("timelineStartUs")).toDouble();
            });
            QJsonArray out;
            for (const QJsonObject &c : clips)
                out.append(c);
            track.insert(QStringLiteral("clips"), out);
            tracks[it.key()] = track;
        }
        doc.insert(QStringLiteral("tracks"), tracks);
        doc.insert(QStringLiteral("assets"), assets);
    }
    outcome.doc = doc;
    return outcome;
}

QJsonObject detachGenerated(QJsonObject doc, const QString &script)
{
    const auto owned = [&](const QJsonObject &o) {
        const QString by = o.value(QStringLiteral("generatedBy")).toString();
        return !by.isEmpty() && (script.isEmpty() || by == script);
    };
    QJsonArray tracks;
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray()) {
        QJsonObject track = t.toObject();
        if (owned(track))
            track.remove(QStringLiteral("generatedBy"));
        QJsonArray clips;
        for (const QJsonValue &c : track.value(QStringLiteral("clips")).toArray()) {
            QJsonObject clip = c.toObject();
            if (owned(clip))
                clip.remove(QStringLiteral("generatedBy"));
            clips.append(clip);
        }
        track.insert(QStringLiteral("clips"), clips);
        tracks.append(track);
    }
    doc.insert(QStringLiteral("tracks"), tracks);
    QJsonArray assets;
    for (const QJsonValue &a : doc.value(QStringLiteral("assets")).toArray()) {
        QJsonObject asset = a.toObject();
        if (owned(asset))
            asset.remove(QStringLiteral("generatedBy"));
        assets.append(asset);
    }
    doc.insert(QStringLiteral("assets"), assets);
    return doc;
}

namespace {

FileMap readTree(const QString &dir, const QString &sub, const QString &suffixFilter, bool recursive)
{
    FileMap files;
    const QDir base(dir);
    const QString root = base.filePath(sub);
    if (!QFileInfo(root).isDir())
        return files;
    QDirIterator it(root, QDir::Files, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!suffixFilter.isEmpty() && !path.endsWith(suffixFilter))
            continue;
        QFile file(path);
        if (file.size() > 16 * 1024 * 1024 || !file.open(QIODevice::ReadOnly))
            continue;
        files.insert(base.relativeFilePath(path), file.readAll());
    }
    return files;
}

} // namespace

FileMap readScripts(const QString &dir)
{
    return readTree(dir, QStringLiteral("scripts"), QStringLiteral(".lua"), false);
}

FileMap readData(const QString &dir)
{
    return readTree(dir, QStringLiteral("data"), {}, true);
}

QByteArray luaDefinitions()
{
    return R"LUA(---@meta
-- Types for the drift scripting API. Written by Drift; changes here are overwritten.

---A track a script made or found. Add clips with `t:clip{}`.
---@class DriftTrack
---@field id string
---@field type string
local DriftTrack = {}

---@class DriftClipSpec
---@field id? string       Stable id. Without one the clip is numbered in creation order.
---@field type? string     video, audio, image, text, subtitle, shape, adjustment, ... (default: the track's type)
---@field start? integer   Timeline start in microseconds (alias of timelineStartUs).
---@field duration? integer Length on the timeline in microseconds (alias of timelineDurationUs).
---@field srcIn? integer   Source in point in microseconds (alias of srcInUs).
---@field srcOut? integer  Source out point in microseconds (alias of srcOutUs).
---@field assetId? string
---@field name? string
---@field textContent? string
---@field [string] any     Any other clip key; see .drift/schema/clip in track.schema.json.

---Adds a clip to the track. The clip is stamped generatedBy this script.
---@param spec DriftClipSpec
---@return DriftClipSpec
function DriftTrack:clip(spec) end

---@class DriftTrackSpec
---@field id? string       An existing track's id returns that track; a new id creates it.
---@field name? string
---@field type? string     video, audio, text, subtitle, shape, adjustment (default video)

---Creates a track (or returns the existing one with that id).
---@param spec? DriftTrackSpec
---@return DriftTrack
function track(spec) end

---@class DriftTitleSpec : DriftClipSpec
---@field text? string     Alias of textContent.

---Adds a text clip to this script's "Titles" track.
---@param spec DriftTitleSpec
---@return DriftClipSpec
function title(spec) end

---@class DriftAssetSpec
---@field id? string
---@field name? string
---@field kind? string     video, audio, image, ...
---@field path? string     Relative to the project folder, or absolute.
---@field durationUs? integer

---Declares a media asset.
---@param spec DriftAssetSpec
---@return DriftAssetSpec
function asset(spec) end

---Rows of data/<name>: a CSV (header row, numbers converted) or a JSON array of objects.
---@param name string
---@return table[]
function data(name) end

---The value of a template parameter (or the row's override), nil when there is none.
---@param name string
---@return any
function param(name) end

---Seconds to microseconds.
---@param s number
---@return integer
function sec(s) end

---Frames at the project frame rate to microseconds.
---@param n number
---@return integer
function frames(n) end

---Time of beat `n` at `bpm`, in microseconds.
---@param bpm number
---@param n number
---@return integer
function beats(bpm, n) end

---Timecode "HH:MM:SS:FF" (or "MM:SS:FF", "SS:FF") to microseconds at the project frame rate.
---@param text string
---@return integer
function tc(text) end

---@class DriftProjectClip
---@field id string
---@field type string
---@field name string
---@field assetId string
---@field timelineStartUs integer
---@field timelineDurationUs integer
---@field generatedBy string

---@class DriftProjectTrack
---@field id string
---@field type string
---@field name string
---@field clips DriftProjectClip[]

---@class DriftProject
---@field fps integer
---@field width integer
---@field height integer
---@field sampleRate integer
---@field name string
---@field tracks DriftProjectTrack[]
---@field assets table[]

---The declarative project as it was before scripts ran. Read-only.
---@type DriftProject
project = nil
)LUA";
}

QByteArray luarcJson()
{
    return R"({
  "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
  "runtime.version": "Lua 5.4",
  "workspace.library": [".drift/lua"],
  "workspace.checkThirdParty": false,
  "diagnostics.globals": ["track", "title", "asset", "data", "param", "sec", "frames", "beats", "tc", "project"]
}
)";
}

} // namespace drift::projdir
