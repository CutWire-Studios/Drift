#include "core/project/folder/ProjectSchema.h"

#include "core/project/Project.h"
#include "core/project/folder/ProjectFormat.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace drift::projdir {

namespace {

FieldDesc f(const char *key, const char *type, const char *doc, const char *items = "")
{
    FieldDesc d;
    d.key = QLatin1String(key);
    d.type = QLatin1String(type);
    d.doc = QLatin1String(doc);
    d.items = QLatin1String(items);
    return d;
}

FieldDesc e(const char *key, const QStringList &values, const char *doc)
{
    FieldDesc d = f(key, "string", doc);
    d.enumValues = values;
    return d;
}

FieldDesc req(FieldDesc d)
{
    d.required = true;
    return d;
}

QStringList clipTypes()
{
    QStringList out;
    for (int i = static_cast<int>(ClipType::Video); i <= static_cast<int>(ClipType::Generator); ++i)
        out.append(clipTypeToString(static_cast<ClipType>(i)));
    return out;
}

QStringList trackTypes()
{
    QStringList out;
    for (int i = static_cast<int>(TrackType::Video); i <= static_cast<int>(TrackType::Adjustment); ++i)
        out.append(trackTypeToString(static_cast<TrackType>(i)));
    return out;
}

QStringList adjustmentKinds()
{
    QStringList out;
    for (int i = static_cast<int>(AdjustmentKind::VideoEffects); i <= static_cast<int>(AdjustmentKind::Camera); ++i)
        out.append(adjustmentKindToString(static_cast<AdjustmentKind>(i)));
    return out;
}

QStringList adjustmentScopes()
{
    QStringList out;
    for (int i = static_cast<int>(AdjustmentScope::AllBelow); i <= static_cast<int>(AdjustmentScope::Camera); ++i)
        out.append(adjustmentScopeToString(static_cast<AdjustmentScope>(i)));
    return out;
}

QStringList mediaKinds()
{
    QStringList out;
    for (int i = static_cast<int>(MediaKind::Video); i <= static_cast<int>(MediaKind::Other); ++i)
        out.append(mediaKindToString(static_cast<MediaKind>(i)));
    return out;
}

QList<FieldDesc> clipFields()
{
    return {
        req(f("id", "string", "Unique clip id. Ids are referenced by links, sidecars and scripts; never reuse one.")),
        e("type", clipTypes(), "What the clip is. Decides which other keys matter."),
        f("assetId", "string", "Id of the asset in assets/ this clip plays. Empty for text, shapes and adjustments."),
        f("name", "string", "Display name."),
        f("label", "string", "Free label shown on the clip (set_clip_label)."),
        f("timelineStartUs", "number", "Where the clip starts on the timeline, in microseconds (1 s = 1000000)."),
        f("timelineDurationUs", "number", "How long the clip lasts on the timeline, in microseconds."),
        f("srcInUs", "number", "Start inside the source media, in microseconds."),
        f("srcOutUs", "number", "End inside the source media, in microseconds."),
        f("path", "string", "Media file. Relative to the project folder when inside it."),
        f("thumbnailPath", "string", "Cached thumbnail; written by the editor."),
        f("filmstripPath", "string", "Cached filmstrip; written by the editor."),
        f("linkId", "string", "Shared by the video and audio halves of one linked clip."),
        f("linkedClipId", "string", "Id of the clip this adjustment is pinned to."),
        f("sourceVideoClipId", "string", "Video clip an audio clip was split from."),
        f("linkLoose", "boolean", "Linked halves move independently."),
        f("suppressEmbeddedAudio", "boolean", "Mute the audio inside a video clip."),
        f("audioStreamIndex", "integer", "Which audio stream of the source to play."),
        e("adjustmentKind", adjustmentKinds(), "For adjustment clips: what the clip applies."),
        f("textContent", "string", "Text of a text clip."),
        f("textStyle", "object", "Font, size, colour, layout and outline of a text clip."),
        f("textScript", "string", "Lua that builds per-frame text; see docs/text.md."),
        f("subtitleCues", "array", "Cues of a subtitle clip: {startUs, endUs, text}.", "object"),
        f("shapeStyle", "object", "Fill, stroke and geometry of a shape clip."),
        f("emoji", "string", "Emoji glyph sequence of an emoji clip."),
        f("blendMode", "string", "How the clip blends over what is below (normal, multiply, screen, ...)."),
        f("speed", "number", "Playback speed; 1 is normal."),
        f("speedCurve", "any", "Speed ramp keyframes."),
        f("reverse", "boolean", "Play backwards."),
        f("loop", "boolean", "Repeat the source to fill the clip."),
        f("loopOffsetUs", "number", "Loop phase, in microseconds."),
        f("flipH", "boolean", "Mirror horizontally."),
        f("flipV", "boolean", "Mirror vertically."),
        f("mask", "object", "Mask applied to the clip."),
        f("faceTrackPath", "string", "Face-tracking data file."),
        f("faceTrackSrcOffsetUs", "number", "Source offset of the face track."),
        f("depthPath", "string", "Depth map video."),
        e("stabilizeMode", {QStringLiteral("bake"), QStringLiteral("keyframes")}, "Stabilize mode."),
        f("stabilizeSmoothing", "number", "Stabilize smoothing."),
        f("stabilizeTripod", "boolean", "Stabilize as a locked-off tripod shot."),
        f("stabilizeAppliedSmoothing", "number", "Smoothing the baked stabilization used."),
        f("stabilizeAppliedTripod", "boolean", "Tripod flag the baked stabilization used."),
        f("stabilizeAppliedMode", "string", "Mode the baked stabilization used."),
        f("stabilizeHasRestPose", "boolean", "A rest pose was captured."),
        f("stabilizeRestX", "number", "Rest pose x."),
        f("stabilizeRestY", "number", "Rest pose y."),
        f("stabilizeRestW", "number", "Rest pose width."),
        f("stabilizeRestH", "number", "Rest pose height."),
        f("stabilizeRestRot", "number", "Rest pose rotation."),
        f("fadeInUs", "number", "Fade-in length, in microseconds."),
        f("fadeOutUs", "number", "Fade-out length, in microseconds."),
        f("fadeInCurve", "string", "Fade-in curve."),
        f("fadeOutCurve", "string", "Fade-out curve."),
        f("fadeInShape", "any", "Fade-in custom shape."),
        f("fadeOutShape", "any", "Fade-out custom shape."),
        f("audioFadeInUs", "number", "Audio fade-in length, in microseconds."),
        f("audioFadeOutUs", "number", "Audio fade-out length, in microseconds."),
        f("animIn", "object", "In animation: {kindId, durationUs, ...}."),
        f("animOut", "object", "Out animation."),
        f("volume", "keyframes", "Volume keyframes; the value is a gain (1 = unchanged)."),
        f("pan", "number", "Stereo pan, -1 to 1."),
        f("opacity", "keyframes", "Opacity keyframes, 0 to 1."),
        f("positionX", "keyframes", "Centre x in project pixels (keyframe times are clip-local, microseconds)."),
        f("positionY", "keyframes", "Centre y in project pixels."),
        f("scaleX", "keyframes", "Width in project pixels."),
        f("scaleY", "keyframes", "Height in project pixels."),
        f("rotation", "keyframes", "Rotation in degrees."),
        f("rotationX", "keyframes", "3D rotation around x, degrees."),
        f("rotationY", "keyframes", "3D rotation around y, degrees."),
        f("z", "keyframes", "3D depth."),
        f("perspective", "keyframes", "3D perspective."),
        f("anchorX", "keyframes", "Anchor point x."),
        f("anchorY", "keyframes", "Anchor point y."),
        f("anchorZ", "keyframes", "Anchor point z."),
        f("rotationCorrection", "number", "Rotation correction in degrees."),
        f("frameW", "number", "Frame width in pixels when not the media size."),
        f("frameH", "number", "Frame height in pixels when not the media size."),
        f("effects", "effects", "Video effects, applied in order."),
        f("audioEffects", "effects", "Audio effects, applied in order."),
        f("hiddenInLane", "boolean", "Hide this adjustment's chip in its lane."),
        f("enabled", "boolean", "false disables the clip without removing it."),
        f("layer3d", "boolean", "Place the clip in 3D space."),
        f("depthOcclude", "boolean", "Occlude by depth in 3D."),
        f("aimAtTarget", "boolean", "Aim the 3D layer at the camera target."),
        f("vector", "object", "Inline vector document of a vector clip."),
        f("model3d", "object", "3D model settings of a model3d clip."),
        f("pdf", "object", "PDF settings of a pdf clip."),
        f("procedural", "object", "Package id and parameters of a procedural or generator clip."),
        f("sequenceId", "string", "Id of the sequence a composite clip plays."),
        f("sourceFrame", "any", "Crop of the source as [x, y, w, h] fractions."),
        f("sourceFrameAngle", "number", "Rotation of the source crop."),
        f("motionTrackers", "array", "Motion trackers on this clip.", "object"),
        f("generatedBy", "string", "Script that produced this clip (scripts/x.lua). Such clips are locked in the editor and replaced when the script runs again; remove the key to take the clip over."),
        f("sidecars", "array", "Groups of this clip's keys stored in sidecars/<id>/. Written by the editor.", "string"),
    };
}

QList<KindDesc> build()
{
    QList<KindDesc> kinds;

    kinds.append({QStringLiteral("project"), QStringLiteral("Drift project"),
                  QStringLiteral("project.json: canvas, frame rate and project info."),
                  {
                      req(f("version", "integer", "Project format version; the editor refuses newer ones.")),
                      f("projectName", "string", "Project title."),
                      f("id", "string", "Stable project id; names the project's cache folders."),
                      f("author", "string", "Author."),
                      f("description", "string", "Description."),
                      f("createdAt", "string", "ISO 8601 creation time."),
                      f("modifiedAt", "string", "ISO 8601 time of the last save."),
                      f("fps", "integer", "Frames per second of the timeline."),
                      f("width", "integer", "Canvas width in pixels."),
                      f("height", "integer", "Canvas height in pixels."),
                      f("sampleRate", "integer", "Audio sample rate in Hz."),
                      f("background", "object", "Canvas fill: {kind: color|blur|transparent, color, blurStrength}."),
                      f("teleprompterScript", "string", "Teleprompter text."),
                      f("openSequenceTabs", "array", "Composite tabs open in the editor.", "string"),
                      f("workAreaInUs", "number", "Work area start, in microseconds."),
                      f("workAreaOutUs", "number", "Work area end, in microseconds."),
                  },
                  false});

    kinds.append({QStringLiteral("bin"), QStringLiteral("Media bin"),
                  QStringLiteral("bin.json: bin folders and the order of the asset list."),
                  {
                      f("assetOrder", "array", "Asset ids in bin order. Assets not listed follow in file-name order.", "string"),
                      f("folders", "array", "Bin folders: {id, name, parentId}.", "object"),
                  }});

    kinds.append({QStringLiteral("markers"), QStringLiteral("Markers"),
                  QStringLiteral("markers.json: timeline flags."),
                  {f("markers", "array", "Flags: {timeUs, label, color?}.", "object")}});

    kinds.append({QStringLiteral("guides"), QStringLiteral("Guides"),
                  QStringLiteral("guides.json: guide sets shown over the preview."),
                  {
                      f("enabled", "boolean", "Guides are visible."),
                      f("active", "array", "Ids of the active guide sets.", "string"),
                      f("sets", "array", "Custom guide sets.", "object"),
                  }});

    kinds.append({QStringLiteral("asset"), QStringLiteral("Media asset"),
                  QStringLiteral("assets/<id>-<slug>.json: one media file in the bin."),
                  {
                      req(f("id", "string", "Unique asset id; clips refer to it with assetId.")),
                      e("kind", mediaKinds(), "Media kind."),
                      f("name", "string", "Display name."),
                      f("path", "string", "Media file. Relative to the project folder when inside it."),
                      f("durationUs", "number", "Duration in microseconds."),
                      f("duration", "string", "Duration label."),
                      f("width", "integer", "Width in pixels."),
                      f("height", "integer", "Height in pixels."),
                      f("fps", "number", "Frame rate."),
                      f("rotationDegrees", "number", "Rotation stored in the file."),
                      f("rotationOverride", "number", "Rotation the user set."),
                      f("trimInUs", "number", "Source trim start, in microseconds."),
                      f("trimOutUs", "number", "Source trim end, in microseconds."),
                      f("sampleRate", "integer", "Audio sample rate."),
                      f("channels", "integer", "Audio channels."),
                      f("codecName", "string", "Video codec."),
                      f("thumbnailPath", "string", "Cached thumbnail."),
                      f("filmstripPath", "string", "Cached filmstrip."),
                      f("hasAudio", "boolean", "The file has an audio stream."),
                      f("variableFrameRate", "boolean", "The file is variable frame rate."),
                      f("editFriendly", "boolean", "Transcoded to an edit-friendly codec."),
                      f("sourceUri", "string", "Android document URI the media came from."),
                      f("folderId", "string", "Bin folder id."),
                      f("sequenceId", "string", "Sequence of a composite asset."),
                      f("generator", "string", "Generator id of a generated asset."),
                      f("sourceSize", "number", "Size in bytes when probed."),
                      f("sourceModifiedMs", "number", "Modified time when probed."),
                      f("sourceFrame", "any", "Crop of the source as [x, y, w, h] fractions."),
                      f("rating", "integer", "Star rating."),
                      f("maskRole", "string", "Role of a mask asset."),
                      f("ranges", "array", "Marked source ranges.", "object"),
                      f("markers", "array", "Source markers.", "object"),
                      f("generatedBy", "string", "Script that produced this asset. See clips."),
                  }});

    kinds.append({QStringLiteral("transcript"), QStringLiteral("Transcript"),
                  QStringLiteral("transcripts/<assetId>.json: speech-to-text of one asset."),
                  {}, false});

    kinds.append({QStringLiteral("template"), QStringLiteral("Project template"),
                  QStringLiteral("template.json: makes the folder a template. Scripts read the params with param \"name\"; each row of the data file overrides the params of the same name for one render."),
                  {
                      f("name", "string", "Template name."),
                      f("description", "string", "What the template makes."),
                      f("params", "array", "Parameters: {name, type: text|media|color|number|choice, default, label?, options? (choice)}.", "object"),
                      f("data", "string", "Data file of rows (CSV or JSON array), relative to the folder, e.g. data/rows.csv."),
                      f("output", "string", "Output file pattern. {row} is the 1-based row number, {name} any parameter."),
                  }});

    kinds.append({QStringLiteral("clip"), QStringLiteral("Clip"),
                  QStringLiteral("One entry of a track's clips array."), clipFields()});

    kinds.append({QStringLiteral("track"), QStringLiteral("Track"),
                  QStringLiteral("timeline/tracks/NN-<type>-<name>.json: track settings and its clips, sorted by start. Files are read in NN order; the first is the bottom track."),
                  {
                      req(f("id", "string", "Unique track id.")),
                      e("type", trackTypes(), "Track type."),
                      f("name", "string", "Display name."),
                      f("number", "integer", "Number in the default track label."),
                      e("adjustmentScope", adjustmentScopes(), "What an adjustment track applies to."),
                      f("parentTrackId", "string", "Parent of a nested adjustment lane."),
                      f("spanEndTrackId", "string", "Last track a transform layer covers."),
                      f("muted", "boolean", "Track is muted."),
                      f("hidden", "boolean", "Track is hidden."),
                      f("locked", "boolean", "Track is locked against edits."),
                      f("solo", "boolean", "Track is soloed."),
                      f("volume", "number", "Track volume."),
                      f("pan", "number", "Track pan."),
                      f("clipDisplay", "integer", "0 video, 1 both, 2 waveform."),
                      f("showChannelWaveforms", "boolean", "One waveform per channel."),
                      f("heightScale", "number", "Row height multiplier."),
                      f("lanesCollapsed", "boolean", "Nested lanes folded."),
                      f("excludeFromExport", "boolean", "Hidden and muted in exports."),
                      f("generatedBy", "string", "Script that created this track (scripts/x.lua). Its clips are replaced when the script runs again; remove the key to keep the track as it is."),
                      f("clips", "array", "Clips on this track, sorted by timelineStartUs.", "clip"),
                      f("transitions", "array", "Transitions between clips.", "object"),
                  }});

    kinds.append({QStringLiteral("sequence"), QStringLiteral("Sequence"),
                  QStringLiteral("sequences/<slug>/sequence.json: a nested timeline played by composite clips; its tracks are in the tracks/ folder beside it."),
                  {req(f("id", "string", "Sequence id; composite clips and assets refer to it with sequenceId."))}});

    for (const SidecarGroup &group : sidecarGroups()) {
        KindDesc kind;
        kind.kind = QStringLiteral("sidecar.") + group.name;
        kind.title = QStringLiteral("Clip %1").arg(group.name);
        kind.doc = QStringLiteral("sidecars/<clipId>/%1.json: keys moved out of the clip because they are large. Same meaning as in the clip.")
                       .arg(group.name);
        for (const FieldDesc &field : clipFields()) {
            if (group.keys.contains(field.key))
                kind.fields.append(field);
        }
        kinds.append(kind);
    }
    return kinds;
}

QJsonObject typeSchema(const FieldDesc &field)
{
    const auto itemSchema = [](const QString &items) -> QJsonObject {
        if (items == QLatin1String("clip"))
            return {{QStringLiteral("$ref"), QStringLiteral("#/$defs/clip")}};
        if (items.isEmpty() || items == QLatin1String("any"))
            return {};
        return {{QStringLiteral("type"), items}};
    };
    QJsonObject out;
    if (field.type == QLatin1String("keyframes")) {
        out.insert(QStringLiteral("$ref"), QStringLiteral("#/$defs/keyframes"));
    } else if (field.type == QLatin1String("effects")) {
        out = {{QStringLiteral("type"), QStringLiteral("array")},
               {QStringLiteral("items"), QJsonObject{{QStringLiteral("$ref"), QStringLiteral("#/$defs/effect")}}}};
    } else if (field.type == QLatin1String("any")) {
        // no constraint
    } else {
        out.insert(QStringLiteral("type"), field.type);
        if (field.type == QLatin1String("array") && !field.items.isEmpty())
            out.insert(QStringLiteral("items"), itemSchema(field.items));
    }
    if (!field.enumValues.isEmpty())
        out.insert(QStringLiteral("enum"), QJsonArray::fromStringList(field.enumValues));
    if (!field.doc.isEmpty())
        out.insert(QStringLiteral("description"), field.doc);
    return out;
}

QJsonObject objectSchema(const KindDesc &kind)
{
    QJsonObject properties{
        {QStringLiteral("$schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
    };
    QJsonArray required;
    for (const FieldDesc &field : kind.fields) {
        properties.insert(field.key, typeSchema(field));
        if (field.required)
            required.append(field.key);
    }
    QJsonObject out{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
    };
    if (!required.isEmpty())
        out.insert(QStringLiteral("required"), required);
    if (kind.closed && !kind.fields.isEmpty())
        out.insert(QStringLiteral("additionalProperties"), false);
    return out;
}

QJsonObject sharedDefs()
{
    const QJsonObject number{{QStringLiteral("type"), QStringLiteral("number")}};
    QJsonObject keyframe{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("timeUs"), QStringLiteral("value")}},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("timeUs"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                          {QStringLiteral("description"),
                           QStringLiteral("Time in microseconds from the clip's start.")}}},
             {QStringLiteral("value"), number},
             {QStringLiteral("hold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                  {QStringLiteral("description"), QStringLiteral("Stay at this value until the next key.")}}},
             {QStringLiteral("corner"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
             {QStringLiteral("inDx"), number}, {QStringLiteral("inDy"), number},
             {QStringLiteral("outDx"), number}, {QStringLiteral("outDy"), number},
             {QStringLiteral("fn"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("fnAmp"), number}, {QStringLiteral("fnPeriod"), number},
         }},
    };
    QJsonObject keyframes{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("keyframes"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                          {QStringLiteral("items"), keyframe}}},
             {QStringLiteral("enabled"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
         }},
    };
    QJsonObject effect{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{
             {QStringLiteral("catalogId"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                          {QStringLiteral("description"),
                           QStringLiteral("Effect id from the catalog (list_effects), e.g. adjust.contrast.")}}},
             {QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
             {QStringLiteral("enabled"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
             {QStringLiteral("parameters"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                          {QStringLiteral("description"), QStringLiteral("Parameter values by key.")}}},
             {QStringLiteral("paramKeyframes"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                          {QStringLiteral("additionalProperties"), QJsonObject{{QStringLiteral("$ref"), QStringLiteral("#/$defs/keyframes")}}}}},
         }},
    };
    return {{QStringLiteral("keyframes"), keyframes}, {QStringLiteral("effect"), effect}};
}

} // namespace

const FieldDesc *KindDesc::field(const QString &key) const
{
    for (const FieldDesc &candidate : fields) {
        if (candidate.key == key)
            return &candidate;
    }
    return nullptr;
}

const QList<KindDesc> &kindDescriptions()
{
    static const QList<KindDesc> kinds = build();
    return kinds;
}

const KindDesc *kindDescription(const QString &kind)
{
    for (const KindDesc &candidate : kindDescriptions()) {
        if (candidate.kind == kind)
            return &candidate;
    }
    return nullptr;
}

QString kindForPath(const QString &relPath)
{
    if (relPath == QLatin1String("project.json"))
        return QStringLiteral("project");
    if (relPath == QLatin1String("bin.json"))
        return QStringLiteral("bin");
    if (relPath == QLatin1String("markers.json"))
        return QStringLiteral("markers");
    if (relPath == QLatin1String("guides.json"))
        return QStringLiteral("guides");
    if (relPath == QLatin1String("template.json"))
        return QStringLiteral("template");
    if (relPath.startsWith(QLatin1String("assets/")))
        return QStringLiteral("asset");
    if (relPath.startsWith(QLatin1String("transcripts/")))
        return QStringLiteral("transcript");
    if (relPath.startsWith(QLatin1String("timeline/tracks/")))
        return QStringLiteral("track");
    if (relPath.startsWith(QLatin1String("sequences/"))) {
        if (relPath.endsWith(QLatin1String("/sequence.json")))
            return QStringLiteral("sequence");
        if (relPath.contains(QLatin1String("/tracks/")))
            return QStringLiteral("track");
    }
    if (relPath.startsWith(QLatin1String("sidecars/"))) {
        const QString group = relPath.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
        for (const SidecarGroup &candidate : sidecarGroups()) {
            if (candidate.name == group)
                return QStringLiteral("sidecar.") + group;
        }
    }
    return {};
}

QString schemaPath(const QString &kind)
{
    return QStringLiteral(".drift/schema/%1.schema.json").arg(kind);
}

QString schemaRefFor(const QString &relPath)
{
    const QString kind = kindForPath(relPath);
    if (kind.isEmpty())
        return {};
    const int depth = relPath.count(QLatin1Char('/'));
    QString prefix;
    for (int i = 0; i < depth; ++i)
        prefix += QStringLiteral("../");
    return prefix + schemaPath(kind);
}

QJsonObject schemaFor(const QString &kind)
{
    const KindDesc *desc = kindDescription(kind);
    if (!desc)
        return {};
    QJsonObject schema = objectSchema(*desc);
    schema.insert(QStringLiteral("$schema"), QStringLiteral("https://json-schema.org/draft/2020-12/schema"));
    schema.insert(QStringLiteral("$id"), QStringLiteral("drift:%1").arg(kind));
    schema.insert(QStringLiteral("title"), desc->title);
    if (!desc->doc.isEmpty())
        schema.insert(QStringLiteral("description"), desc->doc);
    if (kind == QLatin1String("transcript")) {
        schema.remove(QStringLiteral("properties"));
    }
    QJsonObject defs = sharedDefs();
    if (kind == QLatin1String("track")) {
        QJsonObject clip = objectSchema(*kindDescription(QStringLiteral("clip")));
        defs.insert(QStringLiteral("clip"), clip);
    }
    schema.insert(QStringLiteral("$defs"), defs);
    return schema;
}

FileMap schemaFiles()
{
    FileMap files;
    for (const KindDesc &kind : kindDescriptions())
        files.insert(schemaPath(kind.kind), canonicalJson(schemaFor(kind.kind)));
    return files;
}

} // namespace drift::projdir
