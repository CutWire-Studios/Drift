import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Every editor and app preference, in one scrolling pane: the editor, then the app.
// Hosted by SettingsDialog; it used to be a tab in the assets panel's rail.
//
// Canvas size, aspect and frame rate are not here. On the phone they belong to the
// project rather than to preferences, and the project title's sheet already offers
// them under Canvas & layout and Project properties; on desktop they are the
// header's Video dialog. Both reach VideoSizeControls directly.
Item {
    id: root

    // Natural height of the whole list, so the dialog can size itself to the content
    // and cap it at what fits on screen instead of guessing.
    readonly property real contentHeight: flick.contentHeight

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: settingsColumn.height + Theme.spacing3xl
        clip: true
        property int dragLocks: 0
        interactive: dragLocks === 0
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: settingsColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingXl
            topPadding: Theme.pagePadding
            bottomPadding: Theme.spacingXl

            component SettingsSection: Rectangle {
                id: section
                property string title: ""
                default property alias content: body.data

                width: parent ? parent.width : 0
                implicitHeight: cardCol.implicitHeight
                height: implicitHeight
                radius: Theme.radiusMd
                color: Theme.darkMode ? Theme.panelAccent : Theme.appBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                Column {
                    id: cardCol
                    x: Theme.spacingXl
                    width: parent.width - Theme.spacingXl * 2
                    topPadding: Theme.spacingXl
                    bottomPadding: Theme.spacingXl
                    spacing: Theme.spacingLg

                    Text {
                        width: parent.width
                        text: section.title
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.DemiBold
                    }

                    Rectangle {
                        width: parent.width
                        height: Theme.borderWidth
                        color: Theme.panelBorder
                    }

                    Column {
                        id: body
                        width: parent.width
                        spacing: Theme.spacingLg
                    }
                }
            }

            SettingsSection {
                title: qsTr("Preview")

                ThemedSwitch {
                    checked: EditorState.guidesEnabled
                    text: qsTr("Show guides")
                    tooltip: qsTr("Show alignment guides over the preview")
                    onToggled: EditorState.guidesEnabled = checked
                }

                ThemedComboBox {
                    width: parent.width
                    visible: EditorState.guidesEnabled
                    textRole: "label"
                    valueRole: "id"
                    model: [
                        { id: "thirds", label: qsTr("Rule of thirds") },
                        { id: "crosshair", label: qsTr("Center cross") },
                        { id: "safe", label: qsTr("Safe margins") }
                    ]
                    tooltip: qsTr("Which guide to show")
                    currentIndex: {
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].id === EditorState.guideType)
                                return i
                        }
                        return 0
                    }
                    onActivated: EditorState.guideType = model[currentIndex].id
                }

                ThemedLabel {
                    text: qsTr("Background")
                }

                ThemedComboBox {
                    id: bgKindCombo
                    width: parent.width
                    textRole: "label"
                    valueRole: "id"
                    model: [
                        { id: "color", label: qsTr("Solid color") },
                        { id: "blur", label: qsTr("Blur") }
                    ]
                    tooltip: qsTr("Fill behind clips that don’t cover the whole screen")
                    currentIndex: {
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].id === EditorState.background.kind)
                                return i
                        }
                        return 0
                    }
                    onActivated: EditorState.setBackground({ kind: model[currentIndex].id })
                }

                ColorSwatchField {
                    visible: EditorState.background.kind === "color"
                    hex: EditorState.background.color || "#ff000000"
                    tooltip: qsTr("Choose background colour")
                    onEdited: value => EditorState.setBackground({ kind: "color", color: value })
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: EditorState.background.kind === "blur"

                    ThemedSlider {
                        width: parent.width
                        label: qsTr("Blur strength")
                        from: 1
                        to: 100
                        stepSize: 1
                        valueFormatter: function (v) { return Math.round(v) }
                        value: EditorState.background.blurStrength || 20
                        onPressedChanged: {
                            if (!pressed)
                                EditorState.setBackground({ kind: "blur", blurStrength: value })
                        }
                    }
                }

                ThemedSwitch {
                    visible: EditorState.vaapiZeroCopySupported
                    checked: EditorState.vaapiZeroCopy
                    text: qsTr("Faster preview (experimental)")
                    tooltip: qsTr("Can make playback smoother by keeping video on the graphics card. Turn it off if the picture looks wrong. Takes effect after restart.")
                    onToggled: EditorState.vaapiZeroCopy = checked
                }

                // Same wording as the VAAPI switch above: only one of the two is ever visible,
                // since each is supported on exactly the platform the other is not.
                ThemedSwitch {
                    visible: EditorState.mediaCodecZeroCopySupported
                    checked: EditorState.mediaCodecZeroCopy
                    text: qsTr("Faster preview (experimental)")
                    tooltip: qsTr("Can make playback smoother by keeping video on the graphics card. Turn it off if the picture looks wrong. Takes effect after restart.")
                    onToggled: EditorState.mediaCodecZeroCopy = checked
                }
            }

            SettingsSection {
                title: qsTr("Playback")

                ThemedLabel {
                    text: qsTr("Audio output")
                }

                ThemedComboBox {
                    id: audioOutputCombo
                    width: parent.width
                    textRole: "label"
                    valueRole: "id"
                    model: EditorState.audioOutputDevices
                    tooltip: qsTr("Where playback is heard. “System default” follows whatever your computer is set to, including when that changes.")
                    currentIndex: {
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].id === EditorState.audioOutputDeviceId)
                                return i
                        }
                        return 0
                    }
                    onActivated: EditorState.audioOutputDeviceId = model[currentIndex].id
                }
            }

            SettingsSection {
                title: qsTr("Interface")

                ThemedLabel {
                    text: qsTr("Size")
                }

                ThemedComboBox {
                    width: parent.width
                    textRole: "label"
                    valueRole: "id"
                    model: [
                        { id: 1.0,  label: qsTr("100% (system)") },
                        { id: 1.25, label: "125%" },
                        { id: 1.5,  label: "150%" },
                        { id: 1.75, label: "175%" },
                        { id: 2.0,  label: "200%" }
                    ]
                    tooltip: qsTr("Makes buttons, text, and icons larger. This is extra scale on top of the size already set in your display settings. Takes effect after restart.")
                    currentIndex: {
                        const opts = model
                        for (var i = 0; i < opts.length; ++i) {
                            if (Math.abs(opts[i].id - EditorState.uiScale) < 0.001)
                                return i
                        }
                        return 0
                    }
                    onActivated: {
                        if (currentIndex >= 0 && currentIndex < model.length)
                            EditorState.uiScale = model[currentIndex].id
                    }
                }

                ThemedLabel {
                    width: parent.width
                    visible: EditorState.uiScaleNeedsRestart
                    text: qsTr("Restart Drift to apply this size.")
                    color: Theme.panelSecondaryForeground
                }

                ThemedSwitch {
                    visible: !Theme.touchUi
                    checked: EditorState.invertTimelineScroll
                    text: qsTr("Horizontal mouse-wheel pan")
                    tooltip: qsTr("Scroll pans left and right along the timeline. Shift+scroll moves between tracks. Middle-click drag also pans.")
                    onToggled: EditorState.invertTimelineScroll = checked
                }

                ThemedSwitch {
                    visible: Haptics.supported
                    checked: Haptics.enabled
                    text: qsTr("Haptic feedback")
                    tooltip: qsTr("Vibrate on taps, snaps, and edits. Uses this device’s own haptic effects when it has them.")
                    onToggled: Haptics.enabled = checked
                }

                ThemedLabel {
                    visible: Theme.touchUi
                    text: qsTr("Language")
                }

                ThemedComboBox {
                    width: parent.width
                    visible: Theme.touchUi
                    textRole: "label"
                    valueRole: "id"
                    model: EditorState.uiLanguages
                    tooltip: qsTr("Language for menus and labels. Takes effect immediately.")
                    currentIndex: {
                        const langs = EditorState.uiLanguages
                        for (var i = 0; i < langs.length; ++i) {
                            if (langs[i].id === EditorState.uiLanguage)
                                return i
                        }
                        return 0
                    }
                    onActivated: {
                        const langs = EditorState.uiLanguages
                        if (currentIndex >= 0 && currentIndex < langs.length)
                            EditorState.uiLanguage = langs[currentIndex].id
                    }
                }
            }

            SettingsSection {
                title: qsTr("App")

                ThemedSwitch {
                    checked: EditorState.reopenLastProject
                    text: qsTr("Reopen last project on startup")
                    tooltip: qsTr("Automatically restore the last open project on startup. Closing still asks you to save; a crash snapshot never overwrites your save file.")
                    onToggled: EditorState.reopenLastProject = checked
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingLg
                    visible: Updates.supported

                    Rectangle {
                        width: parent.width
                        height: Theme.borderWidth
                        color: Theme.panelBorder
                    }

                    ThemedLabel {
                        text: qsTr("Updates")
                    }

                    ThemedSwitch {
                        checked: Updates.enabled
                        text: qsTr("Check on startup")
                        tooltip: qsTr("Ask GitHub once a day whether a newer Drift has been released")
                        onToggled: Updates.enabled = checked
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.spacingMd

                        ThemedButton {
                            id: checkNowButton
                            variant: "secondary"
                            glyph: Theme.icons.refresh
                            text: Updates.checking ? qsTr("Checking…") : qsTr("Check now")
                            enabled: !Updates.checking
                            onClicked: Updates.checkNow()
                        }

                        ThemedLabel {
                            width: Math.max(0, parent.width - checkNowButton.width - parent.spacing)
                            anchors.verticalCenter: parent.verticalCenter
                            text: Updates.status.length > 0
                                  ? Updates.status
                                  : qsTr("Drift %1").arg(Updates.currentVersion)
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }

                ThemedLabel {
                    text: qsTr("Extra packs")
                }

                ThemedSwitch {
                    checked: Addons.remindEssential
                    text: qsTr("Remind about essential packs")
                    tooltip: qsTr("Pulse the Extras icon when the video, transitions, and audio packs are not installed")
                    onToggled: Addons.remindEssential = checked
                }

                ThemedSwitch {
                    checked: Addons.remindUpdates
                    text: qsTr("Remind about pack updates")
                    tooltip: qsTr("Pulse the Extras icon when updates are available for packs you already have installed")
                    onToggled: Addons.remindUpdates = checked
                }

                Rectangle {
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }

                ThemedLabel {
                    text: qsTr("Agent access")
                }

                // Shared with the header's AgentAccessDialog rather than restated: the
                // cut-down copy that lived here offered only the Claude command, so a
                // switch turned on from this pane could not be connected from Cursor.
                AgentAccessControls {
                    width: parent.width
                    showIntro: false
                }
            }

            SettingsSection {
                title: qsTr("Marketplace")
                visible: Market.configured && Market.authenticated

                ThemedLabel {
                    width: parent.width
                    text: Market.accountName.length > 0
                          ? qsTr("Account connected (%1)").arg(Market.accountName)
                          : qsTr("Marketplace account connected")
                    color: Theme.panelForeground
                }

                ThemedButton {
                    text: qsTr("Disconnect")
                    variant: "ghost"
                    tooltip: qsTr("Unlink the marketplace account from this device")
                    onClicked: Market.disconnectAccount()
                }
            }
        }
    }
}
