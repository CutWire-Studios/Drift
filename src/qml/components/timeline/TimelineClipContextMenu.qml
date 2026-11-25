import QtQuick
import Drift
import "../suggestions/ClipSuggestions.js" as ClipSuggestions

// Right-click menu for one timeline clip. The owning clip is passed in; the loader unloads it on close.
ThemedContextMenu {
    id: clipContextMenu

    required property var owner
    signal unloadRequested()

    property bool canPasteEffects: false
    property bool canPasteAttributes: false
    property bool canMergeTrackSubtitles: false
    property bool canRemoveSilence: false
    property bool canSyncByAudio: false
    property bool canReplaceMedia: false
    property bool canDeleteWithEffects: false
    property bool hasPrevious: false
    property bool hasNext: false
    property bool canPasteClips: false
    property var suggestions: []
    implicitWidth: 240

    readonly property string kind: owner.clipData.kind
    readonly property bool showSpeed: kind === "video" || kind === "audio"
    readonly property bool showVolume: (kind === "video" || kind === "audio") && !owner.clipData.sequenceId
    readonly property bool showFreeze: kind === "video"
            && EditorState.playheadSeconds > owner.clipData.start
            && EditorState.playheadSeconds < owner.clipData.start + owner.clipData.duration
    readonly property bool showAudioMenu: (kind === "video" || kind === "audio")
            && !owner.clipData.sequenceId

    function goTarget(extra) {
        return Object.assign(owner.selected ? {} : { track: owner.trackIndex, clip: owner.clipIndex }, extra)
    }

    function openProperties(tab) {
        if (!owner.selected)
            EditorState.selectClip(owner.trackIndex, owner.clipIndex)
        if (!ShellBridge.phoneShell)
            ShellBridge.goDeeper({ panel: "properties", tab: tab })
        else if (typeof owner.panel.openClipProperties === "function")
            owner.panel.openClipProperties()
    }
    onAboutToShow: {
        canRemoveSilence = EditorState.audioAlign.canRemoveSilence(owner.trackIndex, owner.clipIndex)
        canSyncByAudio = EditorState.audioAlign.canSyncByAudio()
        canReplaceMedia = typeof owner.panel.requestReplaceClip === "function"
                && EditorState.replaceCandidatesForClip(owner.trackIndex, owner.clipIndex).length > 0
        canPasteEffects = EditorState.clipboardHasEffects()
        // Over what the delete acts on (the selection, linked partners included), masks included.
        canDeleteWithEffects = EditorState.selectionHasPinnedAdjustments()
        canPasteAttributes = EditorState.canPasteAttributes()
        hasPrevious = EditorState.hasClipNeighbour(owner.trackIndex, owner.clipIndex, false)
        hasNext = EditorState.hasClipNeighbour(owner.trackIndex, owner.clipIndex, true)
        canMergeTrackSubtitles = owner.trackType === "subtitle"
                && EditorState.canMergeAllSubtitlesOnTrack(owner.trackIndex)
        canPasteClips = EditorState.canPasteClips()
        suggestions = ClipSuggestions.suggest({
            editor: EditorState, addons: Addons, tracks: owner.panel.tracks,
            reframe: typeof owner.panel.requestAutoReframe === "function"
                     ? owner.panel.requestAutoReframe : null
        }, {
            clipData: owner.clipData, trackType: owner.trackType, trackIndex: owner.trackIndex,
            clipIndex: owner.clipIndex, selected: owner.selected
        }, 2)
        rebuildSubMenus()
    }

    property var subMenus: []

    function rebuildSubMenus() {
        for (let i = 0; i < subMenus.length; i++)
            removeMenu(subMenus[i])
        subMenus = []
        const specs = [
            { anchor: effectsAnchor, component: effectsMenuComponent, show: true },
            { anchor: audioAnchor, component: audioMenuComponent, show: showAudioMenu },
            { anchor: transformAnchor, component: transformMenuComponent,
              show: kind === "video" || kind === "image" },
            { anchor: moreAnchor, component: moreMenuComponent, show: true }
        ]
        for (let s = 0; s < specs.length; s++) {
            if (!specs[s].show)
                continue
            const sub = specs[s].component.createObject(null)
            for (let i = 0; i < count; i++) {
                if (itemAt(i) === specs[s].anchor) {
                    insertMenu(i + 1, sub)
                    break
                }
            }
            subMenus.push(sub)
        }
    }

    function rebuildMoveToTrackMenu(more) {
        const targets = EditorState.compatibleTracksForClip(owner.trackIndex, owner.clipIndex)
        if (targets.length === 0)
            return
        const sub = moveToTrackMenuComponent.createObject(null)
        for (let i = 0; i < targets.length; i++) {
            const label = targets[i].name || owner.panel.trackNameAt(targets[i].index)
            sub.addItem(moveToTrackItemComponent.createObject(null,
                { text: label, targetTrack: targets[i].index }))
        }
        more.addMenu(sub)
    }

    Component {
        id: moveToTrackMenuComponent
        ThemedContextMenu {
            title: qsTr("Move to track")
        }
    }

    Component {
        id: moveToTrackItemComponent
        ThemedMenuItem {
            required property int targetTrack
            onTriggered: EditorState.moveClipToTrack(clipContextMenu.owner.trackIndex,
                                                     clipContextMenu.owner.clipIndex, targetTrack,
                                                     clipContextMenu.owner.clipData.start || 0)
        }
    }
    // Deferred: onClosed runs inside the Popup's own close path, and dropping the
    // Loader's item there would destroy an object still unwinding.
    onClosed: unloadRequested()


    Item {
        id: quickRow
        implicitWidth: quickButtons.implicitWidth + Theme.spacingSm * 2
        implicitHeight: Theme.iconButtonSize + Theme.spacingSm
        width: ListView.view ? ListView.view.width : implicitWidth

        Row {
            id: quickButtons
            anchors.centerIn: parent
            spacing: Theme.spacingXs

            IconButton {
                glyph: Theme.icons.scissors
                tooltip: qsTr("Split at current time")
                tooltipActionId: "splitSelection"
                enabled: EditorState.playheadSeconds > clipContextMenu.owner.clipData.start
                         && EditorState.playheadSeconds < clipContextMenu.owner.clipData.start
                                                          + clipContextMenu.owner.clipData.duration
                onClicked: {
                    clipContextMenu.close()
                    EditorState.splitClipAt(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex,
                                            EditorState.playheadSeconds)
                }
            }
            IconButton {
                glyph: Theme.icons.scissors
                tooltip: qsTr("Cut")
                tooltipActionId: "cut"
                onClicked: {
                    clipContextMenu.close()
                    EditorState.cutSelection()
                }
            }
            IconButton {
                glyph: Theme.icons.copy
                tooltip: qsTr("Copy")
                tooltipActionId: "copy"
                onClicked: {
                    clipContextMenu.close()
                    EditorState.copySelection()
                }
            }
            IconButton {
                glyph: Theme.icons.clipboardPaste
                tooltip: qsTr("Paste at current time")
                tooltipActionId: "paste"
                enabled: clipContextMenu.canPasteClips
                onClicked: {
                    clipContextMenu.close()
                    EditorState.pasteAtPlayhead()
                }
            }
            IconButton {
                glyph: Theme.icons.copyPlus
                tooltip: qsTr("Duplicate")
                tooltipActionId: "duplicate"
                onClicked: {
                    clipContextMenu.close()
                    EditorState.duplicateSelectedClip()
                }
            }
            IconButton {
                readonly property bool off: clipContextMenu.owner.clipData.enabled === false
                glyph: off ? Theme.icons.eye : Theme.icons.eyeOff
                tooltip: off ? qsTr("Enable clip") : qsTr("Disable clip")
                tooltipActionId: "toggleClipEnabled"
                onClicked: {
                    clipContextMenu.close()
                    EditorState.setClipEnabled(clipContextMenu.owner.trackIndex,
                                               clipContextMenu.owner.clipIndex, off)
                }
            }
            IconButton {
                glyph: Theme.icons.trash
                tooltip: qsTr("Delete")
                tooltipActionId: "delete"
                onClicked: {
                    clipContextMenu.close()
                    EditorState.deleteSelectedClip()
                }
            }
        }
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: clipContextMenu.suggestions.length > 0 ? clipContextMenu.suggestions[0].label : ""
        icon.name: Theme.icons.sparkles
        visible: clipContextMenu.suggestions.length > 0
        onTriggered: clipContextMenu.suggestions[0].run()
    }
    ThemedMenuItem {
        text: clipContextMenu.suggestions.length > 1 ? clipContextMenu.suggestions[1].label : ""
        icon.name: Theme.icons.sparkles
        visible: clipContextMenu.suggestions.length > 1
        onTriggered: clipContextMenu.suggestions[1].run()
    }
    ThemedMenuSeparator { visible: clipContextMenu.suggestions.length > 0 }

    ThemedMenuItem {
        text: qsTr("Detach from %1").arg(clipContextMenu.owner.clipData.generatedBy || "")
        icon.name: Theme.icons.unlink
        visible: (clipContextMenu.owner.clipData.generatedBy || "") !== ""
        height: visible ? implicitHeight : 0
        onTriggered: EditorState.projectFile.folder.detachClip(clipContextMenu.owner.clipData.id)
    }
    ThemedMenuItem {
        text: qsTr("Properties")
        icon.name: Theme.icons.sliders
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (typeof owner.panel.openClipProperties === "function")
                owner.panel.openClipProperties()
        }
    }
    ThemedMenuItem {
        // Touch route into multi-clip selection; the panel owns the mode and
        // the desktop panel does not declare the property at all.
        text: qsTr("Select multiple")
        icon.name: Theme.icons.check
        visible: owner.panel.multiSelectActive === false
        onTriggered: owner.panel.multiSelectActive = true
    }
    ThemedMenuItem {
        text: qsTr("Speed and reverse…")
        icon.name: Theme.icons.gauge
        visible: clipContextMenu.showSpeed
        onTriggered: clipContextMenu.openProperties("speed")
    }
    ThemedMenuItem {
        text: qsTr("Volume…")
        icon.name: Theme.icons.volumeHigh
        visible: clipContextMenu.showVolume
        onTriggered: clipContextMenu.openProperties("audio")
    }
    ThemedMenuItem {
        text: qsTr("Freeze frame")
        icon.name: Theme.icons.snowflake
        visible: clipContextMenu.showFreeze
        onTriggered: EditorState.freezeFrameAtPlayhead()
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        onTriggered: owner.panel.requestRenameClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Replace with…")
        icon.name: Theme.icons.repeat
        visible: clipContextMenu.canReplaceMedia
        onTriggered: owner.panel.requestReplaceClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Detach audio")
        shortcutId: "separateAudio"
        icon.name: Theme.icons.audioLines
        // CapCut: only offer extract when the clip still has embedded audio.
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.separateAudioFromClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Detach all audio tracks")
        icon.name: Theme.icons.audioLines
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
                 && EditorState.clipAudioStreamCount(owner.trackIndex, owner.clipIndex) > 1
        onTriggered: EditorState.separateAllAudioTracks(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Unlink video and audio")
        shortcutId: "unlink"
        icon.name: Theme.icons.unlink
        visible: !!owner.clipData.linked && EditorState.unlinkAvailable
        onTriggered: EditorState.unlinkSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Link")
        shortcutId: "link"
        icon.name: Theme.icons.link
        // Needs two or more unlinked clips selected (Shift+click to add).
        visible: EditorState.linkAvailable
        onTriggered: EditorState.linkSelectedClips()
    }
    ThemedMenuSeparator { }
    ThemedMenuItem { id: effectsAnchor; visible: false }
    ThemedMenuItem { id: audioAnchor; visible: false }
    ThemedMenuItem { id: transformAnchor; visible: false }
    ThemedMenuItem { id: moreAnchor; visible: false }
    ThemedMenuSeparator { visible: owner.hasAnyEffects || clipContextMenu.canDeleteWithEffects }
    ThemedMenuItem {
        text: qsTr("Remove all effects")
        icon.name: Theme.icons.trash
        visible: owner.hasAnyEffects
        onTriggered: EditorState.removeAllEffects(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        // Plain Delete leaves a graded clip's effects on the timeline, unlinked; this takes them
        // (and its masks) with it, in the same undo step.
        text: qsTr("Delete with effects")
        shortcutId: "deleteWithEffects"
        icon.name: Theme.icons.trash
        visible: clipContextMenu.canDeleteWithEffects
        onTriggered: EditorState.deleteSelectedClipWithEffects()
    }

    Component {
        id: effectsMenuComponent
        ThemedContextMenu {
            title: qsTr("Effects")
            ThemedMenuItem {
                text: qsTr("Copy effects")
                shortcutId: "copyEffects"
                icon.name: Theme.icons.wand
                visible: clipContextMenu.owner.hasAnyEffects
                onTriggered: EditorState.copyClipEffectsToClipboard(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Paste effects")
                shortcutId: "pasteEffects"
                icon.name: Theme.icons.clipboardPaste
                visible: !clipContextMenu.owner.isTransformClip
                enabled: clipContextMenu.canPasteEffects
                onTriggered: EditorState.pasteEffectsFromClipboard(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Paste attributes…")
                shortcutId: "pasteAttributes"
                icon.name: Theme.icons.clipboardPaste
                enabled: clipContextMenu.canPasteAttributes
                onTriggered: EditorState.requestPasteAttributes()
            }
            ThemedMenuItem {
                text: qsTr("Save effects as preset…")
                icon.name: Theme.icons.save
                visible: clipContextMenu.owner.hasAnyEffects
                onTriggered: clipContextMenu.owner.panel.requestSaveEffectPreset(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuSeparator { }
            WorkspaceMenuItem {
                text: qsTr("Color grade…")
                icon.name: Theme.icons.blend
                visible: clipContextMenu.owner.clipData.kind !== "audio" && clipContextMenu.owner.clipData.kind !== "subtitle"
                         && !ShellBridge.phoneShell
                target: clipContextMenu.goTarget({ panel: "color" })
            }
            ThemedMenuItem {
                text: qsTr("Blur area")
                icon.name: Theme.icons.eyeOff
                visible: clipContextMenu.owner.trackType === "video" && !clipContextMenu.owner.isTransformClip
                onTriggered: EditorState.addEffect(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex,
                                                   "builtin.effects.privacy_blur")
            }
            WorkspaceMenuItem {
                text: qsTr("Edit keyframes…")
                icon.name: Theme.icons.diamondPlus
                visible: !ShellBridge.phoneShell
                target: clipContextMenu.goTarget({ panel: "keyframes" })
            }
            WorkspaceMenuItem {
                text: qsTr("Mask & track…")
                icon.name: Theme.icons.mask
                visible: clipContextMenu.owner.trackType === "video" && !ShellBridge.phoneShell
                target: clipContextMenu.goTarget({ panel: "masking" })
            }
        }
    }

    Component {
        id: audioMenuComponent
        ThemedContextMenu {
            title: qsTr("Audio")
            ThemedMenuItem {
                text: qsTr("Remove silence")
                icon.name: Theme.icons.audioLines
                visible: clipContextMenu.canRemoveSilence
                enabled: !EditorState.audioAlign.running
                onTriggered: EditorState.audioAlign.removeSilence(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Auto duck…")
                icon.name: Theme.icons.audioLines
                visible: EditorState.audioAlign.canDuck(clipContextMenu.owner.trackIndex,
                                                        clipContextMenu.owner.clipIndex)
                enabled: !EditorState.audioAlign.running
                onTriggered: {
                    if (ShellBridge.phoneShell) {
                        if (!clipContextMenu.owner.selected)
                            EditorState.selectClip(clipContextMenu.owner.trackIndex,
                                                   clipContextMenu.owner.clipIndex)
                        ShellBridge.navigate("panel:audioMixer#audio-ducking")
                    } else {
                        ShellBridge.goDeeper(clipContextMenu.goTarget({ panel: "audioMixer",
                                                                         anchor: "audio-ducking" }))
                    }
                }
            }
            ThemedMenuItem {
                text: qsTr("Sync by audio")
                icon.name: Theme.icons.audioLines
                visible: clipContextMenu.canSyncByAudio
                enabled: !EditorState.audioAlign.running
                onTriggered: EditorState.audioAlign.syncByAudio()
            }
            ThemedMenuItem {
                text: qsTr("Generate captions")
                icon.name: Theme.icons.captions
                visible: Addons.hasKind("whisper-model")
                enabled: !EditorState.subtitleGenerating
                onTriggered: {
                    if (!clipContextMenu.owner.selected)
                        EditorState.selectClip(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                    EditorState.generateSubtitlesForSelection()
                }
            }
            ThemedMenuItem {
                text: qsTr("Add beat markers: every beat")
                icon.name: Theme.icons.music
                onTriggered: EditorState.addBeatMarkersForSelection(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, 1)
            }
            ThemedMenuItem {
                text: qsTr("Add beat markers: every 2 beats")
                icon.name: Theme.icons.music
                onTriggered: EditorState.addBeatMarkersForSelection(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, 2)
            }
            ThemedMenuItem {
                text: qsTr("Add beat markers: every bar")
                icon.name: Theme.icons.music
                onTriggered: EditorState.addBeatMarkersForSelection(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, 0)
            }
            ThemedMenuItem {
                text: qsTr("Auto-cut to beat…")
                icon.name: Theme.icons.scissors
                visible: clipContextMenu.owner.clipData.kind === "audio"
                         && typeof ShellBridge.main?.openBeatCut === "function"
                enabled: EditorState.beatCutTargetCount(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex) > 0
                onTriggered: ShellBridge.main.openBeatCut(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Cancel audio analysis")
                icon.name: Theme.icons.x
                visible: EditorState.audioAlign.running
                onTriggered: EditorState.audioAlign.cancel()
            }
        }
    }

    Component {
        id: transformMenuComponent
        ThemedContextMenu {
            title: qsTr("Transform")
            ThemedMenuItem {
                text: qsTr("Crop…")
                icon.name: Theme.icons.crop
                // Desktop only: AndroidMain has no crop window for timeline clips.
                visible: typeof ShellBridge.main?.openCrop === "function"
                onTriggered: ShellBridge.main.openCrop(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Crop on canvas")
                icon.name: Theme.icons.crop
                visible: clipContextMenu.owner.clipData.kind === "video" || clipContextMenu.owner.clipData.kind === "image"
                onTriggered: {
                    EditorState.selectClip(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                    EditorState.preview.clipCropMode = true
                }
            }
            ThemedMenuItem {
                text: qsTr("Auto reframe…")
                icon.name: Theme.icons.crop
                visible: typeof clipContextMenu.owner.panel.requestAutoReframe === "function"
                onTriggered: clipContextMenu.owner.panel.requestAutoReframe(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Enhance video…")
                icon.name: Theme.icons.sparkles
                // Desktop only: AndroidMain has no restore window.
                visible: clipContextMenu.owner.clipData.kind === "video"
                         && typeof ShellBridge.main?.openRestore === "function"
                onTriggered: ShellBridge.main.openRestore(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Stabilize…")
                icon.name: Theme.icons.locateFixed
                visible: clipContextMenu.owner.clipData.kind === "video"
                onTriggered: clipContextMenu.openProperties("stabilize")
            }
        }
    }

    Component {
        id: moreMenuComponent
        ThemedContextMenu {
            title: qsTr("More")
            Component.onCompleted: clipContextMenu.rebuildMoveToTrackMenu(this)

            ThemedMenuItem {
                text: clipContextMenu.owner.clipData.loop ? qsTr("Stop looping") : qsTr("Loop")
                icon.name: Theme.icons.repeat
                visible: clipContextMenu.owner.clipData.kind === "video" || clipContextMenu.owner.clipData.kind === "audio"
                onTriggered: EditorState.setClipLoop(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, !clipContextMenu.owner.clipData.loop)
            }
            ThemedMenuItem {
                text: qsTr("Split on beats")
                icon.name: Theme.icons.scissors
                visible: EditorState.beatsAvailable && clipContextMenu.owner.clipData.kind !== "subtitle"
                onTriggered: EditorState.splitSelectionOnBeats(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, "beat")
            }
            ThemedMenuItem {
                text: qsTr("Snap clips to beats")
                icon.name: Theme.icons.music
                visible: EditorState.beatsAvailable
                onTriggered: EditorState.snapSelectionToBeats(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, "beat")
            }
            ThemedMenuItem {
                text: qsTr("Swap with previous clip")
                icon.name: Theme.icons.repeat
                visible: clipContextMenu.hasPrevious
                onTriggered: EditorState.swapWithNeighbour(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, false)
            }
            ThemedMenuItem {
                text: qsTr("Swap with next clip")
                icon.name: Theme.icons.repeat
                visible: clipContextMenu.hasNext
                onTriggered: EditorState.swapWithNeighbour(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex, true)
            }
            ThemedMenuItem {
                readonly property string blocker: EditorState.masking.convertClipToMaskBlocker(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                text: blocker !== "" ? qsTr("Convert to mask") + " — " + blocker : qsTr("Convert to mask")
                icon.name: Theme.icons.mask
                visible: clipContextMenu.owner.trackType === "video"
                enabled: EditorState.masking.canConvertClipToMask(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                onTriggered: EditorState.masking.convertClipToMask(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                readonly property var refs: clipContextMenu.owner.selected ? [] : [{ "track": clipContextMenu.owner.trackIndex, "clip": clipContextMenu.owner.clipIndex }]
                text: qsTr("Use as mask (nested timeline)")
                icon.name: Theme.icons.mask
                visible: clipContextMenu.owner.trackType === "video" && clipContextMenu.owner.clipData.kind !== "adjustment"
                enabled: EditorState.masking.canConvertClipsToSequenceMask(refs)
                onTriggered: EditorState.masking.convertClipsToSequenceMask(refs)
            }
            ThemedMenuItem {
                readonly property string sequenceId: EditorState.masking.maskSequenceId(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                text: qsTr("Open mask timeline")
                icon.name: Theme.icons.layers
                visible: sequenceId !== ""
                onTriggered: EditorState.openSequence(sequenceId)
            }
            ThemedMenuItem {
                text: qsTr("Convert to edit-friendly format")
                icon.name: Theme.icons.rabbit
                // Converts the bin media, so every clip using it follows along.
                visible: clipContextMenu.owner.mediaAssetId.length > 0 && !clipContextMenu.owner.mediaEditFriendly
                onTriggered: EditorState.bin.convertToConstantFrameRate([clipContextMenu.owner.mediaAssetId])
            }
            ThemedMenuItem {
                text: qsTr("Open composite")
                icon.name: Theme.icons.layers
                visible: clipContextMenu.owner.clipData.kind === "composite"
                onTriggered: EditorState.openCompositeClip(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Flatten composite")
                icon.name: Theme.icons.film
                visible: clipContextMenu.owner.clipData.kind === "composite"
                enabled: !EditorState.exportInProgress
                onTriggered: EditorState.flattenComposite(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Dissolve composite")
                icon.name: Theme.icons.layers
                visible: clipContextMenu.owner.clipData.kind === "composite"
                enabled: EditorState.canDissolveComposite(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                onTriggered: EditorState.dissolveComposite(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Make composite")
                icon.name: Theme.icons.layers
                visible: EditorState.makeCompositeAvailable
                onTriggered: EditorState.makeCompositeFromSelection()
            }
            ThemedMenuItem {
                text: EditorState.selection.length > 1 ? qsTr("Transform together")
                                                       : qsTr("Add transform layer")
                shortcutId: "transformTogether"
                icon.name: Theme.icons.group
                visible: EditorState.transformTogetherAvailable && !clipContextMenu.owner.isTransformClip
                onTriggered: EditorState.makeTransformLayerFromSelection()
            }
            ThemedMenuItem {
                text: qsTr("Select transform layer")
                shortcutId: "selectTransformLayer"
                icon.name: Theme.icons.maximize
                visible: clipContextMenu.owner.hasTransformParent && !clipContextMenu.owner.isTransformClip
                onTriggered: {
                    EditorState.selectClip(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                    EditorState.selectTransformParent()
                }
            }
            ThemedMenuItem {
                text: qsTr("Covers…")
                icon.name: Theme.icons.layers
                visible: clipContextMenu.owner.isTransformClip
                onTriggered: {
                    if (!clipContextMenu.owner.selected)
                        EditorState.selectClip(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
                    if (typeof clipContextMenu.owner.panel.openClipProperties === "function")
                        clipContextMenu.owner.panel.openClipProperties()
                }
            }
            ThemedMenuItem {
                text: qsTr("Select covered clips")
                icon.name: Theme.icons.maximize
                visible: clipContextMenu.owner.isTransformClip
                onTriggered: EditorState.selectTransformChildren(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Merge subtitle clips")
                shortcutId: "merge"
                icon.name: Theme.icons.linkTwo
                visible: clipContextMenu.owner.trackType === "subtitle"
                enabled: EditorState.mergeAvailable
                onTriggered: EditorState.mergeSelectedClips()
            }
            ThemedMenuItem {
                text: qsTr("Merge all subtitles on this track")
                icon.name: Theme.icons.linkTwo
                visible: clipContextMenu.owner.trackType === "subtitle"
                enabled: clipContextMenu.canMergeTrackSubtitles
                onTriggered: EditorState.mergeAllSubtitlesOnTrack(clipContextMenu.owner.trackIndex)
            }
            ThemedMenuItem {
                text: qsTr("Convert to text clips")
                icon.name: Theme.icons.type
                visible: clipContextMenu.owner.trackType === "subtitle"
                onTriggered: EditorState.convertSubtitleToTextClips(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Convert to subtitle")
                icon.name: Theme.icons.captions
                visible: clipContextMenu.owner.trackType === "text" && EditorState.textToSubtitleAvailable
                onTriggered: clipContextMenu.owner.panel.requestConvertTextToSubtitle()
            }
            ThemedMenuItem {
                text: qsTr("Unlink from clip")
                icon.name: Theme.icons.unlink
                visible: clipContextMenu.owner.pinnedToClip
                onTriggered: EditorState.unlinkAdjustment(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
            ThemedMenuItem {
                text: qsTr("Move to its own track")
                icon.name: Theme.icons.layers
                // Only meaningful for a nested one: a standalone adjustment already has one.
                visible: clipContextMenu.owner.clipData.kind === "adjustment"
                         && clipContextMenu.owner.panel.tracks[clipContextMenu.owner.trackIndex].isAdjustmentLane === true
                onTriggered: EditorState.moveAdjustmentToOwnTrack(clipContextMenu.owner.trackIndex, clipContextMenu.owner.clipIndex)
            }
        }
    }
}
