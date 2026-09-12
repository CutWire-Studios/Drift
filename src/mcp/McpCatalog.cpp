#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

namespace drift::mcp {
namespace {

const QStringList kTrackTypes = {QStringLiteral("video"), QStringLiteral("audio"),
                                 QStringLiteral("text"), QStringLiteral("subtitle"),
                                 QStringLiteral("shape")};

QJsonObject clipRefProps()
{
    return {
        {QStringLiteral("clip"),
         stringProp(QStringLiteral(
             "Clip UUID from inspect({clips:true}) — preferred, stable across edits. Required unless "
             "both track and index are given; with neither the op fails not_found (it does not fall "
             "back to the selection)."))},
        {QStringLiteral("track"),
         integerProp(QStringLiteral("Track index if clip is omitted (0 = top). Positional — shifts "
                                    "when tracks are added or reordered."))},
        {QStringLiteral("index"),
         integerProp(QStringLiteral("Clip index on that track if clip is omitted"))},
    };
}

QJsonObject assetRefProp()
{
    return stringProp(QStringLiteral(
        "Asset id, bin index as a string, or exact asset name (case-insensitive). A numeric string is "
        "tried as a bin index first, so prefer the id from list_assets."));
}

// Effect/audio-effect stack position. Repeated across ~10 ops, so the discovery path lives here.
QJsonObject effectIndexProp()
{
    return integerProp(QStringLiteral(
        "0-based position in this clip's effect stack — NOT a catalog id. Read the stack from "
        "inspect({clips:true,detail:true}), or use the index returned by add_effect/add_audio_effect."));
}

QJsonObject bookmarkIndexProp()
{
    return integerProp(QStringLiteral(
        "0-based position in inspect({detail:true}).bookmarks — bookmarks have no stable id, so "
        "re-read that list before using this and after any bookmark is removed."));
}

QJsonObject transitionIdProp()
{
    return stringProp(QStringLiteral(
        "Transition id returned by add_transition, or from inspect({clips:true,detail:true}) under "
        "tracks[].transitions. Unique within a track only."));
}

QJsonObject animPropProp()
{
    return stringProp(QStringLiteral(
        "Animated property: x, y, width, height, rotation, opacity, volume, or fx.<effectIndex>.<paramKey> "
        "(e.g. fx.0.amount). Note width/height here vs w/h in set_transform. Spellings live in this "
        "schema — list_animated_properties returns only properties that already have keys (empty on a "
        "fresh clip), so do not use it to learn names."));
}

// Which grid detect_beats' result is read through. Repeated across four ops.
QJsonObject beatUnitProp()
{
    return propWithDefault(
        enumProp(QStringLiteral(
                     "Grid to use from the last detect_beats. beat = every beat; bar = every "
                     "beatsPerBar-th beat counted from firstDownbeat; onset = detected transients, "
                     "which need no tempo and work when bpm is 0."),
                 {QStringLiteral("beat"), QStringLiteral("bar"), QStringLiteral("onset")}),
        QStringLiteral("beat"));
}

QJsonObject maskSchema()
{
    return objectSchema({
        {QStringLiteral("shape"),
         enumProp(QStringLiteral("Mask shape. Omitted or unrecognised = none, i.e. mask off."),
                  {QStringLiteral("none"), QStringLiteral("rectangle"), QStringLiteral("ellipse"),
                   QStringLiteral("star"), QStringLiteral("heart"), QStringLiteral("bars"),
                   QStringLiteral("freeform"), QStringLiteral("matte")})},
        {QStringLiteral("x"), numberProp(QStringLiteral("Center x, 0..1 of canvas (default 0.5)"))},
        {QStringLiteral("y"), numberProp(QStringLiteral("Center y, 0..1 (default 0.5)"))},
        {QStringLiteral("w"), numberProp(QStringLiteral("Width, 0..1 (default 0.6)"))},
        {QStringLiteral("h"), numberProp(QStringLiteral("Height, 0..1 (default 0.6)"))},
        {QStringLiteral("rotation"), numberProp(QStringLiteral("Degrees clockwise (default 0)"))},
        {QStringLiteral("feather"), numberProp(QStringLiteral("Alpha edge blur in pixels (default 0)"))},
        {QStringLiteral("invert"), boolProp(QStringLiteral("Invert coverage (default false)"))},
        {QStringLiteral("points"),
         arrayProp({{QStringLiteral("type"), QStringLiteral("array")}},
                   QStringLiteral("freeform only: [[x,y], …] normalised 0..1"))},
    });
}

QJsonObject shapeStyleSchema()
{
    return objectSchema({
        {QStringLiteral("kind"), stringProp(QStringLiteral("Shape catalog id from list_shapes"))},
        {QStringLiteral("fillKind"),
         enumProp(QStringLiteral("Fill type"),
                  {QStringLiteral("none"), QStringLiteral("solid"), QStringLiteral("linearGradient"),
                   QStringLiteral("radialGradient")})},
        {QStringLiteral("fill"), stringProp(QStringLiteral("Fill color #RRGGBB or #AARRGGBB"))},
        {QStringLiteral("fillSecondary"), stringProp(QStringLiteral("Gradient end stop color"))},
        {QStringLiteral("gradientAngle"), numberProp(QStringLiteral("Degrees; 0 = left to right"))},
        {QStringLiteral("stroke"), stringProp(QStringLiteral("Stroke color"))},
        {QStringLiteral("strokeWidth"), numberProp(QStringLiteral("Stroke width px, clamped 0..200"))},
        {QStringLiteral("strokeStyle"),
         enumProp(QStringLiteral("Stroke dash pattern"),
                  {QStringLiteral("none"), QStringLiteral("solid"), QStringLiteral("dash"),
                   QStringLiteral("dot"), QStringLiteral("dashDot")})},
        {QStringLiteral("cornerRadius"), numberProp(QStringLiteral("Rect family: px, clamped 0..2000"))},
        {QStringLiteral("points"), integerProp(QStringLiteral("Star/polygon point count, clamped 3..60"))},
        {QStringLiteral("innerRatio"), numberProp(QStringLiteral("Star inner radius, clamped 0.05..0.95"))},
        {QStringLiteral("headSize"), numberProp(QStringLiteral("Arrow head size, clamped 0.05..0.9"))},
        {QStringLiteral("thickness"), numberProp(QStringLiteral("Arrow/banner thickness, clamped 0.05..1"))},
        {QStringLiteral("tailX"), numberProp(QStringLiteral("Bubble tail x, clamped 0.08..0.92"))},
        {QStringLiteral("tailSize"), numberProp(QStringLiteral("Bubble tail size, clamped 0.05..0.5"))},
    });
}

QJsonObject mergeProps(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

QJsonObject textStyleSchema()
{
    return objectSchema({
        {QStringLiteral("fontFamily"), stringProp(QStringLiteral("Font family name"))},
        {QStringLiteral("fontWeight"), integerProp(QStringLiteral("Font weight (e.g. 400, 700)"))},
        {QStringLiteral("pixelSize"), numberProp(QStringLiteral("Font size in pixels"))},
        {QStringLiteral("color"), stringProp(QStringLiteral("Text color (#RRGGBB or #AARRGGBB)"))},
        {QStringLiteral("italic"), boolProp(QStringLiteral("Italic"))},
        {QStringLiteral("align"), enumProp(QStringLiteral("Horizontal alignment"),
                                          {QStringLiteral("left"), QStringLiteral("center"),
                                           QStringLiteral("right")})},
        {QStringLiteral("valign"), enumProp(QStringLiteral("Vertical alignment"),
                                            {QStringLiteral("top"), QStringLiteral("center"),
                                             QStringLiteral("bottom")})},
        {QStringLiteral("lineHeight"), numberProp(QStringLiteral("Line height multiplier"))},
        {QStringLiteral("letterSpacing"), numberProp(QStringLiteral("Letter spacing"))},
        {QStringLiteral("wordWrap"), boolProp(QStringLiteral("Wrap long lines"))},
    });
}

QJsonObject speedPointSchema()
{
    return objectSchema({
        {QStringLiteral("pos"), numberProp(QStringLiteral("Normalised position 0..1 over trimmed source"))},
        {QStringLiteral("speed"), numberProp(QStringLiteral("Playback rate at this point"))},
        {QStringLiteral("inDx"), numberProp(QStringLiteral("Incoming tangent dx (curve space)"))},
        {QStringLiteral("inDy"), numberProp(QStringLiteral("Incoming tangent dy"))},
        {QStringLiteral("outDx"), numberProp(QStringLiteral("Outgoing tangent dx"))},
        {QStringLiteral("outDy"), numberProp(QStringLiteral("Outgoing tangent dy"))},
        {QStringLiteral("corner"), boolProp(QStringLiteral("Break tangent collinearity"))},
    });
}

QJsonObject exportSettingsProps()
{
    return {
        {QStringLiteral("scale"), stringProp(QStringLiteral("Scale id from list_export_options"))},
        {QStringLiteral("height"), integerProp(QStringLiteral("Target height in pixels; 0 = project height"))},
        {QStringLiteral("fps"), numberProp(QStringLiteral("Output fps; 0 = project rate. An fps id from list_export_options is also accepted as a string."))},
        {QStringLiteral("video"), stringProp(QStringLiteral("Video codec id from list_export_options.video (h264, hevc, …)"))},
        {QStringLiteral("audio"), stringProp(QStringLiteral("Audio codec id from list_export_options.audio (aac, opus, …)"))},
        {QStringLiteral("rate"), enumProp(QStringLiteral("Rate control mode — decides whether crf or bitrate is honoured. Defaults to crf. Not listed by list_export_options; crf is only applied by codecs that support it, otherwise the encoder falls back to bitrate."),
                                          {QStringLiteral("crf"), QStringLiteral("bitrate")})},
        {QStringLiteral("crf"), integerProp(QStringLiteral("Quality when rate is crf (lower is better)"))},
        {QStringLiteral("bitrate"), integerProp(QStringLiteral("Video kbps when rate is bitrate"))},
        {QStringLiteral("preset"), stringProp(QStringLiteral("Encoder speed/quality preset. Defaults to the codec's own default (medium for x264, p4 for NVENC)."))},
        {QStringLiteral("audio_bitrate"), integerProp(QStringLiteral("Audio kbps"))},
        {QStringLiteral("audio_only"), boolProp(QStringLiteral("Encode audio only. Forced off when gif is true."))},
        {QStringLiteral("gif"), boolProp(QStringLiteral("Encode animated GIF. Forces audio_only off and, when no fps is given, 15fps."))},
        {QStringLiteral("work_area"), boolProp(QStringLiteral("Limit to the In/Out work area. Fails bad_args when no work area is set. Ignored when in/out are given."))},
        {QStringLiteral("in"), numberProp(QStringLiteral("Range start seconds. Overrides work_area."))},
        {QStringLiteral("out"), numberProp(QStringLiteral("Range end seconds; must be greater than in."))},
    };
}

struct Op {
    const char *name;
    const char *toolbox;
    const char *when;
    const char *description;
    QJsonObject schema;
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
};

const QList<Op> &ops()
{
    static const QList<Op> k = {
        { "import_media", "media", "Bring files into the bin",
          "Import local media files and block until each probe finishes (15s cap). Paths must be "
          "absolute (or file://). There is no directory-listing op: if the user named a file loosely "
          "(\"GX010023.mp4 in Downloads\", \"the wedding clip\"), glob or search with YOUR own "
          "filesystem tools, then pass the hits here. Returns {assets:[{id, index, name, kind, dur, "
          "pending}]}, plus missing:[paths that do not exist] and refreshed:[paths already in the bin] "
          "— treat a non-empty missing as \"search again\", not as a bin problem. On timeout returns "
          "error import_timeout with the same assets array and pending:true on the stragglers. Not "
          "undoable.",
          objectSchema({{QStringLiteral("paths"),
                         arrayProp({{QStringLiteral("type"), QStringLiteral("string")}},
                                   QStringLiteral("Absolute file paths from your own filesystem search"))}},
                       {QStringLiteral("paths")}) },
        { "list_assets", "media", "See what is in the bin",
          "List imported assets. Returns {assets:[{index, id, name, kind, dur, w, h}]}. There is no "
          "recording timestamp — \"in the order I shot them\" cannot be recovered from this list; "
          "sort by name or by the order they were imported.",
          objectSchema({}), true, false, true },
        { "rename_asset", "media", "Rename a bin row",
          "Rename an asset in the bin. Does not rename the file on disk.",
          objectSchema({{QStringLiteral("asset"), assetRefProp()},
                        {QStringLiteral("name"), stringProp(QStringLiteral("New display name"))}},
                       {QStringLiteral("asset"), QStringLiteral("name")}) },

        { "add_track", "timeline", "Need a new lane",
          "Prepend a track. New track becomes index 0, so every existing track index shifts down by "
          "one — re-read inspect before reusing track/index clip references.",
          objectSchema({{QStringLiteral("type"), enumProp(QStringLiteral("Track type"), kTrackTypes)}},
                       {QStringLiteral("type")}) },
        { "remove_track", "timeline", "Delete a lane and its clips",
          "Delete a track and everything on it.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))}},
                       {QStringLiteral("track")}),
          false, true },
        { "set_track", "timeline", "Mute or hide a lane",
          "Set track muted/hidden.",
          objectSchema(mergeProps({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                                   {QStringLiteral("muted"), boolProp(QStringLiteral("Mute"))},
                                   {QStringLiteral("hidden"), boolProp(QStringLiteral("Hide from composite"))}},
                                  {}),
                       {QStringLiteral("track")}) },
        { "place_clip", "timeline", "Put media on the timeline",
          "Place an asset as a clip and return the new clip's id. When overlap is off (default) the "
          "start is pushed to the next free gap; the reply carries requested, placed, and "
          "reason:\"gap\" when they differ. Note `track` means two different things: with "
          "new_track:true it is the insert position of the new track, otherwise it is the destination "
          "track and a type mismatch fails type_mismatch. With `track` omitted, the first track "
          "accepting this asset is used, and a new track is created if none does.",
          objectSchema(mergeProps({{QStringLiteral("asset"), assetRefProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline start seconds (default: playhead)"))},
                                   {QStringLiteral("track"), integerProp(QStringLiteral("Destination track, or insert position when new_track is true"))},
                                   {QStringLiteral("new_track"), boolProp(QStringLiteral("Insert a new matching track above"))}},
                                  {})) },
        { "move_clip", "timeline", "Change a clip's start time",
          "Move a clip on its track. When overlap is off (default) the start may be pushed forward to "
          "the next gap; the reply carries requested, placed, and reason:\"gap\" when they differ.",
          objectSchema(mergeProps({{QStringLiteral("at"), numberProp(QStringLiteral("New start seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("at")}) },
        { "set_duration", "timeline", "Change timeline length",
          "Set the clip's timeline duration in seconds. Trims source out (or in if reversed). Clamped "
          "to the source material left after the current trim, so the applied dur in the reply may be "
          "shorter than requested; clips with no intrinsic length (text, shape, still image) accept "
          "any duration.",
          objectSchema(mergeProps({{QStringLiteral("duration"), numberProp(QStringLiteral("Seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("duration")}) },
        { "set_trim", "timeline", "Set source in/out",
          "Set source in/out points in seconds. Recomputes timeline duration from the span and speed. "
          "Values are not validated here — out <= in or a span past the end of the source is clamped "
          "downstream; read back in/out/dur from the reply.",
          objectSchema(mergeProps({{QStringLiteral("in"), numberProp(QStringLiteral("Source in seconds"))},
                                   {QStringLiteral("out"), numberProp(QStringLiteral("Source out seconds"))}},
                                  clipRefProps()),
                       {QStringLiteral("in"), QStringLiteral("out")}) },
        { "move_to_track", "timeline", "Move a clip to another lane",
          "Move a clip to another track. Type must match the destination, else type_mismatch.",
          objectSchema(mergeProps({{QStringLiteral("to_track"), integerProp(QStringLiteral("Destination track"))},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: current)"))}},
                                  clipRefProps()),
                       {QStringLiteral("to_track")}) },
        { "split_clip", "timeline", "Cut a clip in two",
          "Split a clip at `at` seconds (default: playhead). `at` must fall strictly inside the clip, "
          "else bad_args. Returns {clips:[original id, new id], at}; the original id keeps the left "
          "part.",
          objectSchema(mergeProps({{QStringLiteral("at"), numberProp(QStringLiteral("Timeline time seconds"))}},
                                  clipRefProps())) },
        { "delete_clip", "timeline", "Remove a clip",
          "Delete a clip and its linked A/V partner. Reversible with undo.",
          objectSchema(clipRefProps()), false, true },
        { "duplicate_clip", "timeline", "Copy a clip after itself",
          "Duplicate a clip immediately after it on the same track. Returns the new clip's id.",
          objectSchema(clipRefProps()) },
        { "undo", "timeline", "Revert the last edit",
          "Undo the last project edit; one apply batch is one step. Fails bad_args when the stack is "
          "empty. Does not cover the ops in catalog.limitations (import, save, playback, export, "
          "view-state toggles). set_beat_layers is the dangerous one: it changes the user's own "
          "snapping and cannot be undone. Use list_history and undo_to to jump further back.",
          objectSchema({}) },
        { "redo", "timeline", "Re-apply an undone edit",
          "Redo the last undone edit. Fails bad_args when there is nothing to redo.",
          objectSchema({}) },
        { "list_history", "timeline", "Read the undo stack",
          "Linear history (no branches). Returns {entries:[{index, label, hash, short, snapshot}], "
          "current, hash, linear:true}. index 0 is Origin (empty/loaded project); each later entry is "
          "one undo step. hash is SHA-256 of that version's compact project JSON — the same bytes "
          "take_snapshot writes to <hash>.json. current is the HEAD index. Read-only.",
          objectSchema({}), true, false, true },
        { "undo_to", "timeline", "Jump to a history version",
          "Restore the project to a linear history version. Pass index (from list_history) or hash "
          "(full SHA-256 or unique 8+ char prefix). Redo of later steps remains until the next edit, "
          "which discards them — there is no branching. Not itself undoable.",
          objectSchema({{QStringLiteral("index"),
                         integerProp(QStringLiteral("Target index from list_history (0 = Origin)"))},
                        {QStringLiteral("hash"),
                         stringProp(QStringLiteral("Content hash from list_history, or a unique prefix"))}}) },
        { "take_snapshot", "timeline", "Keep a JSON copy of HEAD",
          "Write the current history version as compact project JSON named <hash>.json under the app "
          "history folder. The file's SHA-256 is the same hash list_history reports. Does not change "
          "the project. Caps at 32 files (oldest dropped). Not undoable.",
          objectSchema({{QStringLiteral("label"),
                         stringProp(QStringLiteral("Optional note stored only in the reply"))}}) },
        { "list_snapshots", "timeline", "On-disk history JSON files",
          "Snapshots written by take_snapshot. Returns {snapshots:[{hash, short, path, bytes, savedAt}]}.",
          objectSchema({}), true, false, true },
        { "restore_snapshot", "timeline", "Reset to a saved JSON",
          "If hash is still on the undo stack, same as undo_to. Otherwise load the on-disk JSON and "
          "clear the stack (new Origin). Linear — redo history is discarded. Not undoable.",
          objectSchema({{QStringLiteral("hash"),
                         stringProp(QStringLiteral("Snapshot hash or unique prefix"))}},
                       {QStringLiteral("hash")}) },
        { "set_overlap", "timeline", "Allow clips to overlap",
          "Project setting. Off (default): place/move push to the next gap. On: requested start is kept.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Allow overlapping clips"))}},
                       {QStringLiteral("enabled")}) },

        { "set_transform", "canvas", "Position, size, rotate, fade a clip",
          "Set canvas transform in project-canvas pixels (0,0 = canvas top-left). Omitted fields are "
          "left unchanged. IMPORTANT: the write lands at the CURRENT PLAYHEAD — if the property is "
          "already keyframed, or autoKey is on, this creates/updates a keyframe there instead of "
          "setting a constant value, so seek first. Use set_property_keyframes_enabled(false) or "
          "remove the keys for an unconditional value. Fails bad_args on audio clips and when no "
          "transform field is supplied.",
          objectSchema(mergeProps({{QStringLiteral("x"), numberProp(QStringLiteral("Left edge, canvas pixels"))},
                                   {QStringLiteral("y"), numberProp(QStringLiteral("Top edge, canvas pixels"))},
                                   {QStringLiteral("w"), numberProp(QStringLiteral("On-canvas width in pixels (not source resolution)"))},
                                   {QStringLiteral("h"), numberProp(QStringLiteral("On-canvas height in pixels"))},
                                   {QStringLiteral("rotation"), numberProp(QStringLiteral("Degrees clockwise"))},
                                   {QStringLiteral("opacity"), numberProp(QStringLiteral("0..1"))}},
                                  clipRefProps())) },
        { "reset_transform", "canvas", "Reset a clip to fill the canvas",
          "Reset position, size, rotation, opacity, and flips to defaults.",
          objectSchema(clipRefProps()) },

        { "seek", "playback", "Jump the playhead",
          "Move the playhead to `at` seconds and return the clamped position. Worth doing before ops "
          "that read the playhead: set_transform, capture, split_clip, freeze_frame, "
          "upsert_subtitle_cue, paste_at_playhead, and any add_* with `at` omitted.",
          objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds; clamped to the project duration"))}},
                       {QStringLiteral("at")}) },
        { "play", "playback", "Start playback",
          "Start playback from the current playhead, looping over the work area when one is set. "
          "Playback moves the playhead, which changes where playhead-based ops land — pause before "
          "editing. Returns {playing}.",
          objectSchema({}) },
        { "pause", "playback", "Stop playback",
          "Pause playback, leaving the playhead where it stopped. Returns {playing}. capture pauses "
          "on its own.",
          objectSchema({}) },
        { "set_work_area", "playback", "Set the In/Out range",
          "Set the In/Out work area, used for loop playback and for export_video's work_area option. "
          "out must be greater than in, else bad_args. Read it back from inspect as work_in/work_out, "
          "which are absent when no work area is set.",
          objectSchema({{QStringLiteral("in"), numberProp(QStringLiteral("In point seconds"))},
                        {QStringLiteral("out"), numberProp(QStringLiteral("Out point seconds; must be greater than in"))}},
                       {QStringLiteral("in"), QStringLiteral("out")}) },
        { "clear_work_area", "playback", "Clear the In/Out range",
          "Remove the In/Out work area. Afterwards export_video with work_area:true fails bad_args, "
          "and inspect stops reporting work_in/work_out.",
          objectSchema({}) },

        { "add_text", "text", "Put a title or caption on the timeline",
          "Add a text clip on a text track, creating one if needed, and return the new clip's id. "
          "Empty text becomes \"Text\".",
          objectSchema({{QStringLiteral("text"), stringProp(QStringLiteral("Caption"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))},
                        {QStringLiteral("preset"), stringProp(QStringLiteral("Optional text style pack id from list_text_presets"))}}) },
        { "set_text", "text", "Change caption copy or style",
          "Set text content and/or a partial style patch. Only supplied style keys change; omitted "
          "ones keep their current value.",
          objectSchema(mergeProps({{QStringLiteral("text"), stringProp(QStringLiteral("New content"))},
                                   {QStringLiteral("style"), textStyleSchema()}},
                                  clipRefProps())) },

        { "list_effects", "effects", "See available video effects",
          "The installable catalog, not what is on a clip. Returns "
          "{effects:[{id, label, cat, params:[{key, type, default, min, max, bool}]}]}. Use id with "
          "add_effect and param keys with set_effect_param. To see a clip's current stack use "
          "inspect({clips:true,detail:true}).",
          objectSchema({}), true, false, true },
        { "list_audio_effects", "effects", "See available audio effects",
          "The installable catalog, not what is on a clip. Returns {effects:[{id, label, cat, params:[…]}]}. "
          "Use id with add_audio_effect.",
          objectSchema({}), true, false, true },
        { "list_transitions", "effects", "See available transitions",
          "Returns {transitions:[{id, label, cat, params:[…]}]}. Pass the id as `kind` to add_transition "
          "(default crossfade).",
          objectSchema({}), true, false, true },
        { "add_effect", "effects", "Put a video effect on a clip",
          "Append a video effect to the end of the clip's stack. Returns index — its stack position, "
          "which is what every other effect op takes. Fails not_found on an unknown effect id.",
          objectSchema(mergeProps({{QStringLiteral("effect"), stringProp(QStringLiteral("Effect catalog id from list_effects"))}},
                                  clipRefProps()),
                       {QStringLiteral("effect")}) },
        { "remove_effect", "effects", "Remove a video effect",
          "Remove a video effect by stack index. Later effects shift down one position.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()}},
                                  clipRefProps()),
                       {QStringLiteral("index")}),
          false, true },
        { "set_effect_param", "effects", "Tweak a video effect",
          "Set one numeric/boolean video effect parameter. WARNING: the write is not validated — an "
          "unknown key or out-of-range index still returns ok. Take keys from list_effects and confirm "
          "the result with inspect({clips:true,detail:true}). Use set_effect_color_param for colors.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from the effect's params in list_effects"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Value (booleans as 0/1)"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "add_audio_effect", "effects", "Put an audio effect on a clip",
          "Append an audio effect to the clip's audio stack. Returns index — its stack position. The "
          "audio stack is numbered separately from the video effect stack. Fails not_found on an "
          "unknown id.",
          objectSchema(mergeProps({{QStringLiteral("effect"), stringProp(QStringLiteral("Audio effect catalog id from list_audio_effects"))}},
                                  clipRefProps()),
                       {QStringLiteral("effect")}) },
        { "remove_audio_effect", "effects", "Remove an audio effect",
          "Remove an audio effect by stack index. Later effects shift down one position.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()}},
                                  clipRefProps()),
                       {QStringLiteral("index")}),
          false, true },
        { "set_audio_effect_param", "effects", "Tweak an audio effect",
          "Set one audio effect parameter (booleans as 0/1). WARNING: not validated — an unknown key "
          "or bad index still returns ok. Take keys from list_audio_effects and confirm with "
          "inspect({clips:true,detail:true}).",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from list_audio_effects"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Value"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "add_transition", "effects", "Bridge two adjacent clips",
          "Add or replace a transition between this clip and the next eligible clip on the same track "
          "(the neighbour with the earliest start after it); fails bad_args when there is none. Only "
          "video, shape, and text tracks take transitions. Duration is forced to the physical overlap "
          "when the clips already overlap, and is floored at 0.1s otherwise. Replacing an existing "
          "transition clears its parameter overrides. Returns {id, kind, dur, track} — keep id for "
          "remove_transition and set_transition_*.",
          objectSchema(mergeProps(
              {{QStringLiteral("kind"),
                propWithDefault(stringProp(QStringLiteral("Transition id from list_transitions; unknown ids silently fall back to crossfade")),
                                QStringLiteral("crossfade"))},
               {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds (ignored when clips already overlap)"))}},
              clipRefProps())) },
        { "remove_transition", "effects", "Remove a transition",
          "Remove a transition by id from a track. Transition ids are unique within a track only, so "
          "track is required. Ids come from add_transition or inspect({clips:true,detail:true}).",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("id"), transitionIdProp()}},
                       {QStringLiteral("track"), QStringLiteral("id")}),
          false, true },

        { "set_project_setup", "project", "Change canvas size or frame rate",
          "Set project width, height, and fps. Changing width/height rebases every clip's canvas "
          "transform onto the new size (same helper apply_canvas_crop uses) so existing clips stay "
          "visually in place rather than jumping; fps does not retime clips.",
          objectSchema({{QStringLiteral("width"), integerProp(QStringLiteral("Canvas width pixels"))},
                        {QStringLiteral("height"), integerProp(QStringLiteral("Canvas height pixels"))},
                        {QStringLiteral("fps"), integerProp(QStringLiteral("Frames per second"))}},
                       {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("fps")}) },
        { "set_background", "project", "Change canvas background",
          "Set background kind, color, and/or blur strength. At least one field is required. "
          "blurStrength only has a visible effect when kind is blur.",
          objectSchema({{QStringLiteral("kind"), enumProp(QStringLiteral("Background kind"),
                                                          {QStringLiteral("color"), QStringLiteral("blur")})},
                        {QStringLiteral("color"), stringProp(QStringLiteral("Background color #AARRGGBB"))},
                        {QStringLiteral("blurStrength"), numberProp(QStringLiteral("Blur amount 0..200"))}}) },
        { "set_metadata", "project", "Set project title and author",
          "Set project metadata. Omitted fields are left unchanged.",
          objectSchema({{QStringLiteral("title"), stringProp(QStringLiteral("Project title"))},
                        {QStringLiteral("author"), stringProp(QStringLiteral("Author name"))},
                        {QStringLiteral("description"), stringProp(QStringLiteral("Project description"))}}) },
        { "save_project", "project", "Save the project file",
          "Save the open project. With path omitted, saves to the current project path (inspect.path); "
          "fails bad_args when the project has never been saved. With path given, writes a .drift "
          "bundle there, creating parent folders as needed. Returns ok once the save is dispatched — "
          "it does NOT report a failed write. Confirm with inspect: dirty should be false and path "
          "should match.",
          objectSchema({{QStringLiteral("path"),
                         stringProp(QStringLiteral("Absolute .drift path; omit to save to the current path"))}}) },
        { "list_export_options", "project", "See codecs, scales, and fps choices",
          "Returns {scales:[{id,w,h}], fps:[{id}], video:[{id,label}], audio:[{id,label}], gif, folder} "
          "— only codecs available on this machine are listed. Does not list rate or preset values; "
          "those live in the export_video schema.",
          objectSchema({}), true, false, true },
        { "export_video", "project", "Render the timeline to a file",
          "Start an async encode and return immediately with {started, path, busy}. Poll "
          "inspect().export.{active,progress} or export_status until active is false. Fails export_busy "
          "when an encode is already running — cancel_export first. The output path is normalised: a "
          "directory gets <project name>.<ext> appended and a suffix-less path gets the container "
          "extension added, so use the path echoed in the reply, not the one you sent. Omitted settings "
          "inherit from the last export in this app profile, then defaults — pass every setting you "
          "care about rather than relying on them.",
          objectSchema(mergeProps({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute output path, or a directory to auto-name inside"))}},
                                  exportSettingsProps()),
                       {QStringLiteral("path")}) },
        { "export_status", "project", "Check an in-flight encode",
          "Returns {busy, progress 0..1, message}. Same data as inspect().export.",
          objectSchema({}), true, false, true },

        { "list_animated_properties", "keyframes", "See what already has keys",
          "Returns {props:[…]} — only the properties that already carry keyframes on this clip. Empty "
          "on a fresh clip. Property spellings live in the `prop` schema of the other keyframes ops "
          "(x, y, width, height, rotation, opacity, volume, fx.<i>.<key>), not here.",
          objectSchema(clipRefProps()), true, false, true },
        { "list_keyframes", "keyframes", "Read keys for one property",
          "Returns {prop, enabled, keys:[{seconds, value, inDx, inDy, outDx, outDy, corner, hold, "
          "easing, custom}]}. Times are timeline seconds. enabled is false when the property was muted "
          "via set_property_keyframes_enabled.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()}},
                                  clipRefProps()),
                       {QStringLiteral("prop")}),
          true, false, true },
        { "set_keyframe", "keyframes", "Add or update a key",
          "Add a keyframe, or overwrite the value of an existing one at that time. Creates the "
          "animation if the property had no keys yet.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Property value, in the property's own units (pixels, degrees, 0..1)"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("value")}) },
        { "remove_keyframe", "keyframes", "Delete a key",
          "Remove the keyframe NEAREST to `at` — there is no distance limit, so a time that misses "
          "every key still deletes the closest one, and a no-op returns ok. Confirm the exact key time "
          "with list_keyframes first.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds of the key to delete"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at")}),
          false, true },
        { "set_keyframe_interpolation", "keyframes", "Set linear/hold/ease on a key",
          "Set the easing preset on the key nearest `at`. SIDE EFFECT: this moves the playhead to `at`, "
          "which changes the default time of later ops in the same apply batch and the target of "
          "set_transform — seek back if that matters.",
          objectSchema(mergeProps(
              {{QStringLiteral("prop"), animPropProp()},
               {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds; the playhead is moved here"))},
               {QStringLiteral("mode"), enumProp(QStringLiteral("Interpolation"),
                                                {QStringLiteral("linear"), QStringLiteral("hold"),
                                                 QStringLiteral("ease")})}},
              clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("mode")}) },
        { "set_keyframe_tangents", "keyframes", "Shape bezier handles",
          "Set tangent handles on the key at `at`, relative to it. Omitted handle fields are sent as 0, "
          "not left alone — pass all four to avoid flattening the ones you skip.",
          objectSchema(mergeProps(
              {{QStringLiteral("prop"), animPropProp()},
               {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
               {QStringLiteral("inDx"), numberProp(QStringLiteral("Incoming handle dx in seconds (defaults to 0)"))},
               {QStringLiteral("inDy"), numberProp(QStringLiteral("Incoming handle dy in property units (defaults to 0)"))},
               {QStringLiteral("outDx"), numberProp(QStringLiteral("Outgoing handle dx in seconds (defaults to 0)"))},
               {QStringLiteral("outDy"), numberProp(QStringLiteral("Outgoing handle dy in property units (defaults to 0)"))},
               {QStringLiteral("corner"), boolProp(QStringLiteral("Break tangent collinearity"))}},
              clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at")}) },
        { "set_keyframe_hold", "keyframes", "Step-hold a key",
          "When hold is true the property steps: it keeps this key's value until the next key instead "
          "of interpolating.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds"))},
                                   {QStringLiteral("hold"), boolProp(QStringLiteral("Hold until next key"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("at"), QStringLiteral("hold")}) },
        { "set_property_keyframes_enabled", "keyframes", "Mute animation without deleting keys",
          "When false the keys are kept but the property holds its first key's value. Use this before "
          "set_transform when you want a constant value on an already-animated property.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("enabled"), boolProp(QStringLiteral("Keyframes drive the property"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("enabled")}) },

        { "list_speed_curve", "speed", "Read a clip's speed ramp",
          "Returns {hasCurve, points:[{pos,speed,…}], retimedDuration}. Despite being a read, this "
          "opens and closes a transient curve session and will CLOSE any speed-curve session already "
          "open. Fails bad_args on clips that cannot carry a curve (no continuous source: text, shape, "
          "still image).",
          objectSchema(clipRefProps()), false, false, true },
        { "set_speed_curve", "speed", "Apply a custom speed ramp",
          "Replace the clip with a retimed copy carrying the curve. Needs at least two points. Returns "
          "{id, track, index, retimedDuration} with a NEW id — the old clip UUID is dead. An ops array "
          "cannot reference an id produced earlier in the same batch, so END THE BATCH after this op "
          "and use the returned id in the next apply. Fails bad_args on clips that cannot carry a "
          "curve.",
          objectSchema(mergeProps(
              {{QStringLiteral("points"),
                arrayProp(speedPointSchema(), QStringLiteral("Speed curve control points, at least two, ordered by pos"))}},
              clipRefProps()),
                       {QStringLiteral("points")}) },
        { "clear_speed_curve", "speed", "Remove a speed ramp",
          "Clear the curve and restore the scalar-speed timeline duration.",
          objectSchema(clipRefProps()), false, true },

        { "get_ui_preferences", "ui", "Read editor UI settings",
          "Returns {theme:{overridden, dark}, autoKey, mediaGrid, reopenLastProject}. autoKey matters "
          "for set_transform: when true, transform writes become keyframes at the playhead.",
          objectSchema({}), true, false, true },
        { "set_theme", "ui", "Set dark or light theme",
          "Set an explicit dark-mode preference. This overrides the OS theme; clear it with "
          "set_ui_preferences({followSystem:true}). Not undoable.",
          objectSchema({{QStringLiteral("dark"), boolProp(QStringLiteral("true = dark, false = light"))}},
                       {QStringLiteral("dark")}) },
        { "list_shortcuts", "ui", "List action bindings",
          "Returns {actions:[{id, label, shortcut}]} for every editor action.",
          objectSchema({}), true, false, true },
        { "set_shortcut", "ui", "Rebind a shortcut",
          "Bind keys to an action id from list_shortcuts. An empty keys string clears the binding. On "
          "a clash the call FAILS with error \"conflict\" and the conflicting action's label in detail; "
          "nothing is rebound. Not undoable.",
          objectSchema({{QStringLiteral("action"), stringProp(QStringLiteral("Action id from list_shortcuts"))},
                        {QStringLiteral("keys"), stringProp(QStringLiteral("Qt key sequence, e.g. Ctrl+S; empty string clears"))}},
                       {QStringLiteral("action"), QStringLiteral("keys")}) },
        { "reset_shortcuts", "ui", "Restore default shortcuts",
          "Reset every action binding to its default. Not undoable.",
          objectSchema({}), false, true }
#include "mcp/McpCatalogExtendedOps.inl"
    };
    return k;
}

QJsonObject opTool(const Op &op)
{
    return toolDef(QString::fromUtf8(op.name),
                   QStringLiteral("When: %1. %2").arg(QString::fromUtf8(op.when),
                                                      QString::fromUtf8(op.description)),
                   op.schema,
                   toolAnnotations(op.readOnly, op.destructive, op.idempotent));
}

QJsonArray endpointList()
{
    QJsonArray endpoints;
    endpoints.append(QStringLiteral("/mcp"));
    for (const QString &name : toolboxNames()) {
        if (name != QStringLiteral("mcp"))
            endpoints.append(QStringLiteral("/mcp/") + name);
    }
    return endpoints;
}

} // namespace

QStringList toolboxNames()
{
    return {QStringLiteral("media"),     QStringLiteral("timeline"), QStringLiteral("canvas"),
            QStringLiteral("playback"),  QStringLiteral("text"),     QStringLiteral("effects"),
            QStringLiteral("project"),   QStringLiteral("keyframes"), QStringLiteral("speed"),
            QStringLiteral("ui"),        QStringLiteral("shapes"),   QStringLiteral("subtitles"),
            QStringLiteral("segmentation"), QStringLiteral("ai"),   QStringLiteral("audio"),
            QStringLiteral("scene"),     QStringLiteral("multicam")};
}

QStringList undoExemptOps()
{
    static const QStringList k = {
        QStringLiteral("import_media"),        QStringLiteral("import_media_bytes"),
        QStringLiteral("seek"),                QStringLiteral("play"),
        QStringLiteral("pause"),               QStringLiteral("undo"),
        QStringLiteral("redo"),                QStringLiteral("undo_to"),
        QStringLiteral("take_snapshot"),       QStringLiteral("restore_snapshot"),
        QStringLiteral("set_overlap"),         QStringLiteral("set_ripple"),
        QStringLiteral("set_snap"),            QStringLiteral("set_guides"),
        QStringLiteral("set_loop_work_area"),  QStringLiteral("export_video"),
        QStringLiteral("save_project"),        QStringLiteral("set_theme"),
        QStringLiteral("set_shortcut"),        QStringLiteral("reset_shortcuts"),
        QStringLiteral("set_beat_layers"),     QStringLiteral("detect_beats"),
        QStringLiteral("list_speed_curve"),    QStringLiteral("list_fade_curve"),
        QStringLiteral("install_addon"),       QStringLiteral("cancel_addon_install"),
        QStringLiteral("set_acceleration"),    QStringLiteral("switch_angle"),
        QStringLiteral("end_multicam"),
    };
    return k;
}

QStringList selectionBasedOps()
{
    static const QStringList k = {
        QStringLiteral("separate_audio"), QStringLiteral("unlink_audio"), QStringLiteral("link_clips"),
        QStringLiteral("merge_clips"),    QStringLiteral("align_clip_left"),
        QStringLiteral("align_clip_right"), QStringLiteral("copy_selection"),
        QStringLiteral("cut_selection"),
    };
    return k;
}

QString agentGuideText()
{
    return QStringLiteral(
        "Drift MCP agent guide\n"
        "\n"
        "Workflow:\n"
        "1. Call catalog on POST /mcp (homepage).\n"
        "2. Call toolbox({name}) for JSON schemas of ops in that toolbox.\n"
        "3. Call apply({ops:[{tool, args}, …]}) to run one or many mutations in one undo step.\n"
        "4. Call inspect({clips:true, detail:true}) for clip UUIDs, effect stacks, mask/fade/speed, "
        "subtitle cues, face-track/stabilize fields, and job state. inspect always reports "
        "selection:{track,index,clip} (omitted when nothing is selected) and "
        "undo:{can,canRedo,depth,index,hash}. inspect({clips:true,cues:true}) adds subtitleCues without "
        "the rest of detail.\n"
        "5. Call capture() for a composition still (JPEG by default).\n"
        "\n"
        "Pinned endpoints (/mcp/media, /mcp/timeline, …) list toolbox ops directly. "
        "catalog, toolbox, and apply are only on /mcp; inspect and capture work on both. "
        "Toolbox ops may also be called by name directly on /mcp instead of through apply, but only "
        "apply gives you one undo step for a batch.\n"
        "\n"
        "Conventions:\n"
        "- Times are seconds. Track index 0 is the top lane.\n"
        "- Identify clips by UUID from inspect({clips:true}); track+index are positional and shift as\n"
        "  tracks and clips move. Clip-ref ops need one or the other — they never fall back to the\n"
        "  selection.\n"
        "- Clip overlap is off by default (place/move snap to the next gap; the reply reports\n"
        "  requested vs placed).\n"
        "- Every op returns {ok:true, …} or {ok:false, error:<code>, detail:<text>}. Codes: bad_args,\n"
        "  not_found, type_mismatch, unknown_op, unknown_toolbox, wrong_endpoint, wrong_toolbox,\n"
        "  apply_failed, import_failed, import_timeout, export_busy, export_failed, export_timeout,\n"
        "  capture_failed, conflict.\n"
        "- apply is not atomic: on failure the ops before it stay applied. Check stopped/failed.\n"
        "\n"
        "Selection-based ops take no clip argument and act on the current selection — call\n"
        "select_clip or select_clips first: separate_audio, unlink_audio, link_clips, merge_clips,\n"
        "align_clip_left, align_clip_right, copy_selection, cut_selection.\n"
        "freeze_frame and paste_at_playhead are playhead-based, not selection-based — seek first.\n"
        "\n"
        "History is linear, like git without branches. list_history returns every version (index 0 =\n"
        "Origin) with a SHA-256 hash of that version's compact project JSON. undo_to({hash}) or\n"
        "undo_to({index}) jumps; the next edit discards redo. take_snapshot writes <hash>.json whose\n"
        "bytes hash to the same value; restore_snapshot loads a disk copy only if it has fallen off\n"
        "the stack (and resets Origin).\n"
        "\n"
        "Async jobs return {started:true} immediately. Poll these fields, all of which need\n"
        "inspect({detail:true}) except export:\n"
        "- export_video      -> export.{active,progress}      (or export_status)\n"
        "- package_project   -> package.{active,progress}\n"
        "- generate_subtitles-> subtitleGen.{active,progress,status}\n"
        "- detect_scenes     -> sceneDetect.{active,progress,status}\n"
        "- set_clip_reverse  -> reverseRender.{active,progress,status}\n"
        "Segmentation, denoise, and face detection report through the app's status only; re-read\n"
        "inspect({clips:true,detail:true}) and compare to detect completion.\n"
        "\n"
        "Working to the music (audio toolbox):\n"
        "1. detect_beats({start, duration}) blocks and returns bpm plus exact beat and onset times.\n"
        "2. set_beat_layers({grid:true}) turns those beats into snap targets, so place_clip,\n"
        "   move_clip and move_to_track magnet to the nearest beat within 150 ms from then on.\n"
        "3. split_on_beats and snap_clips_to_beats cut and quantise against the same grid;\n"
        "   bookmark_beats writes it into the project as markers that survive re-analysis.\n"
        "The analysis is transient: any edit that changes the mix drops it, and\n"
        "inspect({detail:true}).beats.stale says whether what you have still describes the audio.\n"
        "\n"
        "Understanding the footage (scene toolbox):\n"
        "1. detect_scenes({clip, with_objects:true}) starts a scan; poll\n"
        "   inspect({detail:true}).sceneDetect until active is false. A clip already scanned at\n"
        "   the same settings comes back {cached:true} straight away.\n"
        "2. describe_clip gives the one-call impression — shot count, pacing, what is in it.\n"
        "   list_scenes gives every shot, with timeline_start/timeline_end already mapped through\n"
        "   trim, speed and reverse so you can act on them directly.\n"
        "3. find_scenes({label}) searches EVERY scanned clip on the timeline, which is how you\n"
        "   gather material rather than inspect one clip at a time.\n"
        "4. split_on_scenes cuts a clip at its boundaries; bookmark_scenes marks them instead.\n"
        "Unlike beats this analysis is NOT transient: it describes the source file and is cached\n"
        "against that file's timestamp, so it survives edits, undo and reload. with_objects needs\n"
        "the object-model addon — ai_capabilities reports what is installed; list_addons /\n"
        "install_addon can install a missing model from the addon store.\n"
        "\n"
        "Finding media: import_media takes absolute paths only. Drift will not list folders. When\n"
        "the user gives a partial name, glob or search the host with YOUR own tools (find, ls,\n"
        "whatever the client exposes), then pass those absolute paths to import_media and confirm\n"
        "missing:[] is empty.\n"
        "\n"
        "Toolboxes: media, timeline, canvas, playback, text, effects, project, keyframes, speed, ui, "
        "shapes, subtitles, segmentation, ai, audio, scene, multicam.\n");
}

QJsonObject catalogPayload()
{
    struct Box {
        const char *name;
        const char *when;
    };
    static const Box boxes[] = {
        {"media", "Import (absolute paths only — resolve fuzzy names with your own filesystem tools) "
                   "and inspect the media bin before placing clips."},
        {"timeline", "Tracks, place/move/trim/split/delete clips, ripple/gap, overlap, selection, undo."},
        {"canvas", "Transform, flip, blend, mask, fade, speed, reverse, animation, and stabilisation. "
                   "Constant speed is set_clip_speed here; ramps are set_speed_curve in speed. "
                   "Write fade curves with set_fade_curve here; read a custom curve with list_fade_curve in speed."},
        {"playback", "Seek, play, pause, In/Out work area. Use capture (homepage) to see the frame."},
        {"text", "Add and edit title/caption clips."},
        {"effects", "Video/audio effects and transitions."},
        {"project", "Canvas size, background, metadata, save, and export."},
        {"keyframes", "Animate clip and effect properties over time."},
        {"speed", "Variable playback speed (retimed clips). Constant speed is set_clip_speed in canvas. "
                  "list_fade_curve lives here; write fades with set_fade_curve in canvas."},
        {"ui", "Editor theme and keyboard shortcuts."},
        {"shapes", "Builtin shapes, stickers, emoji, fonts."},
        {"subtitles", "Subtitle clips, import/export, Whisper generation."},
        {"segmentation", "SAM-style cutout and mask output."},
        {"ai", "Denoise, face detection, auto-reframe, model add-ons (list/install), acceleration."},
        {"audio", "Waveforms, silence, loudness, ducking, beat detection, beat-synced cuts, clip volume."},
        {"scene", "Detect shots, read what is in them, and cut or assemble against them."},
        {"multicam", "Multi-camera session: set up angles, switch at the playhead, save separate or combined."},
    };

    QJsonArray toolboxes;
    for (const Box &box : boxes) {
        QJsonArray opEntries;
        for (const Op &op : ops()) {
            if (qstrcmp(op.toolbox, box.name) == 0) {
                opEntries.append(QJsonObject{
                    {QStringLiteral("name"), QString::fromUtf8(op.name)},
                    {QStringLiteral("when"), QString::fromUtf8(op.when)},
                });
            }
        }
        toolboxes.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(box.name)},
            {QStringLiteral("when"), QString::fromUtf8(box.when)},
            {QStringLiteral("ops"), opEntries},
        });
    }

    return ok({
        {QStringLiteral("toolboxes"), toolboxes},
        {QStringLiteral("endpoints"), endpointList()},
        {QStringLiteral("units"),
         QJsonObject{{QStringLiteral("time"), QStringLiteral("seconds")},
                     {QStringLiteral("trackIndex"), QStringLiteral("0=top")},
                     {QStringLiteral("clipId"), QStringLiteral("stable UUID")}}},
        {QStringLiteral("workflow"),
         QStringLiteral("catalog → toolbox({name}) → apply({ops:[{tool,args}…]})")},
        {QStringLiteral("hint"),
         QStringLiteral("toolbox({name}) then apply({ops:[{tool,args}…]}) for a batch. "
                        "inspect({clips:true,detail:true}) for full clip rows: effect stacks, "
                        "transitions, mask/fade/speed/reverse/animation, subtitleCues, volume, "
                        "keyframes, hasFaceTrack, stabilize* fields, and async job state. "
                        "inspect always includes selection and undo. capture() for a still.")},
        {QStringLiteral("limitations"),
         QJsonArray{
             QStringLiteral("apply is not atomic — on failure the ops before it stay applied; check stopped/failed."),
             QStringLiteral("apply cannot run catalog, toolbox, inspect, capture, or apply; call those directly on /mcp."),
             QStringLiteral("Clip-ref ops need clip, or track+index — they never fall back to the current selection. Read the current selection from inspect.selection."),
             QStringLiteral("Effect, transition, and bookmark indices/ids are only discoverable via inspect({clips:true,detail:true})."),
             QStringLiteral("set_effect_param, set_audio_effect_param, and set_transition_param do not validate key or index; verify with inspect."),
             QStringLiteral("set_transform writes at the playhead and becomes a keyframe when the property is animated or autoKey is on."),
             QStringLiteral("set_mask replaces the whole mask; omitted keys revert to defaults. Read inspect({clips:true,detail:true}).tracks[].items[].mask first."),
             QStringLiteral("set_subtitle_cues replaces every cue. Read inspect({clips:true,cues:true}) or detail rows' subtitleCues first."),
             QStringLiteral("Segmentation, denoise, and face detection expose no progress field — diff inspect to detect completion."),
             QStringLiteral("These ops sit outside the undo stack (and so do all read-only ops): %1. "
                            "set_beat_layers is the dangerous one: it changes the user's own snapping and cannot be undone.")
                 .arg(undoExemptOps().join(QStringLiteral(", "))),
             QStringLiteral("set_speed_curve returns a new clip id and the previous UUID stops resolving — end the apply batch after it."),
             QStringLiteral("generate_subtitles must run AFTER remove_silence; silence removal shifts the timeline and would invalidate cue times."),
             QStringLiteral("import_media takes absolute paths only. There is no directory listing — when the user names a file loosely, glob/search with your own filesystem tools, then import and check missing:[]."),
         }},
        {QStringLiteral("guide"), agentGuideText()},
    });
}

QJsonObject toolboxPayload(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (!toolboxNames().contains(key))
        return err("unknown_toolbox", QStringLiteral("Known: %1").arg(toolboxNames().join(QLatin1Char(' '))));

    QJsonArray tools;
    for (const Op &op : ops()) {
        if (key == QLatin1String(op.toolbox))
            tools.append(opTool(op));
    }
    return ok({{QStringLiteral("name"), key}, {QStringLiteral("tools"), tools}});
}

QJsonArray homepageTools()
{
    const QStringList toolboxEnum = toolboxNames();
    QJsonArray tools;
    tools.append(toolDef(QStringLiteral("catalog"),
                         QStringLiteral("When: Start here. Returns toolboxes, per-op when hints, endpoints, units, and workflow (no schemas)."),
                         objectSchema({}),
                         toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("toolbox"),
        QStringLiteral("When: Load schemas. Returns full JSON schemas for one toolbox's ops. Then call those ops via apply."),
        objectSchema({{QStringLiteral("name"), enumProp(QStringLiteral("Toolbox name"), toolboxEnum)}},
                     {QStringLiteral("name")})));
    tools.append(toolDef(
        QStringLiteral("inspect"),
        QStringLiteral("When: Read state. Returns revision, name, w, h, fps, dur, playhead, playing, overlap, "
                       "clips (count), tracks, assets, path, dirty, background, export {active, progress}, "
                       "selection {track, index, clip} (absent when nothing is selected), and "
                       "undo {can, canRedo, depth, index, hash}. work_in/work_out appear only when a work area is set. "
                       "clips=true adds per-clip rows (id, start, duration, trim) under tracks[].items. "
                       "cues=true adds subtitleCues [{start,end,text}] to those rows without the rest of detail. "
                       "detail=true is REQUIRED to see the full QML clip map: effect stacks, transition ids, "
                       "per-track waveform/height, bookmarks, mask, fade/fadeCurve/fadeShape, speed, reverse, "
                       "blend, animIn/animOut, volume, keyframes, subtitleCues, hasFaceTrack, "
                       "stabilized/stabilizing/stabilizeMode/stabilizeSmoothing/stabilizeTripod/stabilizeStale/"
                       "stabilizeProgress/stabilizeStatus, and the package/subtitleGen/reverseRender/sceneDetect/"
                       "beats job state. since=<revision> returns {unchanged:true, revision} when nothing changed."),
        objectSchema({{QStringLiteral("clips"), boolProp(QStringLiteral("Include per-clip rows under tracks[].items"))},
                      {QStringLiteral("detail"),
                       boolProp(QStringLiteral("Expand to full clip maps: effects, transitions, mask/fade/speed, "
                                               "cues, face track, stabilize*, bookmarks, and async job state. Combine with clips=true."))},
                      {QStringLiteral("cues"),
                       boolProp(QStringLiteral("Include subtitleCues on clip rows without requiring detail. Use this for an hour-long transcript index."))},
                      {QStringLiteral("since"),
                       integerProp(QStringLiteral("Revision from a prior inspect; returns {unchanged:true} when current"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("apply"),
        QStringLiteral("When: Mutate. Runs ops in order and stops at the first failure. NOT atomic — ops before the failure stay applied; the reply is {ok:false, error:\"apply_failed\", stopped:<index>, failed:<that op's error>, done:[results so far]}. On success: {ok:true, n, done}. Successful mutations collapse into a single undo step labelled from the op names, except the ops listed in catalog.limitations (import, save, playback, export, view-state toggles including set_beat_layers), which undo cannot revert. Only toolbox ops go here — catalog, toolbox, inspect, capture, and apply itself return unknown_op, so call those directly. An ops array cannot reference an id produced earlier in the same batch — end the batch after set_speed_curve or any op that mints a clip id you need next."),
        objectSchema({{QStringLiteral("ops"),
                       arrayProp(objectSchema({{QStringLiteral("tool"), stringProp(QStringLiteral("Toolbox op name, e.g. place_clip"))},
                                               {QStringLiteral("args"),
                                                QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}},
                                              {QStringLiteral("tool")}),
                                 QStringLiteral("Sequential operations"))}},
                     {QStringLiteral("ops")})));
    tools.append(toolDef(
        QStringLiteral("capture"),
        QStringLiteral("When: Verify visually. Still of the composition at `at`, defaulting to the playhead. Pauses playback first. By default returns an inline JPEG scaled to a 1280px long edge plus {at, w, h, full}; full=true instead writes a full-resolution PNG next to the project's frame captures and returns its path in the reply. Cannot be used inside apply."),
        objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds (default: playhead)"))},
                      {QStringLiteral("full"), boolProp(QStringLiteral("Full-res PNG on disk instead of inline JPEG"))}}),
        toolAnnotations(true, false, true)));
    return tools;
}

QJsonArray toolboxDirectTools(const QString &name)
{
    const QString key = name.trimmed().toLower();
    QJsonArray tools;
    for (const Op &op : ops()) {
        if (key == QLatin1String(op.toolbox))
            tools.append(opTool(op));
    }
    return tools;
}

bool isHomepageTool(const QString &name)
{
    static const QStringList k = {QStringLiteral("catalog"), QStringLiteral("toolbox"),
                                  QStringLiteral("inspect"), QStringLiteral("apply"),
                                  QStringLiteral("capture")};
    return k.contains(name);
}

bool isKnownOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return true;
    }
    return false;
}

bool isReadOnlyOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return op.readOnly;
    }
    return false;
}

QString toolboxForOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return QString::fromUtf8(op.toolbox);
    }
    return {};
}

