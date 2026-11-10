pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Source editor for a Procedural 3D package: procedural.json, main.lua and every shader. It opens
// the package of the selected procedural clip. Apply (Ctrl+S) validates and installs through the
// same service the MCP tools use; the result lands in the Procedural console.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "procedural-editor"
        title: qsTr("Procedural code editor")
        keywords: "lua glsl shader script procedural 3d scene code edit apply"
    }

    property bool shown: true
    property string currentFile: "main.lua"
    property bool loading: false
    property bool creating: false
    property string pendingOpen: ""
    property var packages: EditorState.proceduralPackages()

    readonly property var clipData: EditorState.selectedClipData
    readonly property string selectedPackage: !!clipData && clipData.kind === "procedural" && !!clipData.procedural
                                              ? clipData.procedural.packageId : ""
    readonly property bool hasPackage: ProceduralEditor.packageId.length > 0
    readonly property bool editable: hasPackage && !ProceduralEditor.readOnly
    readonly property int packageIndex: {
        for (let i = 0; i < packages.length; ++i) {
            if (packages[i].id === ProceduralEditor.packageId)
                return i
        }
        return -1
    }

    function loadFile() {
        const wanted = ProceduralEditor.text(root.currentFile)
        if (editor.text === wanted)
            return
        loading = true
        editor.text = wanted
        loading = false
    }

    function requestOpen(id) {
        if (id.length === 0 || id === ProceduralEditor.packageId)
            return
        if (ProceduralEditor.dirty)
            pendingOpen = id
        else
            ProceduralEditor.open(id)
    }

    function applyEdits() {
        if (editable)
            ProceduralEditor.apply()
    }

    function createPackage() {
        if (ProceduralEditor.createNew(nameField.text).length > 0) {
            creating = false
            nameField.text = ""
        }
    }

    onSelectedPackageChanged: requestOpen(selectedPackage)
    Component.onCompleted: {
        requestOpen(selectedPackage)
        loadFile()
    }
    onCurrentFileChanged: loadFile()

    Connections {
        target: ProceduralEditor

        function onPackageChanged() {
            if (!ProceduralEditor.fileNames.includes(root.currentFile))
                root.currentFile = "main.lua"
            root.loadFile()
        }

        function onCatalogChanged() {
            root.packages = EditorState.proceduralPackages()
        }
    }

    Item {
        id: toolbar
        width: parent.width
        height: toolbarFlow.height + Theme.spacingMd * 2

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.proceduralEditorPanel.borderWidth
            color: Theme.c.proceduralEditorPanel.border
        }

        Flow {
            id: toolbarFlow
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingXs

            ThemedComboBox {
                width: Math.max(160, Math.min(240, widestContentWidth + Theme.spacingLg))
                model: root.packages.map(p => p.id.startsWith("user.") ? p.name : qsTr("%1 (read-only)").arg(p.name))
                currentIndex: root.packageIndex
                displayText: root.packageIndex < 0 ? qsTr("Pick a package") : currentText
                tooltip: qsTr("The package to edit. Selecting a procedural clip opens its package.")
                onActivated: root.requestOpen(root.packages[currentIndex].id)
            }

            ThemedButton {
                text: qsTr("New")
                glyph: Theme.icons.plus
                enabled: !ProceduralEditor.dirty
                tooltip: ProceduralEditor.dirty ? qsTr("Apply or revert your edits first")
                                                : qsTr("Create a minimal procedural package")
                onClicked: root.creating = !root.creating
                SearchAnchor {
                    searchId: "procedural-new"
                    title: qsTr("New procedural package")
                    keywords: "create template lua shader"
                }
            }

            ThemedButton {
                visible: ProceduralEditor.readOnly
                text: qsTr("Duplicate to edit")
                glyph: Theme.icons.copy
                variant: "primary"
                tooltip: qsTr("Copy this package to your own procedurals and edit the copy")
                onClicked: ProceduralEditor.duplicate()
                SearchAnchor {
                    searchId: "procedural-duplicate"
                    title: qsTr("Duplicate procedural to edit")
                    keywords: "copy fork bundled read-only"
                }
            }

            ThemedButton {
                visible: !ProceduralEditor.readOnly
                text: qsTr("Apply")
                variant: "primary"
                enabled: root.editable
                tooltip: qsTr("Validate and install the package, then refresh the preview (Ctrl+S)")
                onClicked: root.applyEdits()
                SearchAnchor {
                    searchId: "procedural-apply"
                    title: qsTr("Apply procedural code")
                    keywords: "save run validate install ctrl+s"
                }
            }

            ThemedButton {
                text: qsTr("Check")
                enabled: root.hasPackage
                tooltip: qsTr("Validate the edits without installing them")
                onClicked: ProceduralEditor.check()
            }

            ThemedButton {
                text: qsTr("Revert")
                enabled: ProceduralEditor.dirty
                tooltip: qsTr("Discard edits and reload the package from disk")
                onClicked: ProceduralEditor.revert()
            }

            ThemedButton {
                text: qsTr("Add clip")
                glyph: Theme.icons.plus
                enabled: root.hasPackage
                tooltip: qsTr("Add a clip using this package at the playhead")
                onClicked: EditorState.addProceduralClip(ProceduralEditor.packageId, -1, -1, ({}))
            }
        }
    }

    Rectangle {
        id: banner
        readonly property bool confirming: root.pendingOpen.length > 0
        visible: confirming || root.creating
        anchors.top: toolbar.bottom
        width: parent.width
        height: visible ? bannerRow.height + Theme.spacingMd * 2 : 0
        color: Theme.c.proceduralEditorPanel.bannerBackground

        Flow {
            id: bannerRow
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingXs

            Text {
                visible: banner.confirming
                height: Theme.controlHeightSm
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Discard unapplied edits?")
                color: Theme.c.proceduralEditorPanel.bannerForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            ThemedButton {
                visible: banner.confirming
                text: qsTr("Discard")
                onClicked: {
                    const id = root.pendingOpen
                    root.pendingOpen = ""
                    ProceduralEditor.open(id)
                }
            }
            ThemedButton {
                visible: banner.confirming
                text: qsTr("Keep editing")
                onClicked: root.pendingOpen = ""
            }

            ThemedTextField {
                id: nameField
                visible: root.creating && !banner.confirming
                width: 200
                placeholderText: qsTr("Package name")
                onAccepted: root.createPackage()
            }
            ThemedButton {
                visible: root.creating && !banner.confirming
                text: qsTr("Create")
                variant: "primary"
                enabled: nameField.text.trim().length > 0
                onClicked: root.createPackage()
            }
            ThemedButton {
                visible: root.creating && !banner.confirming
                text: qsTr("Cancel")
                onClicked: root.creating = false
            }
        }
    }

    Item {
        id: tabs
        visible: root.hasPackage
        anchors.top: banner.bottom
        width: parent.width
        height: visible ? tabFlow.height + Theme.spacingXs * 2 : 0

        Flow {
            id: tabFlow
            x: Theme.spacingMd
            y: Theme.spacingXs
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingXs

            Repeater {
                model: ProceduralEditor.fileNames
                delegate: ThemedToggleButton {
                    required property string modelData
                    text: modelData
                    checked: root.currentFile === modelData
                    onClicked: root.currentFile = modelData
                }
            }

            ThemedButton {
                visible: root.editable
                glyph: Theme.icons.plus
                tooltip: qsTr("Add a shader pair")
                onClicked: shaderRow.visible = !shaderRow.visible
            }

            Row {
                id: shaderRow
                visible: false
                spacing: Theme.spacingXs

                ThemedTextField {
                    id: shaderName
                    width: 140
                    placeholderText: qsTr("Shader name")
                    onAccepted: addShader.clicked()
                }
                ThemedButton {
                    id: addShader
                    text: qsTr("Add")
                    enabled: shaderName.text.trim().length > 0
                    onClicked: {
                        if (ProceduralEditor.addShaderPair(shaderName.text.trim())) {
                            root.currentFile = shaderName.text.trim() + ".frag"
                            shaderName.text = ""
                            shaderRow.visible = false
                        }
                    }
                }
            }
        }
    }

    Item {
        id: body
        visible: root.hasPackage
        anchors.top: tabs.bottom
        anchors.bottom: status.top
        width: parent.width

        FontMetrics {
            id: metrics
            font: editor.font
        }

        readonly property real lineHeight: editor.lineCount > 0 ? Math.max(1, editor.contentHeight / editor.lineCount)
                                                                 : metrics.height

        Rectangle {
            id: gutterBox
            width: Math.max(String(editor.lineCount).length, 2) * metrics.averageCharacterWidth + Theme.spacingLg * 2
            height: parent.height
            color: Theme.c.proceduralEditorPanel.gutterBackground
            clip: true

            ListView {
                id: gutter
                anchors.fill: parent
                interactive: false
                model: editor.lineCount
                delegate: Text {
                    required property int index
                    width: gutter.width
                    height: body.lineHeight
                    rightPadding: Theme.spacingLg
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: index + 1
                    color: Theme.c.proceduralEditorPanel.gutterForeground
                    font: editor.font
                }
            }

            Binding {
                target: gutter
                property: "contentY"
                value: flick.contentY - editor.topPadding
            }
        }

        Flickable {
            id: flick
            anchors.left: gutterBox.right
            anchors.right: parent.right
            height: parent.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }
            ScrollBar.horizontal: AppScrollBar { }

            TextArea.flickable: TextArea {
                id: editor
                readOnly: !root.editable
                wrapMode: TextArea.NoWrap
                color: Theme.c.proceduralEditorPanel.foreground
                selectionColor: Theme.c.proceduralEditorPanel.selection
                selectedTextColor: Theme.c.proceduralEditorPanel.selectionForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeSm
                selectByMouse: true
                persistentSelection: true
                background: Rectangle {
                    color: Theme.c.proceduralEditorPanel.background
                }
                onTextChanged: {
                    if (!root.loading)
                        ProceduralEditor.setText(root.currentFile, editor.text)
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
                        root.applyEdits()
                        event.accepted = true
                    }
                }
            }
        }

        ProceduralHighlighter {
            textDocument: editor.textDocument
            language: ProceduralEditor.languageFor(root.currentFile)
            keywordColor: Theme.c.proceduralEditorPanel.syntaxKeyword
            typeColor: Theme.c.proceduralEditorPanel.syntaxType
            builtinColor: Theme.c.proceduralEditorPanel.syntaxBuiltin
            stringColor: Theme.c.proceduralEditorPanel.syntaxString
            commentColor: Theme.c.proceduralEditorPanel.syntaxComment
            numberColor: Theme.c.proceduralEditorPanel.syntaxNumber
        }
    }

    Rectangle {
        id: status
        visible: root.hasPackage
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.controlHeightSm + Theme.spacingXs
        color: Theme.c.proceduralEditorPanel.bannerBackground

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: ProceduralEditor.readOnly ? qsTr("%1 is read-only").arg(ProceduralEditor.packageId)
                  : ProceduralEditor.dirty ? qsTr("%1 - unapplied edits").arg(ProceduralEditor.packageId)
                                           : ProceduralEditor.packageId
            color: Theme.c.proceduralEditorPanel.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 300)
        visible: !root.hasPackage
        compact: true
        title: qsTr("No package open")
        hint: qsTr("Select a procedural clip, pick a package above, or create a new one.")
    }
}
