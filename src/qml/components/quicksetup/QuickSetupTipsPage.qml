import QtQuick
import QtQuick.Layouts
import Drift

QuickSetupPage {
    id: root

    property int current: 0

    readonly property var tips: {
        const search = shortcutText("search")
        const first = shortcutText("workspace.1")
        const last = shortcutText("workspace.9")
        const list = []
        if (!mobile) {
            list.push({
                kind: "search",
                title: qsTr("Search anything"),
                text: search.length > 0
                      ? qsTr("Press %1 to search actions, workspaces, add-ons, themes and settings from anywhere, then jump straight to the result.").arg(search)
                      : qsTr("Use the search field in the header to find actions, workspaces, add-ons, themes and settings, then jump straight to the result.")
            })
            list.push({
                kind: "workspaces",
                title: qsTr("Switch workspaces"),
                text: first.length > 0 && last.length > 0
                      ? qsTr("Each workspace arranges the panels for a job, such as Edit, Color, Keyframes or Subtitles. Open the Workspace menu in the header, or press %1 to %2 to jump to one.").arg(first).arg(last)
                      : qsTr("Each workspace arranges the panels for a job, such as Edit, Color, Keyframes or Subtitles. Open the Workspace menu in the header to switch.")
            })
        }
        list.push({
            kind: "extras",
            title: qsTr("Get more from Extras"),
            text: mobile
                  ? qsTr("More effects, transitions, looks, fonts, stickers and AI models are installed from Extras, in the top bar.")
                  : qsTr("More effects, transitions, looks, fonts, stickers and AI models are installed from Extras, in the header.")
        })
        if (mobile) {
            list.push({
                kind: "touch",
                title: qsTr("Use your fingers"),
                text: qsTr("Pinch the timeline to zoom it, and pinch the preview to zoom and pan the picture. Long-press a clip and choose Select multiple to pick several clips at once.")
            })
        } else {
            const keys = []
            const defs = [
                { id: "playPause", label: qsTr("Play/Pause") },
                { id: "split", label: qsTr("Split at current time") },
                { id: "delete", label: qsTr("Delete selection") },
                { id: "undo", label: qsTr("Undo") }
            ]
            for (const d of defs) {
                const seq = EditorState.shortcutFor(d.id)
                if (seq.length > 0)
                    keys.push({ parts: keyParts(seq), label: d.label })
            }
            list.push({
                kind: "shortcuts",
                title: qsTr("Edit from the keyboard"),
                text: qsTr("A few shortcuts cover most of an edit. See and change every shortcut in Settings, under Keyboard shortcuts."),
                keycaps: keys
            })
        }
        list.push({
            kind: "ai",
            title: qsTr("AI tools on your device"),
            text: qsTr("Auto subtitles, people cutout and scene finding run locally. Install their add-ons from Extras, under AI tools.")
        })
        return list
    }
    readonly property var tip: tips[Math.min(current, tips.length - 1)]

    title: qsTr("A few tips before you start")
    subtitle: qsTr("Quick pointers to the parts of Drift people tend to miss.")
    glyph: Theme.icons.sparkles
    contentMaxWidth: 640

    function shortcutText(id) {
        const seq = EditorState.shortcutFor(id)
        return seq.length > 0 ? Theme.shortcutDisplay(seq) : ""
    }

    function keyParts(seq) {
        const shown = Theme.shortcutDisplay(seq)
        if (!Theme.isMacOS)
            return shown.split("+")
        const m = shown.match(/^([⌃⌥⇧⌘]*)(.*)$/)
        return m[1].split("").concat(m[2].length > 0 ? [m[2]] : [])
    }

    function go(step) {
        current = (current + step + tips.length) % tips.length
    }

    QuickSetupTipArt {
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: width * 0.625
        kind: root.tip.kind
        keycaps: root.tip.keycaps || []
        running: root.visible
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            tone: "default"
            size: "base"
            font.weight: Font.DemiBold
            text: root.tip.title
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            size: "sm"
            text: root.tip.text
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacingXl

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronLeft
            tooltip: qsTr("Previous tip")
            onClicked: root.go(-1)
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd

            Repeater {
                model: root.tips.length

                Rectangle {
                    required property int index

                    anchors.verticalCenter: parent.verticalCenter
                    width: index === root.current ? 20 : 8
                    height: 8
                    radius: 4
                    color: index === root.current ? Theme.c.quickSetup.dotActive : Theme.c.quickSetup.dotInactive

                    Behavior on width { NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing } }

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        onClicked: root.current = parent.index
                    }
                }
            }
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronRight
            tooltip: qsTr("Next tip")
            onClicked: root.go(1)
        }
    }
}