QString homepageHtml()
{
    const QJsonObject cat = catalogPayload();
    QString body = QStringLiteral(
        "<!doctype html><meta charset=utf-8><title>Drift MCP</title>"
        "<body style='font:14px/1.45 system-ui;max-width:42rem;margin:2rem auto;padding:0 1rem'>"
        "<h1>Drift agent access</h1>"
        "<p>This editor is exposing an MCP server on localhost. Any local process with the "
        "session token can edit the open project and capture frames.</p>"
        "<p><strong>Workflow:</strong> %1</p>"
        "<p>Agents: POST JSON-RPC to <code>/mcp</code> with "
        "<code>Authorization: Bearer …</code>.</p>"
        "<h2>Toolboxes</h2><ul>")
                    .arg(cat.value(QStringLiteral("workflow")).toString());
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    for (const QJsonValue &v : boxes) {
        const QJsonObject b = v.toObject();
        QStringList names;
        for (const QJsonValue &op : b.value(QStringLiteral("ops")).toArray())
            names.append(op.toObject().value(QStringLiteral("name")).toString());
        body += QStringLiteral("<li><strong>%1</strong> — %2<br><code>%3</code></li>")
                    .arg(b.value(QStringLiteral("name")).toString(),
                         b.value(QStringLiteral("when")).toString(),
                         names.join(QStringLiteral(", ")));
    }
    QStringList endpointStrings;
    for (const QJsonValue &ep : cat.value(QStringLiteral("endpoints")).toArray())
        endpointStrings.append(QStringLiteral("<code>%1</code>").arg(ep.toString()));
    body += QStringLiteral("</ul><p>Pinned endpoints: %1.</p></body>").arg(endpointStrings.join(QStringLiteral(", ")));
    return body;
}

} // namespace drift::mcp
