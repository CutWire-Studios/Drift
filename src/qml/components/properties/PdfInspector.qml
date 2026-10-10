import QtQuick
import QtQuick.Window
import Drift
import ".."

// PDF clip inspector: which pages show and how they are laid out, then the viewport that scrolls
// and zooms through them. The clip frame itself is placed from the Transform tab.
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property bool hasPdf: hasSelection && clipData.kind === "pdf" && !!clipData.pdf
    readonly property var pdf: hasPdf ? clipData.pdf : ({
                                            "path": "", "pageCount": 0, "firstPage": 1, "lastPage": 0,
                                            "layout": "column", "gridColumns": 2, "gap": 12,
                                            "scrollX": 0, "scrollY": 0, "zoom": 1,
                                            "addonAvailable": true
                                        })
    readonly property var keyframes: (root.pdf && root.pdf.keyframes) || ({})
    readonly property int pageCount: root.pdf.pageCount || 0
    readonly property var layoutIds: ["column", "row", "grid"]
    readonly property int shownPages: {
        if (root.pageCount <= 0)
            return 1
        const last = root.pdf.lastPage > 0 ? Math.min(root.pdf.lastPage, root.pageCount) : root.pageCount
        return Math.max(1, last - root.pdf.firstPage + 1)
    }
    // Slider ends in page units: the scrolling axis spans every shown page (or grid row/column);
    // the other axis only pans across one page.
    readonly property int columnUnits: root.pdf.layout === "row" ? root.shownPages
                                     : root.pdf.layout === "grid" ? Math.min(root.pdf.gridColumns, root.shownPages) : 1
    readonly property int rowUnits: root.pdf.layout === "column" ? root.shownPages
                                  : root.pdf.layout === "grid" ? Math.ceil(root.shownPages / Math.max(1, root.pdf.gridColumns)) : 1

    function knob(key, label, def) {
        return { "key": "pdf." + key, "label": label, "def": Number(root.pdf[key]) || def, "decimals": 2 }
    }
    function knobKeyframes(key) {
        const entry = root.keyframes[key]
        return (entry && entry.points) || []
    }

    function refresh() {
        if (firstPageField && !firstPageField.activeFocus)
            firstPageField.value = root.pdf.firstPage
        if (lastPageField && !lastPageField.activeFocus)
            lastPageField.value = root.pdf.lastPage > 0 ? root.pdf.lastPage : root.pageCount
        if (columnsField && !columnsField.activeFocus)
            columnsField.value = root.pdf.gridColumns
        if (gapField && !gapField.activeFocus)
            gapField.value = root.pdf.gap
    }

    function setOption(key, value) {
        const patch = {}
        patch[key] = value
        const error = EditorState.setPdfOptions(EditorState.selectedTrack, EditorState.selectedClip, patch)
        if (error.length > 0)
            EditorState.setLastMessage(error, "error")
    }

    height: contentCol.height
    implicitHeight: contentCol.height

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refresh() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refresh() }
        function onTracksChanged() { root.clipDataRevision++; root.refresh() }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "pdfium") {
                root.clipDataRevision++
                root.refresh()
            }
        }
    }

    Component.onCompleted: refresh()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: !root.pdf.addonAvailable

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                wrapMode: Text.Wrap
                text: qsTr("PDF pages need the PDF Viewer addon.")
            }

            ThemedButton {
                text: qsTr("Install PDF Viewer")
                variant: "primary"
                glyph: Theme.icons.download
                onClicked: root.Window.window.openAddonManager("pdfium")
            }
        }

        // ----- Pages -------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Pages") }

            ThemedLabel {
                width: parent.width
                opacity: 0.8
                elide: Text.ElideMiddle
                visible: root.pageCount > 0
                text: qsTr("%n page(s)", "", root.pageCount)
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("First page")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: firstPageField
                        width: parent.width
                        from: 1
                        to: root.pageCount > 0 ? root.pageCount : 9999
                        onEdited: v => root.setOption("firstPage", v)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Last page")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: lastPageField
                        width: parent.width
                        from: root.pageCount > 0 ? 1 : 0
                        to: root.pageCount > 0 ? root.pageCount : 9999
                        onEdited: v => root.setOption("lastPage",
                                                      root.pageCount > 0 && v >= root.pageCount ? 0 : v)
                    }
                }
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("Layout")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedComboBox {
                        width: parent.width
                        model: [qsTr("Column"), qsTr("Row"), qsTr("Grid")]
                        tooltip: qsTr("How the pages are arranged")
                        currentIndex: Math.max(0, root.layoutIds.indexOf(root.pdf.layout))
                        onActivated: root.setOption("layout", root.layoutIds[currentIndex])
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    visible: root.pdf.layout === "grid"
                    Text {
                        text: qsTr("Columns")
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: columnsField
                        width: parent.width
                        from: 1
                        to: 20
                        onEdited: v => root.setOption("gridColumns", v)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: qsTr("Gap")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: gapField
                    width: parent.width
                    unit: "pt"
                    decimals: 1
                    step: 1
                    from: 0
                    to: 200
                    onEdited: v => root.setOption("gap", v)
                }
            }
        }

        // ----- Viewport ----------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel { text: qsTr("Viewport") }

            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("scrollX", qsTr("Scroll X"), 0)
                keyframeList: root.knobKeyframes("scrollX")
                useSlider: true
                sliderFrom: 0
                sliderTo: Math.max(1, root.columnUnits - 1)
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("scrollY", qsTr("Scroll Y"), 0)
                keyframeList: root.knobKeyframes("scrollY")
                useSlider: true
                sliderFrom: 0
                sliderTo: Math.max(1, root.rowUnits - 1)
            }
            PropertyKeyframeRow {
                width: parent.width
                propDef: root.knob("zoom", qsTr("Zoom"), 1)
                keyframeList: root.knobKeyframes("zoom")
                useSlider: true
                sliderFrom: 0.1
                sliderTo: 10
            }
        }
    }
}
