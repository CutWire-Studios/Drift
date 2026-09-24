import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// A font selector that previews each family in its own face and groups them by catalog category.
// ThemedComboBox cannot do this: its delegate binds a single `root.font` for every row.
Item {
    id: root

    property string family: ""
    signal familyPicked(string family)

    readonly property string displayFamily: family === "" ? "Select a font" : family

    implicitHeight: 30
    implicitWidth: 200

    // One model shape whatever the source, so the delegate has a single contract: name is what the
    // user picks, previewFamily is the face to render it in, group is the section header.
    ListModel {
        id: familyModel
    }

    // True while no font addon is installed, which is the state of a fresh install.
    property bool usingSystemFonts: false

    function rebuild() {
        familyModel.clear()
        const catalog = EditorState.fontCatalog()
        for (let i = 0; i < catalog.length; ++i) {
            familyModel.append({
                "name": catalog[i].family,
                "previewFamily": catalog[i].qtFamily,
                "group": catalog[i].categoryLabel
            })
        }
        root.usingSystemFonts = familyModel.count === 0
        if (!root.usingSystemFonts)
            return

        // Fonts are an addon now, so an empty catalog is the normal starting state — fall back to
        // whatever the system has and offer the pack.
        const system = Qt.fontFamilies()
        for (let i = 0; i < system.length; ++i)
            familyModel.append({ "name": system[i], "previewFamily": system[i], "group": "System fonts" })
    }

    Component.onCompleted: root.rebuild()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "fonts")
                root.rebuild()
        }
    }

    function changeFontDelta(delta) {
        if (familyModel.count === 0)
            return;

        let currentIndex = -1;
        for (let i = 0; i < familyModel.count; ++i) {
            if (familyModel.get(i).name === root.family) {
                currentIndex = i;
                break;
            }
        }

        let nextIndex = 0;
        if (currentIndex === -1) {
            nextIndex = delta > 0 ? 0 : familyModel.count - 1;
        } else {
            nextIndex = currentIndex + delta;
            nextIndex = Math.max(0, Math.min(familyModel.count - 1, nextIndex));
        }

        if (nextIndex !== currentIndex) {
            const nextFamily = familyModel.get(nextIndex).name;
            root.familyPicked(nextFamily);
            list.currentIndex = nextIndex;
            list.positionViewAtIndex(nextIndex, ListView.Contain);
        }
    }

    Keys.onUpPressed: (event) => {
        changeFontDelta(-1)
        event.accepted = true
    }
    Keys.onDownPressed: (event) => {
        changeFontDelta(1)
        event.accepted = true
    }
    Keys.onReturnPressed: (event) => {
        if (popup.visible) {
            popup.close()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: (event) => {
        if (popup.visible) {
            popup.close()
            event.accepted = true
        }
    }
    Keys.onEscapePressed: (event) => {
        if (popup.visible) {
            popup.close()
            event.accepted = true
        }
    }

    Rectangle {
        id: trigger
        anchors.fill: parent
        radius: Theme.radiusSm
        color: Theme.panelAccent
        border.width: (popup.visible || root.activeFocus) ? 1 : 0
        border.color: root.activeFocus ? Theme.primary : Theme.panelSecondaryBorder

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.right: chevron.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayFamily
            // Preview the current selection in the face it actually is.
            font.family: root.family === "" ? Theme.fontFamily : root.family
            font.pixelSize: Theme.fontSizeSm
            color: Theme.panelForeground
            elide: Text.ElideRight
        }

        IconGlyph {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.chevronDown
            iconSize: 12
            iconColor: Theme.mutedForeground
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.forceActiveFocus()
                if (popup.visible) {
                    popup.close()
                } else {
                    popup.open()
                }
            }
        }
    }

    Popup {
        id: popup

        onOpened: {
            let idx = -1;
            for (let i = 0; i < familyModel.count; ++i) {
                if (familyModel.get(i).name === root.family) {
                    idx = i;
                    break;
                }
            }
            list.currentIndex = idx;
            if (idx >= 0) {
                list.positionViewAtIndex(idx, ListView.Center);
            }
        }
        y: root.height + 2
        width: root.width
        implicitHeight: 320
        padding: 1

        contentItem: ListView {
            id: list
            clip: true
            reuseItems: true
            model: familyModel

            header: Rectangle {
                width: list.width
                height: root.usingSystemFonts ? 40 : 0
                visible: root.usingSystemFonts
                color: Theme.panelSecondaryBg

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    // The count was baked into the translated string, so it could
                    // not be updated or pluralised.
                    text: qsTr("Install the font pack for curated families →")
                    color: Theme.primary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        popup.close()
                        root.Window.window.openAddonManager("fonts")
                    }
                }
            }
            currentIndex: -1
            ScrollBar.vertical: AppScrollBar {}

            section.property: "group"
            section.delegate: Rectangle {
                required property string section
                width: list.width
                height: 24
                color: Theme.panelSecondaryBg

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.section
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Roles come in as required properties. Declaring any required property puts the
            // delegate in required-properties mode, where the `model` context object is not
            // injected — so every role the delegate reads has to be declared here.
            delegate: Rectangle {
                id: row
                required property string name
                required property string previewFamily

                width: list.width
                height: 34
                color: rowMouse.containsMouse ? Theme.panelAccent
                                              : (row.name === root.family ? Theme.panelSecondaryBg
                                                                          : "transparent")

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    font.family: row.previewFamily
                    font.pixelSize: 18
                    color: Theme.panelForeground
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.forceActiveFocus()
                        root.familyPicked(row.name)
                        popup.close()
                    }
                }
            }
        }

        background: Rectangle {
            radius: Theme.radiusSm
            color: Theme.panelBackground
            border.width: 1
            border.color: Theme.panelBorder
        }
    }
}
