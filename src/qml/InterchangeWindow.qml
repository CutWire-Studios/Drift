import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

ToolWindow {
    id: root

    readonly property var interchange: EditorState.interchange
    readonly property var importFormats: interchange.formats.filter(f => f.canImport)
    readonly property var exportFormats: interchange.formats.filter(f => f.canExport)
    readonly property var analysis: interchange.analysis
    readonly property bool hasAnalysis: Object.keys(root.analysis).length > 0

    property string tab: "import"
    property string importFormatId: ""
    property string exportFormatId: ""
    property var exportReport: ({})
    property bool imported: false

    readonly property var importFormat: root.importFormats.find(f => f.id === root.importFormatId) ?? null
    readonly property var exportFormat: root.exportFormats.find(f => f.id === root.exportFormatId) ?? null

    width: 1000
    height: 700
    minimumWidth: 760
    minimumHeight: 520
    title: qsTr("Import / Export")
    color: Theme.c.interchange.background

    function open(tab, url) {
        root.visible = true
        root.raise()
        root.requestActivate()
        root.selectTab(tab === "export" ? "export" : "import")
        if (url && url.toString() !== "") {
            root.imported = false
            root.interchange.analyze(url)
        }
    }

    function selectTab(name) {
        root.tab = name
        if (name === "export") {
            if (!root.exportFormat && root.exportFormats.length > 0)
                root.exportFormatId = root.exportFormats[0].id
            root.refreshExport()
        } else if (!root.importFormat && root.importFormats.length > 0) {
            root.importFormatId = root.importFormats[0].id
        }
    }

    function refreshExport() {
        root.exportReport = root.exportFormatId !== "" ? root.interchange.analyzeExport(root.exportFormatId) : ({})
    }

    function chooseFile() {
        const filters = []
        const all = []
        if (root.importFormat) {
            const patterns = root.importFormat.extensions.map(e => "*." + e)
            filters.push(qsTr("%1 (%2)").arg(root.importFormat.name).arg(patterns.join(" ")))
        }
        for (const f of root.importFormats)
            for (const e of f.extensions)
                if (!all.includes("*." + e))
                    all.push("*." + e)
        filters.push(qsTr("All supported (%1)").arg(all.join(" ")))
        const url = FileDialogs.openFile(qsTr("Choose a file to import"), filters)
        if (url.toString() !== "") {
            root.imported = false
            root.interchange.analyze(url)
        }
    }

    function startImport() {
        const go = () => root.interchange.importAnalyzed()
        if (root.analysis.isTemplate)
            go()
        else
            ShellBridge.main.confirmDiscardForImport(go)
    }

    function exportFile() {
        const f = root.exportFormat
        if (!f)
            return
        const url = FileDialogs.saveFile(qsTr("Export %1").arg(f.name),
                                         [qsTr("%1 (%2)").arg(f.name).arg(f.extensions.map(e => "*." + e).join(" "))],
                                         EditorState.projectFile.projectName, f.extensions[0])
        if (url.toString() !== "")
            root.interchange.exportTo(f.id, url)
    }

    onClosing: root.interchange.clear()

    Connections {
        target: root.interchange
        function onImportFinished(ok) {
            if (ok)
                root.imported = true
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.visible
        onActivated: root.close()
    }

    SearchAnchor {
        searchId: "interchange-window"
        title: qsTr("Import / Export")
        keywords: "import export premiere resolve kdenlive shotcut vegas edl otio fcpxml mogrt timeline interchange"
        searchTarget: "dialog:interchange"
        target: root
    }

    SearchAnchor {
        searchId: "interchange-import"
        title: qsTr("Import a project from another editor")
        keywords: "import premiere prproj resolve drp kdenlive shotcut mlt vegas veg edl otio mogrt analyze"
        searchTarget: "dialog:interchange?tab=import"
        target: root
    }

    SearchAnchor {
        searchId: "interchange-export"
        title: qsTr("Export the timeline for another editor")
        keywords: "export fcpxml edl otio markers tsv timeline interchange"
        searchTarget: "dialog:interchange?tab=export"
        target: root
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingLg

        Row {
            spacing: Theme.spacingSm

            ThemedToggleButton {
                text: qsTr("Import")
                checked: root.tab === "import"
                onClicked: root.selectTab("import")
            }

            ThemedToggleButton {
                text: qsTr("Export")
                checked: root.tab === "export"
                onClicked: root.selectTab("export")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab === "export" ? 1 : 0

            RowLayout {
                spacing: Theme.spacingXl

                InterchangeFormatList {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    model: root.importFormats
                    currentId: root.importFormatId
                    onSelected: (id) => {
                        root.importFormatId = id
                        root.imported = false
                        root.interchange.clear()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingLg

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        visible: root.interchange.busy
                        running: visible
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !root.interchange.busy && !root.hasAnalysis
                        spacing: Theme.spacingLg

                        Text {
                            Layout.fillWidth: true
                            text: root.importFormat ? root.importFormat.name : ""
                            color: Theme.c.interchange.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase * 1.3
                            font.bold: true
                        }

                        InterchangeGapList {
                            Layout.fillWidth: true
                            gaps: root.importFormat ? root.importFormat.knownGaps : []
                        }

                        ThemedButton {
                            variant: "primary"
                            text: qsTr("Choose file…")
                            onClicked: root.chooseFile()
                        }

                        Item { Layout.fillHeight: true }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !root.interchange.busy && root.hasAnalysis
                        spacing: Theme.spacingLg

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingMd

                            Text {
                                Layout.fillWidth: true
                                text: (root.analysis.fileName ?? "")
                                      + (root.analysis.formatName ? "  ·  " + root.analysis.formatName : "")
                                color: Theme.c.interchange.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase * 1.3
                                font.bold: true
                                elide: Text.ElideMiddle
                            }

                            InterchangeStabilityChip {
                                visible: !!root.analysis.stability
                                stability: root.analysis.stability ?? "experimental"
                            }

                            ThemedButton {
                                variant: "ghost"
                                text: qsTr("Choose another file…")
                                onClicked: root.chooseFile()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.analysis.ok === false
                            text: root.analysis.error ?? ""
                            color: Theme.c.interchange.severityError
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase
                            wrapMode: Text.WordWrap
                        }

                        InterchangeReportView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.analysis.ok === true
                            report: root.analysis
                            emptyText: qsTr("Nothing is lost in this import.")
                        }

                        Item {
                            Layout.fillHeight: true
                            visible: root.analysis.ok !== true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.analysis.ok === true
                            spacing: Theme.spacingLg

                            Text {
                                Layout.fillWidth: true
                                text: root.imported ? qsTr("Imported. The report stays here for reference.")
                                      : root.analysis.isTemplate ? qsTr("Adds to the current project")
                                      : qsTr("Opens as a new untitled project")
                                color: root.imported ? Theme.c.interchange.success : Theme.c.interchange.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                wrapMode: Text.WordWrap
                            }

                            ThemedButton {
                                variant: "primary"
                                text: root.analysis.isTemplate ? qsTr("Insert at playhead") : qsTr("Import")
                                enabled: !root.imported
                                onClicked: root.startImport()
                            }
                        }
                    }
                }
            }

            RowLayout {
                spacing: Theme.spacingXl

                InterchangeFormatList {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    model: root.exportFormats
                    currentId: root.exportFormatId
                    onSelected: (id) => {
                        root.exportFormatId = id
                        root.refreshExport()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingLg

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMd

                        Text {
                            Layout.fillWidth: true
                            text: root.exportFormat ? root.exportFormat.name : ""
                            color: Theme.c.interchange.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase * 1.3
                            font.bold: true
                        }

                        InterchangeStabilityChip {
                            visible: root.exportFormat !== null
                            stability: root.exportFormat ? root.exportFormat.stability : "experimental"
                        }
                    }

                    InterchangeGapList {
                        Layout.fillWidth: true
                        gaps: root.exportFormat ? root.exportFormat.knownGaps : []
                    }

                    InterchangeReportView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        report: root.exportReport
                        emptyText: qsTr("Nothing is lost in this export.")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Item { Layout.fillWidth: true }

                        ThemedButton {
                            variant: "primary"
                            text: qsTr("Export…")
                            enabled: root.exportFormat !== null && !root.interchange.busy
                            onClicked: root.exportFile()
                        }
                    }
                }
            }
        }
    }
}
