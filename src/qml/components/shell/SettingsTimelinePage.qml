import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "timeline"
    title: qsTr("Timeline")

    SettingsGroupHeader {
        text: qsTr("Scrolling")
        available: !Theme.touchUi
        query: page.query
    }

    SettingsRow {
        title: qsTr("Mouse wheel scrolls")
        searchId: "timeline-mouse-wheel-scrolls"
        description: qsTr("What the mouse wheel moves on the timeline. Hold Shift for the other direction; middle-click drag also pans.")
        keywords: "scroll wheel pan horizontal vertical invert direction mouse"
        available: !Theme.touchUi
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            model: [
                { id: false, label: qsTr("Tracks (up and down)") },
                { id: true,  label: qsTr("Time (left and right)") }
            ]
            currentIndex: EditorState.preferences.invertTimelineScroll ? 1 : 0
            onActivated: EditorState.preferences.invertTimelineScroll = currentIndex === 1
        }
    }

    SettingsGroupHeader {
        text: qsTr("Editing")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Auto-crossfade overlapping clips")
        searchId: "timeline-auto-crossfade"
        description: qsTr("When a move creates an overlap with a neighbouring clip on the same track, add a crossfade across it.")
        keywords: "transition dissolve overlap"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.autoCrossfadeOverlaps
            onToggled: EditorState.preferences.autoCrossfadeOverlaps = checked
        }
    }

    SettingsRow {
        title: qsTr("Auto-select clip under playhead")
        searchId: "timeline-auto-select-clip"
        description: qsTr("Select the clip under the playhead as the playhead moves.")
        keywords: "locate follow select"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.autoSelectUnderPlayhead
            onToggled: EditorState.preferences.autoSelectUnderPlayhead = checked
        }
    }

    SettingsRow {
        title: qsTr("Auto keyframes")
        searchId: "timeline-auto-keyframes"
        description: qsTr("Dragging a slider also creates keyframes.")
        keywords: "keyframe animate record"
        query: page.query

        ThemedSwitch {
            checked: EditorState.autoKeyEnabled
            onToggled: EditorState.autoKeyEnabled = checked
        }
    }

    SettingsRow {
        title: qsTr("Loop work area")
        searchId: "timeline-loop-work-area"
        description: qsTr("Playback repeats between the work area in and out points.")
        keywords: "repeat in out"
        query: page.query

        ThemedSwitch {
            checked: EditorState.loopWorkAreaEnabled
            onToggled: EditorState.loopWorkAreaEnabled = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Defaults")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Still image duration")
        searchId: "timeline-still-image-duration"
        description: qsTr("Length of images and stills when added to the timeline.")
        keywords: "image photo picture length"
        query: page.query

        ThemedNumberField {
            id: stillDurationField
            width: Theme.c.settingsRow.controlWidth / 2
            from: 0.5
            to: 60
            step: 0.5
            decimals: 1
            unit: "s"
            onEdited: (v) => EditorState.preferences.stillDurationSeconds = v
        }

        // A plain value binding is dropped once the field writes its own edit back.
        Binding {
            target: stillDurationField
            property: "value"
            value: EditorState.preferences.stillDurationSeconds
        }
    }

    SettingsRow {
        title: qsTr("Transition length")
        searchId: "timeline-transition-length"
        description: qsTr("Length of new transitions dropped between clips.")
        keywords: "crossfade duration seconds"
        query: page.query

        ThemedNumberField {
            id: transitionDurationField
            width: Theme.c.settingsRow.controlWidth / 2
            from: 0.1
            to: 5.0
            step: 0.1
            decimals: 1
            unit: "s"
            onEdited: (v) => EditorState.preferences.transitionDurationSeconds = v
        }

        Binding {
            target: transitionDurationField
            property: "value"
            value: EditorState.preferences.transitionDurationSeconds
        }
    }

    SettingsGroupHeader {
        text: qsTr("Panels")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Show timeline overview")
        searchId: "timeline-show-overview"
        description: qsTr("A minimap of the whole project. Click or drag it to jump the view.")
        keywords: "minimap navigator"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.timelineOverviewVisible
            onToggled: EditorState.preferences.timelineOverviewVisible = checked
        }
    }

    SettingsRow {
        title: qsTr("Show audio mixer")
        searchId: "timeline-show-audio-mixer"
        description: qsTr("Show the audio mixer strip beside the timeline.")
        keywords: "volume levels strip"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.audioMixerVisible
            onToggled: EditorState.preferences.audioMixerVisible = checked
        }
    }
}
