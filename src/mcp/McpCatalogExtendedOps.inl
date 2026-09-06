        ,
        { "remove_asset", "media", "Delete a bin row",
          "Remove an asset from the media bin. Clips already placed on the timeline keep playing and "
          "are NOT deleted.",
          objectSchema({{QStringLiteral("asset"), assetRefProp()}},
                       {QStringLiteral("asset")}),
          false, true },
        { "replace_asset", "media", "Swap source file",
          "Point an existing bin row at a different file. Every clip using the asset switches to the "
          "new media, keeping its trim and timeline position — trims past the new file's duration are "
          "clamped downstream.",
          objectSchema({{QStringLiteral("asset"), assetRefProp()},
                        {QStringLiteral("path"), stringProp(QStringLiteral("Absolute path or file:// URL; must exist"))}},
                       {QStringLiteral("asset"), QStringLiteral("path")}) },
        { "export_asset_image", "media", "Export a still from an asset",
          "Write an image file from a bin asset (its poster frame for video). Creates parent folders. "
          "The format follows the path's extension.",
          objectSchema({{QStringLiteral("asset"), assetRefProp()},
                        {QStringLiteral("path"), stringProp(QStringLiteral("Absolute output path, extension picks the format"))}},
                       {QStringLiteral("asset"), QStringLiteral("path")}) },
        { "import_media_bytes", "media", "Import from base64",
          "Decode base64 bytes to a file, then import it like import_media. ALWAYS pass an explicit "
          "`path`: with only `name` the file is written to a temporary directory that is deleted as "
          "soon as the call returns, leaving the asset pointing at a missing file. Returns the same "
          "shape as import_media.",
          objectSchema({{QStringLiteral("data"), stringProp(QStringLiteral("Base64-encoded file bytes"))},
                        {QStringLiteral("name"), stringProp(QStringLiteral("Filename hint (e.g. clip.mp4). Used only when path is omitted."))},
                        {QStringLiteral("path"), stringProp(QStringLiteral("Absolute write path — strongly recommended; parent folders are created"))}},
                       {QStringLiteral("data")}) },

        { "load_project", "project", "Open a project file",
          "Load a .drift bundle or a JSON document from Save as JSON, replacing the open timeline. "
          "JSON is an import: inspect.path stays empty and save_project still needs a .drift path. "
          "DISCARDS unsaved changes without warning and clears the undo stack — save_project first "
          "if the current project matters (inspect.dirty tells you).",
          objectSchema({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute .drift or .json path"))}},
                       {QStringLiteral("path")}),
          false, true },
        { "new_project", "project", "Start empty",
          "Discard the current timeline and create an empty project. DISCARDS unsaved changes without "
          "warning and clears the undo stack — check inspect.dirty and save_project first.",
          objectSchema({}), false, true },
        { "package_project", "project", "Save bundled copy",
          "Write a copy of the project with all media embedded. Async: returns {started:true, path} "
          "immediately — poll inspect({detail:true}).package.{active,progress} until active is false.",
          objectSchema({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute output .drift path"))}},
                       {QStringLiteral("path")}) },
        { "cancel_package", "project", "Stop packaging",
          "Cancel an in-flight package job. Returns ok even when nothing was running; confirm with "
          "inspect({detail:true}).package.active.",
          objectSchema({}) },
        { "cancel_export", "project", "Stop encoding",
          "Cancel an in-flight export. Returns ok even when nothing was running; confirm with "
          "export_status. Use this before a new export_video if one is already busy.",
          objectSchema({}) },
        { "apply_canvas_crop", "project", "Crop canvas",
          "Crop the project canvas to a pixel rectangle, changing the project width/height. Clip "
          "transforms are rebased onto the new canvas. width and height must be > 0.",
          objectSchema({{QStringLiteral("x"), numberProp(QStringLiteral("Left pixels"))},
                        {QStringLiteral("y"), numberProp(QStringLiteral("Top pixels"))},
                        {QStringLiteral("width"), numberProp(QStringLiteral("Width pixels, must be > 0"))},
                        {QStringLiteral("height"), numberProp(QStringLiteral("Height pixels, must be > 0"))}},
                       {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")}) },

        { "move_track", "timeline", "Reorder lanes",
          "Move a track from one index to another; the tracks between them shift. Every track index "
          "and every track+index clip reference you hold is invalidated — re-read inspect after this.",
          objectSchema({{QStringLiteral("from"), integerProp(QStringLiteral("Source track index"))},
                        {QStringLiteral("to"), integerProp(QStringLiteral("Destination track index"))}},
                       {QStringLiteral("from"), QStringLiteral("to")}) },
        { "set_track_waveform", "timeline", "Show/hide waveform",
          "Toggle waveform display on an audio/video track row. Cosmetic — does not affect rendering "
          "or export.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("show"), boolProp(QStringLiteral("Show waveform"))}},
                       {QStringLiteral("track"), QStringLiteral("show")}) },
        { "set_track_height", "timeline", "Resize a lane",
          "Set the track's row height multiplier, clamped to 0.6–4.0. Cosmetic — does not affect "
          "rendering or export.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("scale"), numberProp(QStringLiteral("Height multiplier, clamped 0.6..4.0"))}},
                       {QStringLiteral("track"), QStringLiteral("scale")}) },
        { "select_clip", "timeline", "Focus one clip",
          "Select exactly one clip, replacing any previous selection. REQUIRED before the "
          "selection-based ops, which take no clip argument: separate_audio, unlink_audio, "
          "merge_clips, align_clip_left, align_clip_right, copy_selection, cut_selection. "
          "freeze_frame and paste_at_playhead are playhead-based — seek first, do not select.",
          objectSchema(clipRefProps()) },
        { "clear_selection", "timeline", "Deselect all",
          "Clear the clip selection. Selection-based ops fail afterwards until you select again.",
          objectSchema({}) },
        { "select_all_clips", "timeline", "Select everything",
          "Select every clip on the timeline. Useful before copy_selection or cut_selection.",
          objectSchema({}) },
        { "copy_selection", "timeline", "Copy to clipboard",
          "Copy the selected clips to the internal clipboard. Acts on the current selection — call "
          "select_clip or select_all_clips first. Paste with paste_at_playhead.",
          objectSchema({}) },
        { "cut_selection", "timeline", "Cut to clipboard",
          "Cut the selected clips to the internal clipboard, removing them from the timeline. Acts on "
          "the current selection — call select_clip or select_all_clips first.",
          objectSchema({}), false, true },
        { "paste_at_playhead", "timeline", "Paste clips",
          "Paste the internal clipboard at the playhead. Requires an earlier copy_selection or "
          "cut_selection; returns ok with no effect when the clipboard is empty. Playhead-based — "
          "seek first; selecting a clip does not change where this lands. Returns {ids:[…], n} of "
          "the new clips.",
          objectSchema({}) },
        { "separate_audio", "timeline", "Split A/V",
          "Detach the audio of the selected video clip onto its own audio track. Acts on the current "
          "selection — call select_clip first; fails bad_args when nothing separable is selected.",
          objectSchema({}) },
        { "unlink_audio", "timeline", "Break A/V link",
          "Break the link between paired audio/video clips in the selection so they can be moved and "
          "trimmed independently. Acts on the current selection — call select_clip first; fails "
          "bad_args when no linked clips are selected.",
          objectSchema({}) },
        { "merge_clips", "timeline", "Join adjacent clips",
          "Merge the selected adjacent clips on one track back into a single clip. Acts on the current "
          "selection — select the clips first; fails bad_args when the selection cannot be merged.",
          objectSchema({}) },
        { "align_clip_left", "timeline", "Snap start to zero",
          "Move the selected clip so it starts at 0. Acts on the current selection — call select_clip "
          "first.",
          objectSchema({}) },
        { "align_clip_right", "timeline", "Snap end to duration",
          "Move the selected clip so it ends at the project duration. Acts on the current selection — "
          "call select_clip first.",
          objectSchema({}) },
        { "set_clip_name", "timeline", "Rename a clip",
          "Set the clip's display name on the timeline. Cosmetic; does not touch the bin asset.",
          objectSchema(mergeProps({{QStringLiteral("name"), stringProp(QStringLiteral("New name, must be non-empty"))}},
                                  clipRefProps()),
                       {QStringLiteral("name")}) },
        { "add_bookmark", "timeline", "Mark a time",
          "Add a bookmark at `at` seconds. Does not return its index — bookmarks are addressed by "
          "position in inspect({detail:true}).bookmarks, so re-read that before removing or updating.",
          objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds"))},
                        {QStringLiteral("label"), stringProp(QStringLiteral("Label"))}},
                       {QStringLiteral("at")}) },
        { "remove_bookmark", "timeline", "Delete bookmark",
          "Remove a bookmark by its position in inspect({detail:true}).bookmarks. Later bookmarks shift "
          "down one index.",
          objectSchema({{QStringLiteral("index"), bookmarkIndexProp()}},
                       {QStringLiteral("index")}),
          false, true },
        { "update_bookmark", "timeline", "Edit bookmark",
          "Update a bookmark's time and/or label. Pass BOTH at and label: an omitted field is read back "
          "from the existing bookmark, and an out-of-range index is not checked on that path. Verify "
          "the index against inspect({detail:true}).bookmarks first.",
          objectSchema({{QStringLiteral("index"), bookmarkIndexProp()},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Seconds; omitted keeps the current time"))},
                        {QStringLiteral("label"), stringProp(QStringLiteral("Label; omitted keeps the current label"))}},
                       {QStringLiteral("index")}) },
        { "go_to_bookmark", "timeline", "Seek to bookmark",
          "Move the playhead to a bookmark, addressed by its position in "
          "inspect({detail:true}).bookmarks.",
          objectSchema({{QStringLiteral("index"), bookmarkIndexProp()}},
                       {QStringLiteral("index")}) },
        { "freeze_frame", "timeline", "Hold current frame",
          "Insert a still of the frame under the playhead as a new clip. Uses the playhead, not a clip "
          "argument and not the selection — seek first. Returns {id, track, index} of the new still.",
          objectSchema({}) },

        { "set_flip", "canvas", "Mirror a clip",
          "Set horizontal and/or vertical flip. Omitted axes keep their current value. At least one of "
          "flipH/flipV is required.",
          objectSchema(mergeProps({{QStringLiteral("flipH"), boolProp(QStringLiteral("Flip horizontally"))},
                                   {QStringLiteral("flipV"), boolProp(QStringLiteral("Flip vertically"))}},
                                  clipRefProps())) },
        { "set_blend_mode", "canvas", "Change compositing",
          "Set how the clip composites over the layers below it. Unrecognised values silently fall back "
          "to normal.",
          objectSchema(mergeProps({{QStringLiteral("mode"),
                                    enumProp(QStringLiteral("Blend mode; unknown values fall back to normal"),
                                             {QStringLiteral("normal"), QStringLiteral("multiply"),
                                              QStringLiteral("screen"), QStringLiteral("overlay"),
                                              QStringLiteral("add"), QStringLiteral("darken"),
                                              QStringLiteral("lighten")})}},
                                  clipRefProps()),
                       {QStringLiteral("mode")}) },
        { "set_mask", "canvas", "Clip mask",
          "Set the clip's mask. This REPLACES the whole mask — it is not a patch. Every omitted key "
          "reverts to its default, and omitting `shape` turns the mask off entirely, so read the "
          "current mask from inspect({clips:true,detail:true}) and send it back with your changes "
          "merged in.",
          objectSchema(mergeProps({{QStringLiteral("mask"), maskSchema()}},
                                  clipRefProps()),
                       {QStringLiteral("mask")}) },
        { "set_fade", "canvas", "Audio/video fade",
          "Set fade-in and fade-out lengths in seconds. Omitted ends keep their current value. A fade "
          "reveals the canvas background (the project background colour or blur), not black. Use "
          "set_fade_curve to change the fade's shape.",
          objectSchema(mergeProps({{QStringLiteral("in"), numberProp(QStringLiteral("Fade in seconds"))},
                                   {QStringLiteral("out"), numberProp(QStringLiteral("Fade out seconds"))}},
                                  clipRefProps())) },
        { "set_fade_curve", "canvas", "Fade shape",
          "Set the shape of this clip's fades, either by preset (`curve`) or by explicit control "
          "points (`points`, which selects the custom curve). Exactly one of the two is required; when "
          "both are sent, points wins. Passing points opens and closes a transient curve session and "
          "closes any fade-curve session already open. A clip whose fade animation is set to Fade "
          "inherits this curve.",
          objectSchema(mergeProps(
              {{QStringLiteral("curve"),
                enumProp(QStringLiteral("Fade curve preset. Unknown values fall back to smooth."),
                         {QStringLiteral("linear"), QStringLiteral("smooth"),
                          QStringLiteral("equalPower"), QStringLiteral("custom")})},
               {QStringLiteral("points"),
                arrayProp(objectSchema({{QStringLiteral("t"), numberProp(QStringLiteral("Time 0..1 across the fade"))},
                                        {QStringLiteral("g"), numberProp(QStringLiteral("Gain 0..1"))}}),
                          QStringLiteral("Custom curve points, at least two. Selects the custom curve."))}},
              clipRefProps())) },
        { "set_clip_speed", "canvas", "Constant speed",
          "Set a single playback rate for the whole clip (1.0 = normal, 2.0 = double speed). Retimes "
          "the clip's timeline duration. Clears any speed curve — use set_speed_curve for a ramp.",
          objectSchema(mergeProps({{QStringLiteral("speed"), numberProp(QStringLiteral("Playback rate, e.g. 0.5 half speed, 2.0 double"))}},
                                  clipRefProps()),
                       {QStringLiteral("speed")}) },
        { "set_clip_reverse", "canvas", "Reverse playback",
          "Play the clip backwards. For video this kicks off an async proxy render — poll "
          "inspect({detail:true}).reverseRender.{active,progress,status} until active is false before "
          "exporting, and cancel it with cancel_reverse_render.",
          objectSchema(mergeProps({{QStringLiteral("reverse"), boolProp(QStringLiteral("Reversed"))}},
                                  clipRefProps()),
                       {QStringLiteral("reverse")}) },
        { "cancel_reverse_render", "canvas", "Stop reverse proxy",
          "Cancel the in-flight reverse proxy render. Returns ok even when nothing was running; "
          "confirm with inspect({detail:true}).reverseRender.active.",
          objectSchema({}) },
        { "set_animation", "canvas", "In/out motion",
          "Set the clip's entrance (animIn) or exit (animOut) animation. Only the supplied fields "
          "change. Setting kind to fade makes the animation follow the clip's fade curve.",
          objectSchema(mergeProps({{QStringLiteral("which"), enumProp(QStringLiteral("Which end of the clip to animate"),
                                                                       {QStringLiteral("animIn"),
                                                                        QStringLiteral("animOut")})},
                                   {QStringLiteral("kind"),
                                    enumProp(QStringLiteral("Animation kind; unknown values fall back to none"),
                                             {QStringLiteral("none"), QStringLiteral("fade"),
                                              QStringLiteral("slideUp"), QStringLiteral("slideDown"),
                                              QStringLiteral("slideLeft"), QStringLiteral("slideRight"),
                                              QStringLiteral("zoomIn"), QStringLiteral("zoomOut"),
                                              QStringLiteral("pop"), QStringLiteral("spinCW"),
                                              QStringLiteral("spinCCW"), QStringLiteral("bounce")})},
                                   {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds"))},
                                   {QStringLiteral("ease"),
                                    enumProp(QStringLiteral("Easing; unknown values fall back to easeOut"),
                                             {QStringLiteral("linear"), QStringLiteral("easeInOut"),
                                              QStringLiteral("easeOut"), QStringLiteral("back")})},
                                   {QStringLiteral("curve"),
                                    enumProp(QStringLiteral("Fade curve used when kind is fade"),
                                             {QStringLiteral("linear"), QStringLiteral("smooth"),
                                              QStringLiteral("equalPower"), QStringLiteral("custom")})}},
                                  clipRefProps()),
                       {QStringLiteral("which")}) },
        { "set_shape_style", "canvas", "Shape look",
          "Patch the fill, stroke, and geometry of a shape clip. Only supplied keys change. Applies to "
          "shape clips only — silently does nothing on any other clip type. Numeric fields are clamped "
          "to the ranges in the schema, and each geometry knob is read by a subset of shape kinds only.",
          objectSchema(mergeProps({{QStringLiteral("style"), shapeStyleSchema()}},
                                  clipRefProps()),
                       {QStringLiteral("style")}) },

        { "list_shapes", "shapes", "Builtin shapes",
          "Returns {shapes:[{id, label, cat}]}. Use id with add_shape or as shape_style.kind.",
          objectSchema({}), true, false, true },
        { "list_stickers", "shapes", "Builtin stickers",
          "Returns {stickers:[{id, label, cat}]}. Use id with add_sticker.",
          objectSchema({}), true, false, true },
        { "list_emoji", "shapes", "Emoji catalog",
          "Returns {emoji:[{id, label}]}. add_emoji takes the emoji character itself, not the id.",
          objectSchema({}), true, false, true },
        { "list_text_presets", "shapes", "Text style packs",
          "Returns {presets:[{id, label}]}. Use id as the preset argument to add_text or "
          "apply_text_preset.",
          objectSchema({}), true, false, true },
        { "list_fonts", "shapes", "Font list",
          "Returns {fonts:[{id, label}]} for fonts available on this machine. Use the family name as "
          "style.fontFamily in set_text.",
          objectSchema({}), true, false, true },
        { "add_shape", "shapes", "Place a shape",
          "Add a builtin shape clip, creating a shape track when needed. Returns {id, track, index}. "
          "Style it afterwards with set_shape_style.",
          objectSchema({{QStringLiteral("shape"), stringProp(QStringLiteral("Shape id from list_shapes"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))},
                        {QStringLiteral("track"), integerProp(QStringLiteral("Optional destination track; omitted picks or creates one"))}},
                       {QStringLiteral("shape")}) },
        { "add_sticker", "shapes", "Place a sticker",
          "Add a sticker clip, creating a track when needed. Returns {id, track, index}.",
          objectSchema({{QStringLiteral("sticker"), stringProp(QStringLiteral("Sticker id from list_stickers"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))}},
                       {QStringLiteral("sticker")}) },
        { "add_emoji", "shapes", "Place emoji",
          "Add an emoji clip, creating a track when needed. Takes the emoji CHARACTER (e.g. \"🎬\"), not "
          "a catalog id. Returns {id, track, index}.",
          objectSchema({{QStringLiteral("emoji"), stringProp(QStringLiteral("The emoji character itself, e.g. 🎬"))},
                        {QStringLiteral("name"), stringProp(QStringLiteral("Optional display name for the clip"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))}},
                       {QStringLiteral("emoji")}) },

        { "add_subtitle_clip", "subtitles", "Empty subtitle lane",
          "Add an empty subtitle clip, creating a subtitle track when needed. Returns {id, track, index}. "
          "Fill it with set_subtitle_cues or import_subtitle_into_clip.",
          objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))}}) },
        { "import_subtitle_file", "subtitles", "Load SRT/VTT",
          "Import an .srt or .vtt file as a NEW subtitle clip. Returns {id, track, index}. Use "
          "import_subtitle_into_clip to fill an existing clip instead.",
          objectSchema({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute .srt or .vtt path"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Start seconds (default: playhead)"))}},
                       {QStringLiteral("path")}) },
        { "import_subtitle_into_clip", "subtitles", "Load into clip",
          "Import a subtitle file into an existing subtitle clip, replacing its cues.",
          objectSchema(mergeProps({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute .srt or .vtt path"))}},
                                  clipRefProps()),
                       {QStringLiteral("path")}) },
        { "export_subtitle_file", "subtitles", "Write subtitles",
          "Write a subtitle clip's cues to a file. The format follows the path's extension (.srt/.vtt).",
          objectSchema(mergeProps({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute output path; extension picks the format"))}},
                                  clipRefProps()),
                       {QStringLiteral("path")}) },
        { "set_subtitle_cues", "subtitles", "Replace all cues",
          "REPLACE every cue on a subtitle clip. This is not a merge — cues you leave out are deleted, "
          "so read the current list from inspect({clips:true,cues:true}) or "
          "inspect({clips:true,detail:true}).subtitleCues first when editing. Times "
          "are timeline seconds.",
          objectSchema(mergeProps(
              {{QStringLiteral("cues"),
                arrayProp(objectSchema({{QStringLiteral("start"), numberProp(QStringLiteral("Start seconds"))},
                                        {QStringLiteral("end"), numberProp(QStringLiteral("End seconds"))},
                                        {QStringLiteral("text"), stringProp(QStringLiteral("Caption"))}}),
                           QStringLiteral("Full cue list; replaces the existing one"))}},
              clipRefProps()),
                       {QStringLiteral("cues")}) },
        { "upsert_subtitle_cue", "subtitles", "Edit cue at playhead",
          "Set the text of the cue under the playhead, creating one if there is none. Uses the "
          "playhead, not a time argument — seek first.",
          objectSchema(mergeProps({{QStringLiteral("text"), stringProp(QStringLiteral("Caption text, must be non-empty"))}},
                                  clipRefProps()),
                       {QStringLiteral("text")}) },
        { "list_whisper_languages", "subtitles", "Whisper languages",
          "Returns {languages:[{id, label}]}. Pass an id as generate_subtitles.language.",
          objectSchema({}), true, false, true },
        { "generate_subtitles", "subtitles", "Auto transcribe, including word-by-word captions",
          "Transcribe a clip's audio with Whisper into a new subtitle clip. Async: returns "
          "{started:true} immediately — poll "
          "inspect({detail:true}).subtitleGen.{active,progress,status} until active is false. Cancel "
          "with cancel_subtitle_generation. For word-by-word captions pass max_words_per_cue:1. Run "
          "this AFTER remove_silence — silence removal shifts the timeline and would invalidate cue "
          "times.",
          objectSchema(mergeProps({{QStringLiteral("language"), stringProp(QStringLiteral("Language id from list_whisper_languages; omitted auto-detects"))},
                                   {QStringLiteral("max_words_per_cue"), numberProp(QStringLiteral("Cap words per caption; omit or 0 for the recommended length. Short caps drift slightly out of sync."))}},
                                  clipRefProps())) },
        { "cancel_subtitle_generation", "subtitles", "Stop Whisper",
          "Cancel in-flight subtitle generation. Returns ok even when nothing was running; confirm "
          "with inspect({detail:true}).subtitleGen.active.",
          objectSchema({}) },

        { "set_effect_enabled", "effects", "Toggle effect",
          "Bypass or re-enable a video effect without removing it. The effect keeps its stack position "
          "and parameters.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("enabled"), boolProp(QStringLiteral("Enabled"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("enabled")}) },
        { "move_effect", "effects", "Reorder effect",
          "Move a video effect to a different stack position. Effects render in stack order, so this "
          "changes the result. All indices between from and to shift.",
          objectSchema(mergeProps({{QStringLiteral("from"), effectIndexProp()},
                                   {QStringLiteral("to"), integerProp(QStringLiteral("Destination stack position"))}},
                                  clipRefProps()),
                       {QStringLiteral("from"), QStringLiteral("to")}) },
        { "set_effect_color_param", "effects", "Color effect param",
          "Set a color-typed effect parameter. Use this instead of set_effect_param for params whose "
          "type is color in list_effects. Not validated — an unknown key or bad index still returns ok.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from list_effects"))},
                                   {QStringLiteral("value"), stringProp(QStringLiteral("Color #RRGGBB or #AARRGGBB"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "set_audio_effect_enabled", "effects", "Toggle audio effect",
          "Bypass or re-enable an audio effect without removing it.",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("enabled"), boolProp(QStringLiteral("Enabled"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("enabled")}) },
        { "move_audio_effect", "effects", "Reorder audio effect",
          "Move an audio effect to a different stack position. Audio effects process in stack order.",
          objectSchema(mergeProps({{QStringLiteral("from"), effectIndexProp()},
                                   {QStringLiteral("to"), integerProp(QStringLiteral("Destination stack position"))}},
                                  clipRefProps()),
                       {QStringLiteral("from"), QStringLiteral("to")}) },
        { "list_effect_templates", "effects", "Effect packs",
          "Returns {templates:[{id, label, cat}]} — preset bundles of effects and parameters. Use id "
          "with apply_effect_template.",
          objectSchema({}), true, false, true },
        { "apply_effect_template", "effects", "Apply template",
          "Apply a preset effect pack to a clip, appending its effects to the clip's stack.",
          objectSchema(mergeProps({{QStringLiteral("template"), stringProp(QStringLiteral("Template id from list_effect_templates"))}},
                                  clipRefProps()),
                       {QStringLiteral("template")}) },
        { "set_transition_kind", "effects", "Change transition type",
          "Swap an existing transition to another kind. This clears its parameter overrides.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index the transition lives on"))},
                        {QStringLiteral("id"), transitionIdProp()},
                        {QStringLiteral("kind"), stringProp(QStringLiteral("Transition id from list_transitions"))}},
                       {QStringLiteral("track"), QStringLiteral("id"), QStringLiteral("kind")}) },
        { "set_transition_duration", "effects", "Transition length",
          "Set a transition's length in seconds. Ignored when the two clips physically overlap — the "
          "overlap dictates the duration there.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("id"), transitionIdProp()},
                        {QStringLiteral("duration"), numberProp(QStringLiteral("Seconds"))}},
                       {QStringLiteral("track"), QStringLiteral("id"), QStringLiteral("duration")}) },
        { "set_transition_param", "effects", "Transition param",
          "Set one numeric transition parameter. Not validated — an unknown key or id still returns "
          "ok. Take keys from the transition's params in list_transitions and verify with "
          "inspect({clips:true,detail:true}).",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("id"), transitionIdProp()},
                        {QStringLiteral("key"), stringProp(QStringLiteral("Param key from list_transitions"))},
                        {QStringLiteral("value"), numberProp(QStringLiteral("Value"))}},
                       {QStringLiteral("track"), QStringLiteral("id"), QStringLiteral("key"),
                        QStringLiteral("value")}) },

        { "move_keyframe", "keyframes", "Move a key",
          "Move a keyframe from one time to another, optionally changing its value. `from` must match "
          "an existing key — confirm with list_keyframes. When value is omitted the property's current "
          "value at `from` is carried over.",
          objectSchema(mergeProps({{QStringLiteral("prop"), animPropProp()},
                                   {QStringLiteral("from"), numberProp(QStringLiteral("Existing key's timeline seconds"))},
                                   {QStringLiteral("to"), numberProp(QStringLiteral("New timeline seconds"))},
                                   {QStringLiteral("value"), numberProp(QStringLiteral("Optional new value; omitted keeps the value at `from`"))}},
                                  clipRefProps()),
                       {QStringLiteral("prop"), QStringLiteral("from"), QStringLiteral("to")}) },

        { "list_fade_curve", "speed", "Read custom fade shape",
          "Returns {points:[{t,g}]} for the clip's custom fade curve. Despite being a read, this opens "
          "and closes a transient session and will CLOSE any fade-curve session already open. Fails "
          "bad_args on clips that cannot carry a custom fade. Write with set_fade_curve (canvas).",
          objectSchema(clipRefProps()), false, false, true },

        { "segmentation_status", "segmentation", "Check availability",
          "Returns {available, model, backends}. Check available is true before any other segmentation "
          "op — the models ship separately and the session ops do not report their absence. backends "
          "lists what is installed: sam2 takes hint points and can cut out anything; rvm takes no "
          "points at all and cuts out people only, with a soft edge.",
          objectSchema({}), true, false, true },
        { "begin_segmentation_session", "segmentation", "Open segment UI state",
          "Open an interactive segmentation session on one frame of a clip. REQUIRED before "
          "set_segmentation_frame, add/remove/clear_segmentation_point, and run_segmentation — those "
          "ops do not check, and return ok while doing nothing when no session is open. Close with "
          "end_segmentation_session. Prefer segment_clip for a single one-shot cutout.",
          objectSchema(mergeProps({{QStringLiteral("at"), numberProp(QStringLiteral("Frame seconds (default: playhead)"))},
                                   {QStringLiteral("forTemplate"), boolProp(QStringLiteral("Template mode: produce a reusable mask instead of editing this clip"))}},
                                  clipRefProps())) },
        { "end_segmentation_session", "segmentation", "Close session",
          "End the segmentation session, discarding its points without applying them. Always call this "
          "when abandoning a session — a stale session interferes with later segmentation ops.",
          objectSchema({}) },
        { "set_segmentation_frame", "segmentation", "Pick frame",
          "Move the segmentation preview to another frame. Requires an open session from "
          "begin_segmentation_session; returns ok with no effect otherwise.",
          objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds"))}},
                       {QStringLiteral("at")}) },
        { "add_segmentation_point", "segmentation", "Add SAM point",
          "Add an include or exclude hint point. Requires an open session; returns ok with no effect "
          "otherwise. Coordinates are normalised 0..1 across the frame, not pixels.",
          objectSchema({{QStringLiteral("x"), numberProp(QStringLiteral("X, 0..1 across the frame"))},
                        {QStringLiteral("y"), numberProp(QStringLiteral("Y, 0..1 down the frame"))},
                        {QStringLiteral("include"), propWithDefault(boolProp(QStringLiteral("true keeps this region, false excludes it")), true)}},
                       {QStringLiteral("x"), QStringLiteral("y")}) },
        { "remove_segmentation_point", "segmentation", "Remove SAM point",
          "Remove a hint point by the order it was added. Requires an open session; returns ok with no "
          "effect otherwise.",
          objectSchema({{QStringLiteral("index"), integerProp(QStringLiteral("0-based position in the order points were added"))}},
                       {QStringLiteral("index")}) },
        { "clear_segmentation_points", "segmentation", "Clear points",
          "Remove all hint points but keep the session open. Requires an open session.",
          objectSchema({}) },
        { "run_segmentation", "segmentation", "Run from session",
          "Run segmentation using the session's current points. Requires an open session. Async and "
          "with no progress field — re-read inspect({clips:true,detail:true}) and compare to detect "
          "completion. Cancel with cancel_segmentation.",
          objectSchema({{QStringLiteral("output"),
                         propWithDefault(enumProp(QStringLiteral("clips splits the subject onto its own clip; mask writes a mask onto the existing clip"),
                                                  {QStringLiteral("clips"), QStringLiteral("mask")}),
                                         QStringLiteral("clips"))},
                        {QStringLiteral("backend"),
                propWithDefault(enumProp(QStringLiteral("sam2 needs hint points and cuts out anything; rvm ignores points and cuts out people automatically"),
                                         {QStringLiteral("sam2"), QStringLiteral("rvm")}),
                                QStringLiteral("sam2"))}}) },
        { "segment_clip", "segmentation", "One-shot segment",
          "Segment a clip with explicit points in a single call — no session needed. Prefer this over "
          "the session ops for scripted use. Async and with no progress field: re-read "
          "inspect({clips:true,detail:true}) and compare to detect completion. points are required for "
          "the sam2 backend and ignored by rvm, which finds people on its own.",
          objectSchema(mergeProps(
              {{QStringLiteral("points"),
                arrayProp(objectSchema({{QStringLiteral("x"), numberProp(QStringLiteral("X, 0..1 across the frame"))},
                                        {QStringLiteral("y"), numberProp(QStringLiteral("Y, 0..1 down the frame"))},
                                        {QStringLiteral("include"), boolProp(QStringLiteral("true keeps this region, false excludes it (default true)"))}}),
                           QStringLiteral("Hint points, at least one"))},
               {QStringLiteral("output"),
                propWithDefault(enumProp(QStringLiteral("clips splits the subject onto its own clip; mask writes a mask onto the existing clip"),
                                         {QStringLiteral("clips"), QStringLiteral("mask")}),
                                QStringLiteral("clips"))},
               {QStringLiteral("backend"),
                propWithDefault(enumProp(QStringLiteral("sam2 needs hint points and cuts out anything; rvm ignores points and cuts out people automatically"),
                                         {QStringLiteral("sam2"), QStringLiteral("rvm")}),
                                QStringLiteral("sam2"))}},
              clipRefProps())) },
        { "cancel_segmentation", "segmentation", "Stop segment job",
          "Cancel an in-flight segmentation. Returns ok even when nothing was running.",
          objectSchema({}) },

        { "denoise_status", "ai", "Denoise availability",
          "Returns {available}. Check this before apply_denoise — the model ships separately.",
          objectSchema({}), true, false, true },
        { "apply_denoise", "ai", "Denoise clip",
          "Run AI audio denoise over the clip — this removes background noise (hiss, HVAC, rumble), "
          "not reverb. \"Sounds like a bathroom\" is reverb and this will not fix it. Async and with "
          "no progress field — re-read inspect({clips:true,detail:true}) and compare to detect "
          "completion. Cancel with cancel_denoise.",
          objectSchema(clipRefProps()) },
        { "cancel_denoise", "ai", "Cancel denoise",
          "Cancel an in-flight denoise. Returns ok even when nothing was running.",
          objectSchema({}) },
        { "ai_capabilities", "ai", "What models are installed",
          "Which optional model add-ons are present and what each unlocks, plus the active ONNX "
          "Runtime. Call this before an op that needs a model, or after one fails, so you can "
          "tell the user exactly what to install instead of retrying. Returns "
          "{models:[{kind, installed, unlocks}], runtime, hint}. Missing models install with "
          "install_addon after list_addons; set_acceleration picks the ONNX Runtime.",
          objectSchema({}), true, false, true },
        { "face_detection_status", "ai", "Face track availability",
          "Returns {available}. Check this before detect_faces — the model ships separately.",
          objectSchema({}), true, false, true },
        { "detect_faces", "ai", "Detect faces",
          "Run face detection over a clip and store a face-track sidecar (per-frame anchors: eyes, "
          "nose, mouth, chin, forehead). Read it with list_face_track; auto_reframe consumes it to "
          "write transform keyframes that keep a face in frame. inspect({clips:true,detail:true}) "
          "reports hasFaceTrack. Async and with no progress field — re-read inspect and compare. "
          "Cancel with cancel_face_detection.",
          objectSchema(clipRefProps()) },
        { "cancel_face_detection", "ai", "Cancel face detect",
          "Cancel an in-flight face detection. Returns ok even when nothing was running.",
          objectSchema({}) },
        { "clear_face_track", "ai", "Remove face track",
          "Delete the stored face track from a clip, undoing detect_faces.",
          objectSchema(clipRefProps()), false, true },

        { "audio_summary", "audio", "What carries sound",
          "List every clip that actually reaches the mix — audio clips, plus video clips whose "
          "embedded audio is not suppressed — with per-clip volume keyframe count, audio effect "
          "count, fades, and the asset's sampleRate/channels/hasAudio. Cheaper and far smaller than "
          "inspect({clips:true,detail:true}) when all you need is a range worth analysing. Also "
          "returns beats.{analysed, bpm, stale, gridVisible, onsetsVisible}.",
          objectSchema({}), true, false, true },
        { "get_waveform", "audio", "See the amplitude",
          "Amplitude peaks as an array of numbers in 0..1, one per bucket, evenly spaced across the "
          "range. Three modes: pass clip (or track+index) for that clip's trimmed source window, "
          "asset for a media file, or neither (with start and duration) for the MIXED TIMELINE — "
          "every track summed with volume, fades and mutes applied. WARNING: clip and asset mode "
          "read the source file, so clip speed, reverse and volume are NOT applied; only timeline "
          "mode is what you would actually hear. Silence reads as a true 0, so this is usable for "
          "finding dead air. Blocks while decoding, so the data is there on the first call.",
          objectSchema(mergeProps(
              {{QStringLiteral("asset"), assetRefProp()},
               {QStringLiteral("start"), numberProp(QStringLiteral("Range start in seconds. Timeline seconds in timeline mode, source seconds in asset mode. Ignored in clip mode, which always spans the whole trimmed clip."))},
               {QStringLiteral("duration"), numberProp(QStringLiteral("Range length in seconds, max 3600. Required for timeline mode; in asset mode omit it for the whole file."))},
               {QStringLiteral("buckets"), propWithDefault(integerProp(QStringLiteral("How many peaks to return, 1..4096")), 400)}},
              clipRefProps())),
          true, false, true },
        { "detect_beats", "audio", "Find the tempo",
          "Detect beats and onsets over a range of the mixed timeline. BLOCKS until done (a few "
          "seconds for a long range) and returns bpm, confidence, beatsPerBar, firstDownbeat, "
          "beats (absolute timeline seconds) and onsets [{at, s}] where s is 0..1 strength. "
          "duration must be at least 4 seconds and at most 600. WARNING: bpm 0 means no trustworthy "
          "tempo was found — the beats array is then empty, but onsets are still valid and "
          "unit:\"onset\" works everywhere a grid is accepted. Re-running the same range returns "
          "the cached result with cached:true unless force is set. The result is transient: any "
          "edit that changes the mix drops it. Returns error conflict when the editor is already "
          "analysing. Long lists are trimmed to 2000 beats / 500 strongest onsets, flagged as "
          "truncated. Call set_beat_layers next to make these beats snap targets.",
          objectSchema({{QStringLiteral("start"), propWithDefault(numberProp(QStringLiteral("Range start in timeline seconds")), 0)},
                        {QStringLiteral("duration"), numberProp(QStringLiteral("Range length in seconds, 4..600"))},
                        {QStringLiteral("force"), boolProp(QStringLiteral("Re-analyse even when this exact range is already cached"))}},
                       {QStringLiteral("duration")}) },
        { "set_beat_layers", "audio", "Arm beat snapping",
          "Show or hide the detected beat grid and onset markers. SIDE EFFECT, and the reason to "
          "call it: only visible layers become snap targets, so with grid:true every later "
          "place_clip, move_clip and move_to_track magnets to the nearest beat within 150 ms, and "
          "so do the user's own drags. Turn it off to place something at an exact time instead. "
          "Onsets are thinned against beats so a dense track still leaves un-snapped positions. "
          "Returns the resulting flags plus snapTargets and snapEnabled — snapping does nothing "
          "when snapEnabled is false.",
          objectSchema({{QStringLiteral("grid"), propWithDefault(boolProp(QStringLiteral("Show the beat grid")), true)},
                        {QStringLiteral("onsets"), propWithDefault(boolProp(QStringLiteral("Show onset markers")), false)}}) },
        { "bookmark_beats", "audio", "Beats as markers",
          "Write the detected grid into the project as bookmarks, which persist with the project, "
          "show in the bookmark lane, and are snap targets in their own right — this is how a grid "
          "outlives the transient analysis. Skips any beat within 150 ms of an existing bookmark, "
          "since stacked markers make the magnet ambiguous rather than stronger. Requires "
          "detect_beats first. Returns {added}.",
          objectSchema({{QStringLiteral("unit"), beatUnitProp()},
                        {QStringLiteral("start"), propWithDefault(numberProp(QStringLiteral("Only mark from this timeline second on")), 0)},
                        {QStringLiteral("duration"), numberProp(QStringLiteral("Only mark this many seconds; omit for the rest of the analysed range"))},
                        {QStringLiteral("min_strength"), propWithDefault(numberProp(QStringLiteral("For unit:\"onset\" only — drop onsets weaker than this, 0..1")), 0.0)},
                        {QStringLiteral("label"), propWithDefault(stringProp(QStringLiteral("Label prefix; each mark gets its grid number appended")), QStringLiteral("Beat"))}}) },
        { "split_on_beats", "audio", "Cut to the beat",
          "Split one clip at every grid time strictly inside it — the beat-cut edit. Requires "
          "detect_beats first. Cuts run back to front so the clip id you passed stays valid "
          "throughout; it ends up naming the FIRST piece. Returns clips (all resulting ids in "
          "timeline order, including the original) and at (the times actually cut). One undo step "
          "however many cuts it makes. min_gap drops cuts that would leave a sliver.",
          objectSchema(mergeProps(
              {{QStringLiteral("unit"), beatUnitProp()},
               {QStringLiteral("min_strength"), propWithDefault(numberProp(QStringLiteral("For unit:\"onset\" only — drop onsets weaker than this, 0..1")), 0.0)},
               {QStringLiteral("min_gap"), propWithDefault(numberProp(QStringLiteral("Skip a cut that would leave a piece shorter than this many seconds")), 0.1)}},
              clipRefProps())) },
        { "snap_clips_to_beats", "audio", "Quantise to the grid",
          "Move clip starts onto the nearest grid time. Requires detect_beats first. Pass clips "
          "(a list of UUIDs) or track (every clip on that lane); clips wins when both are given. "
          "Clips further than max_distance from any grid time are left alone and reported in "
          "skipped. WARNING: the placed time can still differ from the beat — overlap resolution "
          "pushes a clip to the next free gap when overlap is off — so read the `to` values back "
          "rather than assuming. One undo step for the whole pass.",
          objectSchema(mergeProps(
              {{QStringLiteral("clips"), arrayProp(stringProp(QStringLiteral("Clip UUID")), QStringLiteral("Clip UUIDs to quantise"))},
               {QStringLiteral("unit"), beatUnitProp()},
               {QStringLiteral("min_strength"), propWithDefault(numberProp(QStringLiteral("For unit:\"onset\" only — drop onsets weaker than this, 0..1")), 0.0)},
               {QStringLiteral("max_distance"), propWithDefault(numberProp(QStringLiteral("Leave a clip alone when the nearest grid time is further than this many seconds")), 0.25)}},
              {{QStringLiteral("track"), integerProp(QStringLiteral("Quantise every clip on this track index; ignored when clips is given"))}})) },
        { "set_volume", "audio", "Clip loudness",
          "Set a clip's volume. 1 is unity, 0 is silent, 2 is the maximum the mixer allows. "
          "Without `at` this sets the clip's constant level; with `at` it writes a keyframe at that "
          "TIMELINE second, which is how you ramp or duck. WARNING: once a clip has more than one "
          "volume keyframe, a call without `at` retargets the key nearest the playhead rather than "
          "flattening the animation — read the keys back with list_keyframes({prop:\"volume\"}). "
          "Track-level volume does not exist; mute a whole lane with set_track instead.",
          objectSchema(mergeProps(
              {{QStringLiteral("value"), numberProp(QStringLiteral("Volume, 0..2 (1 = unity)"))},
               {QStringLiteral("at"), numberProp(QStringLiteral("Timeline seconds for a keyframe; omit to set the constant level"))}},
              clipRefProps()),
              {QStringLiteral("value")}) },

        // --- scene ---
        { "detect_scenes", "scene", "Find the shots in a clip",
          "Scan a video clip for shot boundaries and rank each shot by how much is happening. "
          "ASYNC: returns {started:true} immediately — poll inspect({detail:true}).sceneDetect "
          "{active, progress, status} until active is false, then read the result with "
          "list_scenes or describe_clip. Returns {cached:true} instead when this clip and these "
          "settings were already scanned, in which case the result is ready at once.\n"
          "Unlike detect_beats, the analysis does NOT go stale on edits: it describes the source "
          "file, is cached against that file's timestamp, and survives edits, undo and reload. "
          "Re-trimming the clip does change the scanned range, so that scans afresh.\n"
          "with_objects needs the object-model addon; without it the op fails with a message "
          "naming what to install.",
          objectSchema(mergeProps(
              {{QStringLiteral("threshold"), propWithDefault(numberProp(QStringLiteral("Sensitivity, 4..100 on a mean-HSV-delta scale. Lower finds more cuts. The engine still falls back to an adaptive threshold when this one finds implausibly little on flat or graded footage")), 27.0)},
               {QStringLiteral("min_scene"), propWithDefault(numberProp(QStringLiteral("Shortest shot to emit, in seconds")), 0.5)},
               {QStringLiteral("with_objects"), propWithDefault(boolProp(QStringLiteral("Also label each shot with the objects in it. Slower; needs the object-model addon")), false)}},
              clipRefProps())) },
        { "list_scenes", "scene", "Read shots; sort:score for a highlight reel",
          "The shots found by detect_scenes, as {scenes:[{index, start, end, duration, "
          "timeline_start, timeline_end, motion, loudness, objects, score, labels}]}. start/end "
          "are seconds into the SOURCE file; timeline_start/timeline_end are the same moments on "
          "the timeline, already mapped through the clip's trim, speed and reverse — use those to "
          "seek or cut. score is 0..1 and combines motion, loudness and objects.\n"
          "Requires detect_scenes first; fails not_found otherwise.",
          objectSchema({{QStringLiteral("label"), stringProp(QStringLiteral("Keep only shots containing this object class (needs with_objects)"))},
                        {QStringLiteral("min_score"), propWithDefault(numberProp(QStringLiteral("Drop shots scoring below this, 0..1")), 0.0)},
                        {QStringLiteral("sort"), propWithDefault(enumProp(QStringLiteral("time = in order; score = most active first"), {QStringLiteral("time"), QStringLiteral("score")}), QStringLiteral("time"))},
                        {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Return at most this many shots")), 200)}}),
          true, false, true },
        { "describe_clip", "scene", "What is this footage",
          "One-call summary of the analysed clip, for forming an impression without walking every "
          "shot. Returns {clip, duration, scenes (count), cuts, shortest, longest, mean_score, "
          "detector, adaptive (whether the fallback threshold was used), objects_scanned, "
          "labels:[{name, scenes, seconds}] and top:[the highest-scoring shots]}. labels is empty "
          "unless detect_scenes ran with_objects.\n"
          "Requires detect_scenes first; fails not_found otherwise.",
          objectSchema({{QStringLiteral("top"), propWithDefault(integerProp(QStringLiteral("How many of the highest-scoring shots to include")), 5)}}),
          true, false, true },
        { "find_scenes", "scene", "Search shots across the timeline",
          "Search every clip that has a cached analysis, not just the one last scanned — the op "
          "for gathering material: \"every shot with a person in it, best first\". Returns "
          "{scenes:[{clip, index, start, end, timeline_start, timeline_end, score, labels}]} "
          "sorted by score descending.\n"
          "Only clips already scanned by detect_scenes are searched; unscanned clips are listed "
          "in unscanned:[clip ids] so you know what you are missing.",
          objectSchema({{QStringLiteral("label"), stringProp(QStringLiteral("Keep only shots containing this object class"))},
                        {QStringLiteral("min_score"), propWithDefault(numberProp(QStringLiteral("Drop shots scoring below this, 0..1")), 0.0)},
                        {QStringLiteral("track"), integerProp(QStringLiteral("Restrict to clips on this track index"))},
                        {QStringLiteral("limit"), propWithDefault(integerProp(QStringLiteral("Return at most this many shots")), 50)}}),
          true, false, true },
        { "split_on_scenes", "scene", "Cut at every shot boundary",
          "Split one clip at every detected shot boundary strictly inside it. Requires "
          "detect_scenes on that clip first. Cuts run back to front so the clip id you passed "
          "stays valid throughout; it ends up naming the FIRST piece. Returns clips (all "
          "resulting ids in timeline order, including the original) and at (the timeline times "
          "actually cut). One undo step however many cuts it makes. min_gap drops cuts that would "
          "leave a sliver.",
          objectSchema(mergeProps(
              {{QStringLiteral("min_gap"), propWithDefault(numberProp(QStringLiteral("Skip a cut that would leave a piece shorter than this many seconds")), 0.1)},
               {QStringLiteral("min_score"), propWithDefault(numberProp(QStringLiteral("Only cut at boundaries opening a shot that scores at least this")), 0.0)}},
              clipRefProps())) },
        { "bookmark_scenes", "scene", "Mark the shots",
          "Write the detected shot boundaries into the project as bookmarks, which survive "
          "re-analysis and show on the timeline ruler. Requires detect_scenes first. Returns "
          "{added, at:[timeline seconds]}. One undo step.",
          objectSchema(mergeProps(
              {{QStringLiteral("min_score"), propWithDefault(numberProp(QStringLiteral("Only mark boundaries opening a shot that scores at least this")), 0.0)},
               {QStringLiteral("label"), stringProp(QStringLiteral("Only mark shots containing this object class"))},
               {QStringLiteral("prefix"), propWithDefault(stringProp(QStringLiteral("Bookmark label prefix; the shot number is appended")), QStringLiteral("Scene"))}},
              clipRefProps())) },

        { "set_ui_preferences", "ui", "Editor flags",
          "Set editor preferences; only supplied keys change. Note followSystem only acts when TRUE "
          "(it clears the dark-mode override); passing false does nothing — use set_theme to pin a "
          "theme. autoKey matters for set_transform: when on, transform writes become keyframes at the "
          "playhead. Returns the resulting flags plus theme {overridden, dark}.",
          objectSchema({{QStringLiteral("autoKey"), boolProp(QStringLiteral("Auto-keyframe: transform edits create keyframes at the playhead"))},
                        {QStringLiteral("mediaGrid"), boolProp(QStringLiteral("Media bin grid mode"))},
                        {QStringLiteral("reopenLastProject"), boolProp(QStringLiteral("Reopen last project on launch"))},
                        {QStringLiteral("followSystem"), boolProp(QStringLiteral("true clears the theme override so the OS theme wins; false is a no-op"))}}) },

        { "set_ripple", "timeline", "Ripple delete and trim",
          "Project setting. When on, deleting or shortening a clip pulls later clips on that track "
          "left to close the hole. Off (default): every delete leaves a gap. Not undoable.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Ripple later clips into holes"))}},
                       {QStringLiteral("enabled")}) },
        { "close_gap", "timeline", "Close one hole on a track",
          "Shift every clip on `track` that starts at or after `at` left by the width of the gap "
          "whose left edge is `at` (typically the end of the preceding clip). Linked A/V partners "
          "follow. With overlap off this is how you delete without leaving a hole, or how you put an "
          "intro at the front: place then close_gap behind. When moving several clips toward zero with "
          "overlap off, sequence back-to-front or each move is pushed into the clip ahead.",
          objectSchema({{QStringLiteral("track"), integerProp(QStringLiteral("Track index"))},
                        {QStringLiteral("at"), numberProp(QStringLiteral("Left edge of the hole, timeline seconds"))}},
                       {QStringLiteral("track"), QStringLiteral("at")}) },
        { "set_snap", "timeline", "Toggle snapping",
          "Editor snap-to-clips/beats/bookmarks. Not undoable.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Snap on"))}},
                       {QStringLiteral("enabled")}) },
        { "set_guides", "ui", "Preview guides",
          "Show composition guides on the preview and/or pick the guide kind. Not undoable.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Show guides"))},
                        {QStringLiteral("type"),
                         enumProp(QStringLiteral("Guide kind"),
                                  {QStringLiteral("thirds"), QStringLiteral("center"),
                                   QStringLiteral("golden"), QStringLiteral("grid")})}}) },
        { "set_loop_work_area", "playback", "Loop the In/Out range",
          "When on, playback loops over the work area. Not undoable.",
          objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("Loop the work area"))}},
                       {QStringLiteral("enabled")}) },
        { "select_clips", "timeline", "Select several clips",
          "Replace the selection with the given clips (UUIDs). Linked A/V partners are included.",
          objectSchema({{QStringLiteral("clips"),
                         arrayProp(stringProp(QStringLiteral("Clip UUID")),
                                   QStringLiteral("Clip UUIDs to select"))}},
                       {QStringLiteral("clips")}) },
        { "copy_clip_effects", "effects", "Copy a clip's effect stack",
          "Copy the clip's video and audio effects to the effect clipboard.",
          objectSchema(clipRefProps()) },
        { "paste_clip_effects", "effects", "Paste an effect stack",
          "Paste the effect clipboard onto a clip. Fails bad_args when the clipboard is empty.",
          objectSchema(clipRefProps()) },
        { "set_effect_string_param", "effects", "File/string effect param",
          "Set a file- or string-typed effect parameter (e.g. face_swap's sourceImage). Value is an "
          "absolute path or file:// URL. Not validated — confirm with inspect({clips:true,detail:true}).",
          objectSchema(mergeProps({{QStringLiteral("index"), effectIndexProp()},
                                   {QStringLiteral("key"), stringProp(QStringLiteral("Parameter key from list_effects"))},
                                   {QStringLiteral("value"), stringProp(QStringLiteral("Absolute path, file:// URL, or string value"))}},
                                  clipRefProps()),
                       {QStringLiteral("index"), QStringLiteral("key"), QStringLiteral("value")}) },
        { "apply_text_preset", "text", "Restyle an existing title",
          "Apply a builtin text style pack from list_text_presets to an existing text or subtitle clip.",
          objectSchema(mergeProps({{QStringLiteral("preset"), stringProp(QStringLiteral("Preset id from list_text_presets"))}},
                                  clipRefProps()),
                       {QStringLiteral("preset")}) },
        { "list_user_text_presets", "text", "User-saved text styles",
          "Returns {presets:[{id, label}]}. Apply with apply_user_text_preset.",
          objectSchema({}), true, false, true },
        { "apply_user_text_preset", "text", "Apply a saved text style",
          "Apply a user-saved text style from list_user_text_presets.",
          objectSchema(mergeProps({{QStringLiteral("preset"), stringProp(QStringLiteral("Preset id from list_user_text_presets"))}},
                                  clipRefProps()),
                       {QStringLiteral("preset")}) },
        { "save_text_preset", "text", "Save this clip's text style",
          "Save the clip's current text style as a user preset. Returns {id, label}.",
          objectSchema(mergeProps({{QStringLiteral("label"), stringProp(QStringLiteral("Preset name"))}},
                                  clipRefProps()),
                       {QStringLiteral("label")}) },
        { "list_user_effect_presets", "effects", "User-saved effect stacks",
          "Returns {presets:[{id, label, effectCount, audioEffectCount, labels}]}.",
          objectSchema({}), true, false, true },
        { "apply_effect_preset", "effects", "Apply a saved effect stack",
          "Apply a user-saved effect stack from list_user_effect_presets onto a clip.",
          objectSchema(mergeProps({{QStringLiteral("preset"), stringProp(QStringLiteral("Preset id from list_user_effect_presets"))}},
                                  clipRefProps()),
                       {QStringLiteral("preset")}) },
        { "save_effect_preset", "effects", "Save this clip's effects",
          "Save the clip's current video+audio effect stack as a user preset. Returns {id, label}.",
          objectSchema(mergeProps({{QStringLiteral("label"), stringProp(QStringLiteral("Preset name"))}},
                                  clipRefProps()),
                       {QStringLiteral("label")}) },
        { "list_export_presets", "project", "Named export sizes",
          "Returns {presets:[{id, label}]} — 1080p, YouTube, vertical, and the rest. Use id with "
          "export_with_preset.",
          objectSchema({}), true, false, true },
        { "export_with_preset", "project", "Export using a named size",
          "Start an async encode with a scale preset from list_export_presets. Same polling as "
          "export_video.",
          objectSchema({{QStringLiteral("path"), stringProp(QStringLiteral("Absolute output path"))},
                        {QStringLiteral("preset"), stringProp(QStringLiteral("Scale preset id from list_export_presets"))}},
                       {QStringLiteral("path"), QStringLiteral("preset")}) },
        { "stabilize_clip", "canvas", "Stabilise shaky footage",
          "Start a two-pass stabilise on a video clip. Async: poll inspect({clips:true,detail:true}) "
          "for that clip's stabilizing/stabilizeProgress/stabilizeStatus.",
          objectSchema(mergeProps(
              {{QStringLiteral("smoothing"), integerProp(QStringLiteral("Smoothing window, typical 10–30"))},
               {QStringLiteral("tripod"), boolProp(QStringLiteral("Lock camera translation"))},
               {QStringLiteral("mode"), enumProp(QStringLiteral("bake writes a proxy; keyframes write transform keys"),
                                                 {QStringLiteral("bake"), QStringLiteral("keyframes")})}},
              clipRefProps())) },
        { "cancel_stabilize", "canvas", "Stop stabilise job",
          "Cancel an in-flight stabilize_clip. Returns ok even when nothing was running.",
          objectSchema(clipRefProps()) },
        { "remove_stabilize", "canvas", "Remove stabilisation",
          "Clear the baked proxy / keyframe rest pose from a clip.",
          objectSchema(clipRefProps()), false, true },
        { "list_addons", "ai", "What add-ons exist",
          "Returns {addons:[{id, name, kind, version, state, installed}]}. Use id with install_addon.",
          objectSchema({}), true, false, true },
        { "install_addon", "ai", "Install a model or pack",
          "Start downloading and installing an add-on from list_addons. Async: poll list_addons for "
          "state. Not undoable.",
          objectSchema({{QStringLiteral("id"), stringProp(QStringLiteral("Add-on id from list_addons"))}},
                       {QStringLiteral("id")}) },
        { "cancel_addon_install", "ai", "Stop an add-on install",
          "Cancel an in-flight install_addon. Not undoable.",
          objectSchema({{QStringLiteral("id"), stringProp(QStringLiteral("Add-on id"))}},
                       {QStringLiteral("id")}) },
        { "set_acceleration", "ai", "Pick ONNX Runtime",
          "Set the ONNX Runtime used by AI features. A change may require an app restart. Not undoable.",
          objectSchema({{QStringLiteral("variant"), stringProp(QStringLiteral("Acceleration id (auto, cpu, cuda, …)"))}},
                       {QStringLiteral("variant")}) },
        { "list_face_track", "ai", "Read face anchors",
          "The per-frame face-track sidecar written by detect_faces. Returns {fps, n, frames:[{t, "
          "faces:[{cx, cy, rx, ry, valid}]}]}. t is source seconds. Long tracks are subsampled.",
          objectSchema(clipRefProps()), true, false, true },
        { "auto_reframe", "canvas", "Keep a face in frame",
          "Write x/y/w/h transform keyframes so the subject stays in frame at the requested aspect. "
          "Does not crop the project canvas, so a 16:9 timeline can still export a 9:16 deliverable. "
          "mode face follows list_face_track (run detect_faces first); center is a static centre crop; "
          "motion is face with heavier smoothing.",
          objectSchema(mergeProps(
              {{QStringLiteral("aspect"), numberProp(QStringLiteral("Target width/height, e.g. 0.5625 for 9:16"))},
               {QStringLiteral("width"), integerProp(QStringLiteral("Target width pixels"))},
               {QStringLiteral("height"), integerProp(QStringLiteral("Target height pixels"))},
               {QStringLiteral("mode"),
                propWithDefault(enumProp(QStringLiteral("How to pick the crop"),
                                         {QStringLiteral("face"), QStringLiteral("center"),
                                          QStringLiteral("motion")}),
                                QStringLiteral("face"))}},
              clipRefProps())) },
        { "set_scene_threshold", "scene", "Tune shot detection",
          "Persist the default scene-detect sensitivity (4–100, lower finds more cuts).",
          objectSchema({{QStringLiteral("threshold"), numberProp(QStringLiteral("Sensitivity 4..100"))}},
                       {QStringLiteral("threshold")}) },
        { "clear_scenes", "scene", "Drop the shot index",
          "Clear the in-memory scene analysis.",
          objectSchema({}), false, true },
        { "cancel_scene_detection", "scene", "Stop a scene scan",
          "Cancel an in-flight detect_scenes. Returns ok even when nothing was running.",
          objectSchema({}) },
        { "clear_beat_analysis", "audio", "Drop the beat grid",
          "Clear the transient beat analysis so the next detect_beats is not cached.",
          objectSchema({}), false, true },
        { "detect_silence", "audio", "Find dead air",
          "Find ranges where speech-band energy stays below threshold. BLOCKS. Pass clip for that "
          "clip, or start+duration for the mixed timeline. Returns {ranges:[{start,end}], threshold, "
          "source} in timeline seconds.",
          objectSchema(mergeProps(
              {{QStringLiteral("start"), numberProp(QStringLiteral("Timeline start seconds (timeline mode)"))},
               {QStringLiteral("duration"), numberProp(QStringLiteral("Range length seconds (timeline mode)"))},
               {QStringLiteral("threshold"), propWithDefault(numberProp(QStringLiteral("Speech-band amplitude 0..1 below which a bucket is silence")), 0.02)},
               {QStringLiteral("min_duration"), propWithDefault(numberProp(QStringLiteral("Ignore silences shorter than this many seconds")), 0.35)},
               {QStringLiteral("padding"), propWithDefault(numberProp(QStringLiteral("Seconds of room tone to keep on each side of speech")), 0.08)}},
              clipRefProps())),
          true, false, true },
        { "remove_silence", "audio", "Cut dead air in one step",
          "Split, delete, and close-gap internally so silences disappear. One undo step. Pass clip "
          "or track. Returns {removed:[{start,end}], clips:[surviving ids]}. Run generate_subtitles "
          "AFTER this.",
          objectSchema(mergeProps(
              {{QStringLiteral("threshold"), propWithDefault(numberProp(QStringLiteral("Same as detect_silence")), 0.02)},
               {QStringLiteral("min_duration"), propWithDefault(numberProp(QStringLiteral("Same as detect_silence")), 0.35)},
               {QStringLiteral("padding"), propWithDefault(numberProp(QStringLiteral("Same as detect_silence")), 0.08)}},
              clipRefProps())) },
        { "analyze_loudness", "audio", "Measure LUFS and true peak",
          "Integrated loudness (EBU R128 / BS.1770) plus a 4×-interpolated true-peak estimate. Pass "
          "clip or start+duration. BLOCKS. Returns {lufs, true_peak_db, duration}.",
          objectSchema(mergeProps(
              {{QStringLiteral("start"), numberProp(QStringLiteral("Timeline start seconds (timeline mode)"))},
               {QStringLiteral("duration"), numberProp(QStringLiteral("Range length seconds (timeline mode)"))}},
              clipRefProps())),
          true, false, true },
        { "normalize_volume", "audio", "Match a LUFS target",
          "Measure the clip, then set its scalar volume so integrated loudness matches target_lufs. "
          "Clamped to 0..2.",
          objectSchema(mergeProps(
              {{QStringLiteral("target_lufs"), propWithDefault(numberProp(QStringLiteral("Target integrated LUFS")), -16.0)}},
              clipRefProps())) },
        { "duck_under", "audio", "Lower music under speech",
          "Write volume keyframes on this (music) clip that dip whenever speech is present on "
          "over_track or over_clips. amount is the volume multiplier during speech. One undo step.",
          objectSchema(mergeProps(
              {{QStringLiteral("over_track"), integerProp(QStringLiteral("Track whose non-silent ranges drive the duck"))},
               {QStringLiteral("over_clips"), arrayProp(stringProp(QStringLiteral("Clip UUID")), QStringLiteral("Speech clips; wins over over_track"))},
               {QStringLiteral("amount"), propWithDefault(numberProp(QStringLiteral("Multiplier during speech, 0..1")), 0.3)},
               {QStringLiteral("attack"), propWithDefault(numberProp(QStringLiteral("Seconds to ramp down before speech")), 0.12)},
               {QStringLiteral("release"), propWithDefault(numberProp(QStringLiteral("Seconds to ramp back after speech")), 0.25)}},
              clipRefProps())) },
        { "setup_multicam", "multicam", "Open a multi-camera session",
          "With two or more video clips selected, start punching those as angles. Otherwise place "
          "every video asset onto its own track and start a session. Returns {active, angles}.",
          objectSchema({}) },
        { "switch_angle", "multicam", "Cut to another camera",
          "Punch `angle` (0-based) at the playhead. Session-only until save_multicam_*; not undoable.",
          objectSchema({{QStringLiteral("angle"), integerProp(QStringLiteral("0-based angle index"))}},
                       {QStringLiteral("angle")}) },
        { "end_multicam", "multicam", "Abandon the session",
          "Close the multicam session without writing cuts into the project. Not undoable.",
          objectSchema({}) },
        { "save_multicam_separate", "multicam", "Keep each camera on its track",
          "Write the session as separate tracks and end it.",
          objectSchema({}) },
        { "save_multicam_combined", "multicam", "Bake a single program track",
          "Write the session as one combined program track and end it.",
          objectSchema({}) }
