pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "color-grade"
        title: qsTr("Color grade")
        keywords: "grade color workspace open"
        searchTarget: "panel:color#color-grade"
    }

    property bool shown: false

    readonly property var colorState: EditorState.color
    readonly property var target: colorState.target
    readonly property bool hasTarget: !!target.hasTarget
    readonly property var params: target.params || ({})
    readonly property var finishing: target.finishing || ([])
    readonly property var animatedKeys: target.animatedKeys || ([])
    property int tab: 0
    property var live: ({})
    property var expanded: ({})
    property var finishingSpecs: ({})
    property string applyStatus: ""
    property bool includeFinishing: false
    property bool compareWasOn: false
    property real compareDownAt: 0
    readonly property real bodyW: content.width
    readonly property real bodyH: body.height
    readonly property bool wide: bodyW >= 560

    readonly property var tabs: [qsTr("Basic"), qsTr("Wheels"), qsTr("Curves"), qsTr("HSL"), qsTr("Finishing")]
    readonly property var lightSliders: [
        { key: "exposure", label: qsTr("Exposure"), from: -4, to: 4, def: 0, curvePower: 2 },
        { key: "contrast", label: qsTr("Contrast"), from: -1, to: 1, def: 0 },
        { key: "highlights", label: qsTr("Highlights"), from: -1, to: 1, def: 0 },
        { key: "shadows", label: qsTr("Shadows"), from: -1, to: 1, def: 0 },
        { key: "whites", label: qsTr("Whites"), from: -1, to: 1, def: 0 },
        { key: "blacks", label: qsTr("Blacks"), from: -1, to: 1, def: 0 }
    ]
    readonly property var colorSliders: [
        { key: "temperature", label: qsTr("Temperature"), from: -1, to: 1, def: 0,
          gradient: Theme.c.colorGradeTracks.temperature },
        { key: "tint", label: qsTr("Tint"), from: -1, to: 1, def: 0,
          gradient: Theme.c.colorGradeTracks.tint },
        { key: "saturation", label: qsTr("Saturation"), from: 0, to: 2, def: 1 },
        { key: "vibrance", label: qsTr("Vibrance"), from: -1, to: 1, def: 0 }
    ]
    readonly property var wheels: [
        { label: qsTr("Shadows"), key: "lift" },
        { label: qsTr("Midtones"), key: "gamma" },
        { label: qsTr("Highlights"), key: "gain" }
    ]
    readonly property var curveKeys: ({ master: "curveMaster", r: "curveR", g: "curveG", b: "curveB" })
    readonly property var hueCurveKeys: ({ hue: "curveHueHue", sat: "curveHueSat", lum: "curveHueLum" })
    readonly property var hslBands: [
        { name: "red", param: "Red", label: qsTr("Red"), hue: 0 },
        { name: "orange", param: "Orange", label: qsTr("Orange"), hue: 30 },
        { name: "yellow", param: "Yellow", label: qsTr("Yellow"), hue: 60 },
        { name: "green", param: "Green", label: qsTr("Green"), hue: 120 },
        { name: "aqua", param: "Aqua", label: qsTr("Aqua"), hue: 180 },
        { name: "blue", param: "Blue", label: qsTr("Blue"), hue: 225 },
        { name: "purple", param: "Purple", label: qsTr("Purple"), hue: 270 },
        { name: "magenta", param: "Magenta", label: qsTr("Magenta"), hue: 300 }
    ]
    readonly property var hslSliders: [
        { suffix: "Hue", label: qsTr("Hue") },
        { suffix: "Sat", label: qsTr("Saturation") },
        { suffix: "Lum", label: qsTr("Luminance") }
    ]
    property int hslBand: 0
    readonly property var finishingNames: ({
        "film_grain": qsTr("Film Grain"),
        "bloom_glow": qsTr("Glow"),
        "chromatic_aberration": qsTr("Chromatic Aberration"),
        "stylize.vignette": qsTr("Vignette"),
        "halation": qsTr("Halation")
    })
    readonly property var finishingHints: ({
        "film_grain": qsTr("Adds film-like noise"),
        "bloom_glow": qsTr("Soft glow around bright areas"),
        "chromatic_aberration": qsTr("Color fringing at the edges"),
        "stylize.vignette": qsTr("Darkens the corners"),
        "halation": qsTr("Red glow around highlights, like film")
    })

    readonly property var modifiedGroups: target.modifiedGroups || ([])
    readonly property bool lightModified: modifiedGroups.indexOf("Light") >= 0
    readonly property bool colorModified: modifiedGroups.indexOf("Color") >= 0
    readonly property bool lookModified: modifiedGroups.indexOf("Look") >= 0
    readonly property bool wheelsModified: modifiedGroups.indexOf("Wheels") >= 0
    readonly property bool curvesAreModified: modifiedGroups.indexOf("Curves") >= 0
    readonly property bool hueCurvesAreModified: modifiedGroups.indexOf("Hue Curves") >= 0
    readonly property bool hslModified: modifiedGroups.indexOf("HSL") >= 0

    function isAnimated(key) {
        return animatedKeys.indexOf(key) >= 0
    }

    function finishingAnimated(entry, key) {
        return (entry.animatedKeys || []).indexOf(key) >= 0
    }

    function setLive(key, v) {
        const next = Object.assign({}, live)
        next[key] = v
        live = next
    }

    function clearLive(key) {
        const next = Object.assign({}, live)
        delete next[key]
        live = next
    }

    function liveOr(key, fallback) {
        return live[key] !== undefined ? live[key] : fallback
    }

    function finishingEntry(id) {
        for (let i = 0; i < finishing.length; ++i) {
            if (finishing[i].id === id)
                return finishing[i]
        }
        return ({ id: id, present: false, enabled: false, params: ({}) })
    }

    function isExpanded(id) {
        return expanded[id] === undefined ? true : expanded[id]
    }

    function toggleExpanded(id) {
        const next = Object.assign({}, expanded)
        next[id] = !isExpanded(id)
        expanded = next
    }

    function setBypass(on) {
        if (colorState.bypass !== on)
            colorState.bypass = on
    }

    function hslBandModified(param) {
        return (target.modifiedHslBands || []).indexOf(param) >= 0
    }

    function resetHslBand() {
        const band = hslBands[hslBand]
        colorState.resetHslBand(band.param, band.label)
    }

    function selectHslBand(i) {
        hslBand = i
        bandRepeater.itemAt(i)?.forceActiveFocus()
    }

    function hslTrackGradient(suffix) {
        const hue = hslBands[hslBand].hue
        const shade = d => Qt.hsla((((hue + d) % 360) + 360) % 360 / 360, 1, 0.5, 1)
        if (suffix === "Hue")
            return [shade(-30), shade(0), shade(30)]
        if (suffix === "Sat")
            return [Qt.hsla(0, 0, 0.5, 1), shade(0)]
        return [Theme.c.colorGradeTracks.luma[0], shade(0), Theme.c.colorGradeTracks.luma[1]]
    }

    function resetCurrentTab() {
        if (tab === 0) {
            colorState.resetGroups(["Light", "Color", "Look"])
        } else if (tab === 1) {
            colorState.resetGroup("Wheels")
        } else if (tab === 2) {
            colorState.resetGroups(["Curves", "Hue Curves"])
        } else if (tab === 3) {
            colorState.resetGroup("HSL")
        }
    }

    focus: true
    activeFocusOnTab: true
    Shortcut {
        sequence: "\\"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: root.setBypass(!root.colorState.bypass)
    }

    onShownChanged: {
        if (!root.shown)
            root.setBypass(false)
    }
    function applyPanelRequest() {
        const request = ShellBridge.panelRequest
        if (!request || request.panel !== "color")
            return
        ShellBridge.panelRequest = null
        if (request.tab >= 0)
            root.tab = request.tab
    }
    Connections {
        target: ShellBridge
        function onPanelRequestChanged() { root.applyPanelRequest() }
    }
    Component.onCompleted: {
        applyPanelRequest()
        const specs = {}
        const catalog = colorState.finishingCatalog()
        for (let i = 0; i < catalog.length; ++i)
            specs[catalog[i].id] = colorState.finishingParamSpecs(catalog[i].id)
        finishingSpecs = specs
    }
    Component.onDestruction: setBypass(false)

    TapHandler {
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            if (!content.activeFocus)
                root.forceActiveFocus()
        }
    }

    Timer {
        id: statusTimer
        interval: 3000
        onTriggered: root.applyStatus = ""
    }

    component SectionHeader: Item {
        id: section
        property string title: ""
        property bool modified: true
        signal resetRequested()
        width: parent ? parent.width : 200
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: section.title
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }

        IconButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset %1").arg(section.title)
            buttonSize: Theme.controlHeightSm
            enabled: section.modified
            onClicked: section.resetRequested()
        }
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: root.hasTarget ? qsTr("Color: %1").arg(root.target.label) : qsTr("Color")
    }

    Flow {
        id: actions
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingMd
        visible: root.hasTarget

        SearchAnchor {
            searchId: "color-compare"
            title: qsTr("Compare")
            keywords: "before after bypass original"
            searchTarget: "panel:color#color-compare"
            target: compareButton
        }
        ThemedButton {
            id: compareButton
            text: qsTr("Compare")
            variant: root.colorState.bypass ? "primary" : "secondary"
            tooltip: qsTr("Click to toggle the original. Hold to see it while pressed (\\ key)")
            glyph: Theme.icons.eye
            // A short press flips the latch; a long press is a peek that ends on release.
            onPressed: {
                root.compareWasOn = root.colorState.bypass
                root.compareDownAt = Date.now()
                root.setBypass(true)
            }
            onReleased: {
                const held = Date.now() - root.compareDownAt
                root.setBypass(held > 300 ? root.compareWasOn : !root.compareWasOn)
            }
            onCanceled: root.setBypass(root.compareWasOn)
        }

        SearchAnchor {
            searchId: "color-apply-all"
            title: qsTr("Apply to all clips")
            keywords: "grade timeline all clips copy"
            searchTarget: "panel:color#color-apply-all"
            target: applyAllButton
        }
        ThemedButton {
            id: applyAllButton
            text: qsTr("Apply to all…")
            enabled: !!root.target.canApplyToAll
            tooltip: root.target.canApplyToAll
                     ? qsTr("Copy this grade to every video, image and other visual clip on the timeline")
                     : qsTr("This grade already applies to the whole timeline. Select a clip to copy its grade to the others.")
            onClicked: applyDialog.open()
        }

        IconButton {
            id: resetButton
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset")
            onClicked: resetMenu.popup()

            ThemedContextMenu {
                id: resetMenu
                ThemedMenuItem {
                    text: qsTr("Reset everything")
                    onTriggered: root.colorState.resetAll()
                }
                ThemedMenuItem {
                    visible: root.tab < 4
                    height: visible ? implicitHeight : 0
                    text: qsTr("Reset this tab")
                    onTriggered: root.resetCurrentTab()
                }
            }
        }

        Text {
            visible: root.applyStatus.length > 0
            height: compareButton.height
            verticalAlignment: Text.AlignVCenter
            text: root.applyStatus
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Column {
        id: notes
        anchors.top: actions.visible ? actions.bottom : header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        anchors.topMargin: height > 0 ? Theme.spacingMd : 0
        spacing: Theme.spacingSm
        visible: root.hasTarget

        Text {
            width: parent.width
            visible: (root.target.selectionCount ?? 0) > 1
            wrapMode: Text.WordWrap
            text: qsTr("Editing \"%1\" only. Use Apply to all to copy its grade to other clips.").arg(root.target.label)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        RowLayout {
            width: parent.width
            visible: root.colorState.bypass
            spacing: Theme.spacingMd

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Showing the original. Your grade is hidden until you turn Compare off.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedButton {
                text: qsTr("Show grade")
                onClicked: root.setBypass(false)
            }
        }

        RowLayout {
            width: parent.width
            visible: !!root.target.hasGrade && root.target.gradeEnabled === false
            spacing: Theme.spacingMd

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The grade is turned off.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedButton {
                text: qsTr("Turn on")
                onClicked: root.colorState.enableGrade()
            }
        }

        RowLayout {
            width: parent.width
            visible: !!root.target.timelineGradeId
            spacing: Theme.spacingMd

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Timeline grade also applies.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedButton {
                text: qsTr("Select it")
                onClicked: root.colorState.selectTimelineGrade()
            }
        }
    }

    Flow {
        id: tabRow
        anchors.top: notes.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm
        visible: root.hasTarget

        Repeater {
            model: root.tabs
            delegate: ThemedChip {
                required property int index
                required property string modelData
                text: modelData
                selected: root.tab === index
                onClicked: root.tab = index
            }
        }
    }

    Flickable {
        id: body
        anchors.top: tabRow.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.hasTarget
        clip: true
        contentWidth: width
        contentHeight: content.height + Theme.spacingLg * 2
        boundsBehavior: Flickable.StopAtBounds
        property int dragLocks: 0
        interactive: dragLocks === 0
        ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

        FocusScope {
            id: content
            x: Theme.spacingMd
            width: body.width - Theme.spacingMd * 2
            height: Math.max(basicTab.height, wheelsTab.height, curvesTab.height, hslTab.height, finishingTab.height)
            opacity: root.colorState.bypass ? 0.5 : 1

            GridLayout {
                id: basicTab
                width: parent.width
                columns: root.wide ? 2 : 1
                columnSpacing: Theme.spacingXl
                rowSpacing: Theme.spacingMd
                visible: root.tab === 0
                height: visible ? implicitHeight : 0

                Column {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 440
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingMd

                    SectionHeader {
                        title: qsTr("Light")
                        modified: root.lightModified
                        onResetRequested: root.colorState.resetGroup("Light")
                        SearchAnchor {
                            searchId: "color-exposure"
                            title: qsTr("Exposure")
                            keywords: "light brightness"
                            searchTarget: "panel:color#tab=0/color-exposure"
                        }
                        SearchAnchor {
                            searchId: "color-contrast"
                            title: qsTr("Contrast")
                            keywords: "light"
                            searchTarget: "panel:color#tab=0/color-contrast"
                        }
                        SearchAnchor {
                            searchId: "color-highlights"
                            title: qsTr("Highlights")
                            keywords: "light bright"
                            searchTarget: "panel:color#tab=0/color-highlights"
                        }
                        SearchAnchor {
                            searchId: "color-shadows"
                            title: qsTr("Shadows")
                            keywords: "light dark"
                            searchTarget: "panel:color#tab=0/color-shadows"
                        }
                        SearchAnchor {
                            searchId: "color-whites"
                            title: qsTr("Whites")
                            keywords: "light"
                            searchTarget: "panel:color#tab=0/color-whites"
                        }
                        SearchAnchor {
                            searchId: "color-blacks"
                            title: qsTr("Blacks")
                            keywords: "light"
                            searchTarget: "panel:color#tab=0/color-blacks"
                        }
                    }
                    Repeater {
                        model: root.lightSliders
                        delegate: GradeSlider {
                            id: lightSlider
                            required property var modelData
                            width: parent.width
                            label: modelData.label
                            from: modelData.from
                            to: modelData.to
                            defaultValue: modelData.def
                            curvePower: modelData.curvePower ?? 1
                            animated: root.isAnimated(modelData.key)
                            value: root.liveOr(modelData.key, root.params[modelData.key] ?? modelData.def)
                            onBegan: root.colorState.beginEdit(qsTr("Adjust %1").arg(label))
                            onMoved: v => {
                                root.setLive(modelData.key, v)
                                root.colorState.setParam(modelData.key, v)
                            }
                            onEnded: {
                                root.colorState.commitEdit()
                                root.clearLive(modelData.key)
                            }
                        }
                    }
                }

                Column {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 440
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingMd

                    SectionHeader {
                        title: qsTr("Color")
                        modified: root.colorModified
                        onResetRequested: root.colorState.resetGroup("Color")
                        SearchAnchor {
                            searchId: "color-temperature"
                            title: qsTr("Temperature")
                            keywords: "white balance warm cool kelvin"
                            searchTarget: "panel:color#tab=0/color-temperature"
                        }
                        SearchAnchor {
                            searchId: "color-tint"
                            title: qsTr("Tint")
                            keywords: "white balance green magenta"
                            searchTarget: "panel:color#tab=0/color-tint"
                        }
                        SearchAnchor {
                            searchId: "color-saturation"
                            title: qsTr("Saturation")
                            keywords: "color intensity"
                            searchTarget: "panel:color#tab=0/color-saturation"
                        }
                        SearchAnchor {
                            searchId: "color-vibrance"
                            title: qsTr("Vibrance")
                            keywords: "color saturation"
                            searchTarget: "panel:color#tab=0/color-vibrance"
                        }
                    }
                    Repeater {
                        model: root.colorSliders
                        delegate: GradeSlider {
                            required property var modelData
                            width: parent.width
                            label: modelData.label
                            from: modelData.from
                            to: modelData.to
                            defaultValue: modelData.def
                            gradient: modelData.gradient || []
                            animated: root.isAnimated(modelData.key)
                            value: root.liveOr(modelData.key, root.params[modelData.key] ?? modelData.def)
                            onBegan: root.colorState.beginEdit(qsTr("Adjust %1").arg(label))
                            onMoved: v => {
                                root.setLive(modelData.key, v)
                                root.colorState.setParam(modelData.key, v)
                            }
                            onEnded: {
                                root.colorState.commitEdit()
                                root.clearLive(modelData.key)
                            }
                        }
                    }

                    SectionHeader {
                        title: qsTr("Look")
                        modified: root.lookModified
                        onResetRequested: root.colorState.resetGroup("Look")
                    }
                    Text {
                        width: parent.width
                        readonly property bool none: (root.params.look || "") === ""
                        wrapMode: none ? Text.WordWrap : Text.NoWrap
                        elide: none ? Text.ElideNone : Text.ElideRight
                        text: none ? qsTr("None. Choose a look in the Looks panel.") : root.target.lookLabel
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    GradeSlider {
                        width: parent.width
                        enabled: (root.params.look || "") !== ""
                        label: qsTr("Look Intensity")
                        from: 0
                        to: 1
                        defaultValue: 1
                        bipolar: false
                        animated: root.isAnimated("lookAmount")
                        value: root.liveOr("lookAmount", root.params.lookAmount ?? 1)
                        onBegan: root.colorState.beginEdit(qsTr("Adjust %1").arg(label))
                        onMoved: v => {
                            root.setLive("lookAmount", v)
                            root.colorState.setParam("lookAmount", v)
                        }
                        onEnded: {
                            root.colorState.commitEdit()
                            root.clearLive("lookAmount")
                        }
                    }
                }
            }

            Column {
                id: wheelsTab
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.tab === 1
                height: visible ? implicitHeight : 0

                SectionHeader {
                    title: qsTr("Wheels")
                    modified: root.wheelsModified
                    onResetRequested: root.colorState.resetGroup("Wheels")
                    SearchAnchor {
                        searchId: "color-wheels"
                        title: qsTr("Color wheels")
                        keywords: "lift gamma gain shadows midtones highlights"
                        searchTarget: "panel:color#tab=1/color-wheels"
                    }
                }
                Item {
                    width: parent.width
                    height: wheelGrid.implicitHeight

                    GridLayout {
                        id: wheelGrid
                        readonly property real gap: Theme.spacingLg
                        readonly property real minWheel: 112
                        readonly property real maxWheel: 240
                        readonly property int cols: wheelsTab.width >= 3 * minWheel + 2 * gap ? 3
                                                  : wheelsTab.width >= 2 * minWheel + gap ? 2 : 1
                        readonly property int rows: Math.ceil(3 / cols)
                        readonly property real availH: root.bodyH - Theme.controlHeightSm - Theme.spacingMd
                                                       - Theme.spacingLg * 2
                        readonly property real byWidth: (wheelsTab.width - (cols - 1) * gap) / cols
                        readonly property real byHeight: (availH - (rows - 1) * gap) / rows - wheelProbe.chromeHeight
                        readonly property real wheelSize: Math.floor(Math.max(minWheel, Math.min(maxWheel, byWidth, byHeight)))
                        anchors.horizontalCenter: parent.horizontalCenter
                        columns: cols
                        columnSpacing: gap
                        rowSpacing: gap

                        Repeater {
                            model: root.wheels
                            delegate: ColorWheel {
                                id: wheel
                                required property var modelData
                                required property int index
                                readonly property var vec: root.params[modelData.key] || [0, 0]
                                size: wheelGrid.wheelSize
                                Layout.columnSpan: wheelGrid.cols === 2 && index === 2 ? 2 : 1
                                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                                label: modelData.label
                                animated: root.isAnimated(modelData.key) || root.isAnimated(modelData.key + "Luma")
                                offset: Qt.point(vec[0], vec[1])
                                luma: root.liveOr(modelData.key + "Luma", root.params[modelData.key + "Luma"] ?? 0)
                                onBegan: root.colorState.beginEdit(qsTr("Adjust %1").arg(label))
                                onOffsetMoved: (x, y) => root.colorState.setVec2(modelData.key, x, y)
                                onLumaMoved: v => {
                                    root.setLive(modelData.key + "Luma", v)
                                    root.colorState.setParam(modelData.key + "Luma", v)
                                }
                                onEnded: {
                                    root.colorState.commitEdit()
                                    root.clearLive(modelData.key + "Luma")
                                }
                            }
                        }
                    }
                }
                ColorWheel {
                    id: wheelProbe
                    visible: false
                    size: 100
                }
            }

            Column {
                id: curvesTab
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.tab === 2
                height: visible ? implicitHeight : 0

                GridLayout {
                    id: curveGrid
                    width: parent.width
                    readonly property int cols: width >= 2 * 240 + Theme.spacingLg ? 2 : 1
                    readonly property real colW: (width - (cols - 1) * Theme.spacingLg) / cols
                    readonly property real availH: root.bodyH - Theme.controlHeightSm * 2 - Theme.spacingMd * 3
                                                   - Theme.spacingLg * 2 - Theme.fontSizeXs * 4
                    readonly property real plot: Math.floor(Math.max(180, Math.min(colW, 440, availH)))
                    columns: cols
                    columnSpacing: Theme.spacingLg
                    rowSpacing: Theme.spacingMd

                    Column {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: curveGrid.colW
                        spacing: Theme.spacingMd

                        SectionHeader {
                            title: qsTr("Curves")
                            modified: root.curvesAreModified
                            onResetRequested: root.colorState.resetGroup("Curves")
                            SearchAnchor {
                                searchId: "color-curves"
                                title: qsTr("Curves")
                                keywords: "rgb curve"
                                searchTarget: "panel:color#tab=2/color-curves"
                            }
                        }
                        CurveEditor {
                            x: (parent.width - width) / 2
                            width: curveGrid.plot
                            height: implicitHeight
                            curves: ({ master: root.params.curveMaster || "", r: root.params.curveR || "",
                                      g: root.params.curveG || "", b: root.params.curveB || "" })
                            histogram: root.colorState.scopes.active
                                       ? "image://color/scope/histogram?s=" + root.colorState.scopes.serial : ""
                            onBegan: root.colorState.beginEdit(qsTr("Adjust curves"))
                            onChanged: (channel, json) => root.colorState.setText(root.curveKeys[channel], json)
                            onEnded: root.colorState.commitEdit()
                        }
                    }

                    Column {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: curveGrid.colW
                        spacing: Theme.spacingMd

                        SectionHeader {
                            title: qsTr("Hue Curves")
                            modified: root.hueCurvesAreModified
                            onResetRequested: root.colorState.resetGroup("Hue Curves")
                            SearchAnchor {
                                searchId: "color-hue-curves"
                                title: qsTr("Hue curves")
                                keywords: "hue vs hue saturation luminance curve"
                                searchTarget: "panel:color#tab=2/color-hue-curves"
                            }
                        }
                        CurveEditor {
                            x: (parent.width - width) / 2
                            width: curveGrid.plot
                            height: implicitHeight
                            hueMode: true
                            curves: ({ hue: root.params.curveHueHue || "", sat: root.params.curveHueSat || "",
                                      lum: root.params.curveHueLum || "" })
                            onBegan: root.colorState.beginEdit(qsTr("Adjust hue curves"))
                            onChanged: (channel, json) => root.colorState.setText(root.hueCurveKeys[channel], json)
                            onEnded: root.colorState.commitEdit()
                        }
                    }
                }
            }

            Column {
                id: hslTab
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.tab === 3
                height: visible ? implicitHeight : 0

                SectionHeader {
                    title: qsTr("HSL")
                    modified: root.hslModified
                    onResetRequested: root.colorState.resetGroup("HSL")
                    SearchAnchor {
                        searchId: "color-hsl"
                        title: qsTr("HSL")
                        keywords: "hsl hue saturation luminance color band red orange yellow green aqua blue purple magenta"
                        searchTarget: "panel:color#tab=3/color-hsl"
                    }
                }
                Column {
                    id: hslCol
                    width: Math.min(parent.width, 480)
                    spacing: Theme.spacingMd

                    Grid {
                        id: swatchGrid
                        readonly property real cell: Theme.controlHeightSm + 8
                        columns: hslCol.width >= 8 * cell + 7 * Theme.spacingSm ? 8 : 4
                        spacing: Theme.spacingSm
                        Repeater {
                            id: bandRepeater
                            model: root.hslBands
                            delegate: Item {
                                id: swatch
                                required property var modelData
                                required property int index
                                readonly property real inset: 4
                                readonly property bool selected: root.hslBand === index
                                width: Theme.controlHeightSm + inset * 2
                                height: width + 7
                                activeFocusOnTab: true
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData.label
                                Accessible.checkable: true
                                Accessible.checked: selected

                                Rectangle {
                                    width: swatch.width
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Theme.focusRing
                                    visible: swatch.selected || swatch.activeFocus
                                }

                                Rectangle {
                                    x: swatch.inset
                                    y: swatch.inset
                                    width: Theme.controlHeightSm
                                    height: width
                                    radius: width / 2
                                    color: Theme.c.colorGradeBands[swatch.modelData.name]
                                }

                                Rectangle {
                                    x: (swatch.width - width) / 2
                                    y: swatch.width + 1
                                    width: 5
                                    height: width
                                    radius: width / 2
                                    color: Theme.accentOnPanel
                                    visible: root.hslBandModified(swatch.modelData.param)
                                }

                                HoverHandler {
                                    id: swatchHover
                                }

                                ThemedToolTip {
                                    visible: swatchHover.hovered
                                    text: swatch.modelData.label
                                }

                                TapHandler {
                                    onTapped: root.selectHslBand(swatch.index)
                                }

                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Left)
                                        root.selectHslBand(Math.max(0, swatch.index - 1))
                                    else if (event.key === Qt.Key_Right)
                                        root.selectHslBand(Math.min(root.hslBands.length - 1, swatch.index + 1))
                                    else if (event.key === Qt.Key_Up)
                                        root.selectHslBand(Math.max(0, swatch.index - swatchGrid.columns))
                                    else if (event.key === Qt.Key_Down)
                                        root.selectHslBand(Math.min(root.hslBands.length - 1, swatch.index + swatchGrid.columns))
                                    else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                        root.hslBand = swatch.index
                                    else
                                        return
                                    event.accepted = true
                                }
                            }
                        }
                    }
                    Item {
                        width: parent.width
                        height: Theme.controlHeightSm

                        Text {
                            anchors.left: parent.left
                            anchors.right: bandReset.left
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: root.hslBands[root.hslBand].label
                            color: Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }

                        IconButton {
                            id: bandReset
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.reset
                            tooltip: qsTr("Reset %1").arg(root.hslBands[root.hslBand].label)
                            buttonSize: Theme.controlHeightSm
                            enabled: root.hslBandModified(root.hslBands[root.hslBand].param)
                            onClicked: root.resetHslBand()
                        }
                    }
                    Repeater {
                        model: root.hslSliders
                        delegate: GradeSlider {
                            id: hslSlider
                            required property var modelData
                            readonly property string paramKey: "hsl" + root.hslBands[root.hslBand].param + modelData.suffix
                            width: parent.width
                            label: modelData.label
                            from: -1
                            to: 1
                            defaultValue: 0
                            gradient: root.hslTrackGradient(modelData.suffix)
                            animated: root.isAnimated(paramKey)
                            value: root.liveOr(paramKey, root.params[paramKey] ?? 0)
                            onBegan: root.colorState.beginEdit(qsTr("Adjust %1").arg(label))
                            onMoved: v => {
                                root.setLive(hslSlider.paramKey, v)
                                root.colorState.setParam(hslSlider.paramKey, v)
                            }
                            onEnded: {
                                root.colorState.commitEdit()
                                root.clearLive(hslSlider.paramKey)
                            }
                        }
                    }
                }
            }

            Column {
                id: finishingTab
                width: parent.width
                spacing: Theme.spacingMd
                visible: root.tab === 4
                height: visible ? implicitHeight : 0

                SearchAnchor {
                    searchId: "color-film-grain"
                    title: qsTr("Film Grain")
                    keywords: "grain noise film"
                    searchTarget: "panel:color#tab=4/color-film-grain"
                }
                SearchAnchor {
                    searchId: "color-glow"
                    title: qsTr("Glow")
                    keywords: "bloom glow halation"
                    searchTarget: "panel:color#tab=4/color-glow"
                }
                SearchAnchor {
                    searchId: "color-vignette"
                    title: qsTr("Vignette")
                    keywords: "dark corners edges"
                    searchTarget: "panel:color#tab=4/color-vignette"
                }
                SearchAnchor {
                    searchId: "color-chromatic-aberration"
                    title: qsTr("Chromatic Aberration")
                    keywords: "color fringe lens"
                    searchTarget: "panel:color#tab=4/color-chromatic-aberration"
                }
                Repeater {
                    model: Object.keys(root.finishingNames)
                    delegate: Rectangle {
                        id: card
                        required property string modelData
                        readonly property var entry: root.finishingEntry(modelData)
                        readonly property bool open: entry.present && root.isExpanded(modelData)
                        width: Math.min(finishingTab.width, 560)
                        height: cardColumn.height + Theme.spacingMd * 2
                        radius: Theme.radiusMd
                        color: Theme.panelSecondaryBg

                        Column {
                            id: cardColumn
                            x: Theme.spacingMd
                            y: Theme.spacingMd
                            width: parent.width - Theme.spacingMd * 2
                            spacing: Theme.spacingMd

                            Item {
                                width: parent.width
                                height: Theme.controlHeight

                                TapHandler {
                                    enabled: card.entry.present
                                    onTapped: root.toggleExpanded(card.modelData)
                                }

                                IconGlyph {
                                    id: chevron
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: card.entry.present
                                    glyph: card.open ? Theme.icons.chevronDown : Theme.icons.chevronRight
                                    iconSize: Theme.iconSizeSm
                                }

                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: card.entry.present ? chevron.iconSize + Theme.spacingSm : 0
                                    anchors.right: controls.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    text: root.finishingNames[card.modelData]
                                    color: Theme.panelForeground
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSm
                                }

                                Row {
                                    id: controls
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.spacingSm

                                    ThemedSwitch {
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: card.entry.present && card.entry.enabled
                                        onToggled: {
                                            if (!card.entry.present)
                                                root.colorState.toggleFinishing(card.modelData, true)
                                            else
                                                root.colorState.setFinishingEnabled(card.modelData, checked)
                                        }
                                    }
                                    IconButton {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: card.entry.present
                                        glyph: Theme.icons.x
                                        tooltip: qsTr("Remove")
                                        buttonSize: Theme.controlHeightSm
                                        onClicked: root.colorState.toggleFinishing(card.modelData, false)
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                visible: !card.entry.present
                                wrapMode: Text.WordWrap
                                text: root.finishingHints[card.modelData]
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }

                            Repeater {
                                model: card.open ? (root.finishingSpecs[card.modelData] || []) : []
                                delegate: Loader {
                                    id: paramLoader
                                    required property var modelData
                                    readonly property var spec: modelData
                                    readonly property var cardEntry: card.entry
                                    readonly property string fxId: card.modelData
                                    readonly property string liveKey: card.modelData + ":" + spec.key
                                    readonly property var current: card.entry.params[spec.key]
                                    width: cardColumn.width
                                    sourceComponent: spec.type === "bool" ? boolParam
                                                     : spec.type === "color" ? colorParam
                                                     : (spec.type === "float" || spec.type === "int")
                                                       ? floatParam : null
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: floatParam
        GradeSlider {
            readonly property var loader: parent
            label: loader.spec.label
            from: loader.spec.min
            to: loader.spec.max
            defaultValue: Number(loader.spec.default)
            bipolar: false
            animated: root.finishingAnimated(loader.cardEntry, loader.spec.key)
            stepSize: loader.spec.step > 0 ? loader.spec.step : (to - from) / 100
            value: root.liveOr(loader.liveKey, loader.current ?? defaultValue)
            onBegan: root.colorState.beginFinishingEdit(
                         loader.fxId, qsTr("Adjust %1").arg(label))
            onMoved: v => {
                root.setLive(loader.liveKey, v)
                root.colorState.setFinishingParam(loader.fxId, loader.spec.key, v)
            }
            onEnded: {
                root.colorState.commitEdit()
                root.clearLive(loader.liveKey)
            }
        }
    }

    Component {
        id: boolParam
        RowLayout {
            readonly property var loader: parent
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: loader.spec.label
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedSwitch {
                checked: !!loader.current
                onToggled: root.colorState.setFinishingParam(
                               loader.fxId, loader.spec.key, checked ? 1 : 0)
            }
        }
    }

    Component {
        id: colorParam
        RowLayout {
            readonly property var loader: parent
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: loader.spec.label
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ColorSwatchField {
                hex: typeof loader.current === "string" ? loader.current : "#ffffff"
                onEdited: value => root.colorState.setFinishingColor(
                              loader.fxId, loader.spec.key, value)
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: !root.hasTarget
        glyph: Theme.icons.sliders
        title: qsTr("Select a clip to color grade it")
        hint: qsTr("Or grade the whole timeline at once.")
        actionText: qsTr("Grade whole timeline")
        onActionTriggered: root.colorState.gradeWholeTimeline()
    }

    ThemedDialog {
        id: applyDialog
        title: qsTr("Apply to all clips")
        acceptText: qsTr("Apply")
        onAccepted: {
            const n = root.colorState.applyToAll(root.includeFinishing)
            root.applyStatus = qsTr("Applied to %n clip(s)", "", n)
            statusTimer.restart()
        }

        contentItem: ThemedCheckBox {
            text: qsTr("Include finishing effects")
            checked: root.includeFinishing
            onToggled: root.includeFinishing = checked
        }
    }
}
