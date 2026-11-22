import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift

// A font selector that previews each family in its own face and groups them by catalog category.
// ThemedComboBox cannot do this: its delegate binds a single `root.font` for every row.
Item {
    id: root

    property string family: ""
    signal familyPicked(string family)

    readonly property string displayFamily: family === "" ? "Select a font" : family

    implicitHeight: Theme.touchInput && Theme.compact ? Theme.androidMinTouchTarget : 30
    implicitWidth: 200

    // One model shape whatever the source, so the delegate has a single contract: name is what the
    // user picks, previewFamily is the face to render it in, group is the section header.
    ListModel {
        id: familyModel
    }

    // True while no font addon is installed, which is the state of a fresh install.
    property bool usingSystemFonts: false
    property string filter: ""

    function addFont() {
        const url = FileDialogs.openFile(qsTr("Add font"), [qsTr("Fonts (*.ttf *.otf *.ttc *.otc)")])
        if (url.toString().length === 0)
            return
        const families = EditorState.importFontFile(url)
        if (families.length > 0)
            root.familyPicked(families[0])
    }

    function rebuild() {
        const rows = []
        const needle = root.filter
        const matches = (name) => needle === "" || name.toLowerCase().indexOf(needle) >= 0

        const catalog = EditorState.fontCatalog()
        root.usingSystemFonts = catalog.length === 0
        for (let i = 0; i < catalog.length; ++i) {
            if (!matches(catalog[i].family))
                continue
            rows.push({
                "name": catalog[i].family,
                "previewFamily": catalog[i].qtFamily,
                "group": catalog[i].categoryLabel,
                "removable": false
            })
        }

        const user = EditorState.userFonts()
        for (let i = 0; i < user.length; ++i) {
            if (matches(user[i]))
                rows.push({ "name": user[i], "previewFamily": user[i], "group": qsTr("My fonts"), "removable": true })
        }

        // Rendering each system family in its own face stalls scrolling on Windows (#345): every row
        // makes Qt resolve and load an installed font on the GUI thread. They use the UI font instead.
        // Fonts are an addon, so an empty catalog is the normal starting state; the system list
        // is always offered and the header suggests the pack.
        const system = EditorState.systemFontFamilies()
        for (let i = 0; i < system.length; ++i) {
            if (matches(system[i]))
                rows.push({ "name": system[i], "previewFamily": "", "group": qsTr("System fonts"), "removable": false })
        }

        familyModel.clear()
        familyModel.append(rows)
    }

    Timer {
        id: filterDebounce
        interval: 150
        onTriggered: root.rebuild()
    }

    Component.onCompleted: root.rebuild()

    Connections {
        target: EditorState
        function onUserFontsChanged() { root.rebuild() }
    }

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

    FontPickerTrigger {
        displayFamily: root.displayFamily
        previewFamily: root.family
        fieldFocused: root.activeFocus
        popupOpen: popup.visible
        onToggled: {
            root.forceActiveFocus()
            if (popup.visible)
                popup.close()
            else
                popup.open()
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

        onClosed: filterField.text = ""

        contentItem: ColumnLayout {
            spacing: 0

            ThemedTextField {
                id: filterField
                Layout.fillWidth: true
                Layout.margins: 4
                placeholderText: qsTr("Search fonts")
                font.family: Theme.fontFamily
                onTextChanged: {
                    root.filter = text.trim().toLowerCase()
                    filterDebounce.restart()
                }
            }

            FontPickerAddRow {
                onAddRequested: root.addFont()
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                reuseItems: true
                model: familyModel

                header: FontPickerPackBanner {
                    rowWidth: list.width
                    shown: root.usingSystemFonts
                    onPackRequested: {
                        popup.close()
                        ShellBridge.main.openAddonManager("fonts")
                    }
                }
                currentIndex: -1
                ScrollBar.vertical: AppScrollBar {}

                section.property: "group"
                section.delegate: FontPickerSectionHeader {
                    rowWidth: list.width
                }

                // Roles come in as required properties. Declaring any required property puts the
                // delegate in required-properties mode, where the `model` context object is not
                // injected — so every role the delegate reads has to be declared here.
                delegate: FontPickerFamilyRow {
                    rowWidth: list.width
                    selectedFamily: root.family
                    onFamilyChosen: function(familyName) {
                        root.forceActiveFocus()
                        root.familyPicked(familyName)
                        popup.close()
                    }
                    onRemoveRequested: function(familyName) {
                        EditorState.removeUserFont(familyName)
                    }
                }
            }
        }

        background: FontPickerPopupSurface { }
    }
}
