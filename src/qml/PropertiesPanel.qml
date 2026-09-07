import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"
import "components/properties"

PanelFrame {
    id: root

    // Android bottom sheet: keep the inspector tab rail but drop the panel border
    // chrome that fights the sheet frame.
    property bool sheetMode: false
    border.width: sheetMode ? 0 : 1
    radius: sheetMode ? 0 : Theme.radiusSm
    color: sheetMode ? "transparent" : Theme.panelBackground

    // Raised by the Effects / Audio empty states; Main wires them to the
    // assets panel so the browse CTAs actually take the user somewhere.
    signal browseEffectsRequested()

    // -1 while the prompt is saving the whole stack; otherwise the one effect it was raised on.
    property int savePresetEffectIndex: -1
    signal browseAudioEffectsRequested()

    // selectedClipData is a QVariantMap; key the binding on an explicit revision
    // so nested fields such as effects refresh after project edits.
    property int clipDataRevision: 0
    property int previousTab: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!root.clipData && Object.keys(root.clipData).length > 0
    readonly property var transition: EditorState.selectedTransitionData
    readonly property bool hasTransitionSelection: !!transition && Object.keys(transition).length > 0
    readonly property int transitionTabIndex: tabIndexOf("transition")
    readonly property string clipKind: hasSelection ? (root.clipData.kind || "") : ""
    readonly property bool hasTextStyle: hasSelection
                                         && (clipKind === "text" || clipKind === "subtitle")
                                         && !!root.clipData.textStyle

    property int activeTab: 0
    readonly property string currentTabId: tabsModel.get(activeTab).tabId

    onActiveTabChanged: {
        if (tabsModel.get(root.previousTab).tabId === "transition")
            transitionInspector.commitEdits()
        root.previousTab = activeTab
        transitionInspector.refreshFields()
    }

    // Kept for SubtitleEditor, which formats cue times through it.
    function formatSeconds(value) {
        return Number(value || 0).toFixed(2)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
            root.syncShapeTab()
            root.syncTextTab()
            root.syncAnimationTab()
            root.syncStabilizeTab()
            root.syncMasksTab()
            root.syncActiveTab()
        }
        function onSelectedClipDataChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
        }
        function onSelectedTransitionDataChanged() {
            if (root.hasTransitionSelection)
                root.activeTab = root.transitionTabIndex
        }
        function onTracksChanged() {
            root.clipDataRevision++
        }
    }

    Component.onCompleted: {
        root.syncSubtitlesTab()
    }

    // ListElement only accepts literal values; qsTr() calls are not
    // evaluated. Labels are translated via tabLabels below.
    property var tabLabels: ({
        "general": qsTr("General"),
        "text": qsTr("Text"),
        "shape": qsTr("Shape"),
        "subtitles": qsTr("Subtitles"),
        "transform": qsTr("Transform"),
        "stabilize": qsTr("Stabilization"),
        "animation": qsTr("Animation"),
        "audio": qsTr("Audio"),
        "speed": qsTr("Speed"),
        "blending": qsTr("Blending"),
        "masks": qsTr("Masks"),
        "effects": qsTr("Effects"),
        "audioEffects": qsTr("Audio FX"),
        "transition": qsTr("Transition")
    })

    // Rail order: clip identity → geometry/timing → compositing / FX.
    // Contextual tabs (text / shape / captions) share the first group so a
    // separator still appears after General when they are hidden.
    // `group` drives hairlines between the next *visible* tab of a different group.
    ListModel {
        id: tabsModel
        ListElement { tabId: "general"; icon: 0; group: 0 }
        ListElement { tabId: "text"; icon: 1; group: 0 }
        ListElement { tabId: "shape"; icon: 2; group: 0 }
        ListElement { tabId: "subtitles"; icon: 3; group: 0 }
        ListElement { tabId: "transform"; icon: 4; group: 1 }
        ListElement { tabId: "stabilize"; icon: 5; group: 1 }
        ListElement { tabId: "animation"; icon: 6; group: 1 }
        ListElement { tabId: "audio"; icon: 7; group: 1 }
        ListElement { tabId: "speed"; icon: 8; group: 1 }
        ListElement { tabId: "blending"; icon: 9; group: 2 }
        ListElement { tabId: "masks"; icon: 10; group: 2 }
        ListElement { tabId: "effects"; icon: 11; group: 2 }
        ListElement { tabId: "audioEffects"; icon: 12; group: 2 }
        ListElement { tabId: "transition"; icon: 13; group: 2 }
    }
    property var tabIcons: [
        Theme.icons.info,
        Theme.icons.type,
        Theme.icons.shapes,
        Theme.icons.captions,
        Theme.icons.maximize,
        Theme.icons.locateFixed,
        Theme.icons.sparkles,
        Theme.icons.volumeHigh,
        Theme.icons.gauge,
        Theme.icons.blend,
        Theme.icons.mask,
        Theme.icons.wand,
        Theme.icons.audioLines,
        Theme.icons.chevronsRight
    ]

    function tabVisible(tabId) {
        // An adjustment carries one kind of payload, so it shows the inspector for that and
        // nothing else. It used to offer Transform and Speed, which the compositor ignores for
        // an adjustment — the layer is the whole canvas, and there is no source to retime.
        if (root.clipKind === "adjustment") {
            const kind = root.clipData.adjustmentKind || "videoEffects"
            if (tabId === "general" || tabId === "blending")
                return true
            if (tabId === "effects")
                return kind === "videoEffects"
            if (tabId === "audioEffects")
                return kind === "audioEffects"
            if (tabId === "masks")
                return kind === "mask" || kind === "videoEffects"
            return false
        }
        if (tabId === "subtitles")
            return root.clipKind === "subtitle"
        if (tabId === "shape")
            return root.clipKind === "shape"
        if (tabId === "text")
            return root.hasTextStyle
        if (tabId === "animation")
            return root.clipKind === "video" || root.clipKind === "image"
                   || root.clipKind === "shape" || root.clipKind === "text"
                   || root.clipKind === "audio"
        if (tabId === "stabilize")
            return root.clipKind === "video"
        // A media clip's masks live on adjustments pinned to it, and those are what you select to
        // edit them — the tab used to be the only way to *create* one, which the assets panel now
        // does. Offering it here as well would edit only the first mask of a stack.
        if (tabId === "masks")
            return false
        return true
    }

    // Hairline after this index when the next visible tab belongs to another group.
    function showSeparatorAfter(index) {
        const cur = tabsModel.get(index)
        if (!tabVisible(cur.tabId))
            return false
        for (let i = index + 1; i < tabsModel.count; ++i) {
            const next = tabsModel.get(i)
            if (!tabVisible(next.tabId))
                continue
            return next.group !== cur.group
        }
        return false
    }

    function tabIndexOf(id) {
        for (let i = 0; i < tabsModel.count; i++) {
            if (tabsModel.get(i).tabId === id)
                return i
        }
        return -1
    }
    readonly property int subtitlesTabIndex: tabIndexOf("subtitles")
    readonly property int shapeTabIndex: tabIndexOf("shape")
    readonly property int textTabIndex: tabIndexOf("text")
    readonly property int animationTabIndex: tabIndexOf("animation")
    readonly property int stabilizeTabIndex: tabIndexOf("stabilize")
    readonly property int masksTabIndex: tabIndexOf("masks")

    // The Subtitles tab only exists for subtitle clips, so leaving it selected would show a blank
    // pane once the selection moves off one. Selecting a subtitle clip never opens the tab by
    // itself: it took over whatever pane you were working in, and with it the timeline lane.
    function syncSubtitlesTab() {
        if (root.subtitlesTabIndex >= 0 && root.activeTab === root.subtitlesTabIndex
                && root.clipKind !== "subtitle")
            root.activeTab = 0
    }

    // The Shape tab is hidden for every other clip kind, so leaving it selected would show a blank
    // pane after the selection moves off a shape.
    function syncShapeTab() {
        if (root.shapeTabIndex >= 0 && root.activeTab === root.shapeTabIndex
                && root.clipKind !== "shape")
            root.activeTab = 0
    }

    // Same for the Text tab, which only exists for clips carrying a text style.
    function syncTextTab() {
        if (root.textTabIndex >= 0 && root.activeTab === root.textTabIndex && !root.hasTextStyle)
            root.activeTab = 0
    }

    function syncAnimationTab() {
        if (root.animationTabIndex >= 0 && root.activeTab === root.animationTabIndex
                && !root.tabVisible("animation"))
            root.activeTab = 0
    }

    function syncStabilizeTab() {
        if (root.stabilizeTabIndex >= 0 && root.activeTab === root.stabilizeTabIndex
                && !root.tabVisible("stabilize"))
            root.activeTab = 0
    }

    // Selecting a mask clip is how you edit a mask now, so it opens the pane that edits one —
    // the same courtesy selecting a transition already gets. Only for a Mask adjustment: a
    // video-effects adjustment also offers the tab, but its effect stack is what you came for.
    function syncMasksTab() {
        if (root.masksTabIndex < 0)
            return
        if (root.clipKind === "adjustment" && root.clipData.adjustmentKind === "mask")
            root.activeTab = root.masksTabIndex
    }

    // The catch-all behind the per-tab syncs above: an adjustment hides most of the rail, so any
    // tab open on the clip you came from can vanish under you and leave an empty pane.
    function syncActiveTab() {
        if (!root.tabVisible(root.currentTabId))
            root.activeTab = 0
    }

    // Tell the timeline to show its subtitle-cue lane only while the Subtitles tab is open.
    Binding {
        target: EditorState
        property: "subtitleEditing"
        value: root.currentTabId === "subtitles" && root.clipKind === "subtitle"
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(260, root.width - 32)
        visible: !root.hasSelection
        spacing: 16

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: Theme.panelBorder

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.sliders
                iconSize: 22
                iconColor: Theme.mutedForeground
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("It's empty here")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.sheetMode
                  ? qsTr("Tap a clip on the timeline to edit its properties")
                  : qsTr("Click a clip on the timeline to edit its properties")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Item {
        id: content
        anchors.fill: parent
        visible: root.hasSelection

        // === Phone: a labelled tab strip across the top ===============================
        // The desktop rail below is a column of thirteen unlabelled icons. Inside a
        // bottom sheet it has to scroll on its own axis, stands a second scrollbar
        // next to the content's, and spends a fifth of an already narrow sheet
        // saying nothing that can be read at a glance.
        Item {
            id: tabStripHost
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: root.sheetMode
            // Icon over label: the label alone identifies a tab, but the icon is
            // what the eye finds again after the first time.
            readonly property real tabChipHeight: 58
            height: visible ? tabChipHeight + Theme.spacingLg * 2 : 0

            // Keeps the selected tab on screen when it is picked from off the end,
            // and when the visible set changes with the clip kind.
            function ensureTabVisible() {
                const item = tabStripRepeater.itemAt(root.activeTab)
                if (!item || !item.visible)
                    return
                const left = tabStripRow.x + item.x
                const right = left + item.width
                const maxX = Math.max(0, tabStrip.contentWidth - tabStrip.width)
                if (left - Theme.pagePadding < tabStrip.contentX)
                    tabStrip.contentX = Math.max(0, left - Theme.pagePadding)
                else if (right + Theme.pagePadding > tabStrip.contentX + tabStrip.width)
                    tabStrip.contentX = Math.min(maxX, right + Theme.pagePadding - tabStrip.width)
            }

            Connections {
                target: root
                // callLater: a tab change can also change which tabs exist, and the
                // Row has not repositioned yet when the signal arrives.
                function onActiveTabChanged() { Qt.callLater(tabStripHost.ensureTabVisible) }
                function onClipKindChanged() { Qt.callLater(tabStripHost.ensureTabVisible) }
            }

            Flickable {
                id: tabStrip
                anchors.fill: parent
                contentWidth: tabStripRow.width + Theme.pagePadding * 2
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Row {
                    id: tabStripRow
                    x: Theme.pagePadding
                    height: tabStrip.height
                    spacing: Theme.spacingSm

                    Repeater {
                        id: tabStripRepeater
                        model: tabsModel
                        // Not a ThemedChip: that one's contentItem is a single Text,
                        // and stacking a glyph over the label needs two rows.
                        delegate: AbstractButton {
                            id: tabChip
                            required property int index
                            required property var model

                            readonly property bool current: root.activeTab === tabChip.index

                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.tabVisible(tabChip.model.tabId)
                            height: tabStripHost.tabChipHeight
                            implicitWidth: Math.max(Theme.controlHeight + Theme.spacing2xl,
                                                    tabChipLabel.implicitWidth + Theme.spacingXl * 2)
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus

                            Accessible.role: Accessible.PageTab
                            Accessible.name: root.tabLabels[tabChip.model.tabId]
                            Accessible.checked: tabChip.current

                            scale: tabChip.down ? Theme.pressScale : 1.0

                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                            }

                            onClicked: {
                                Haptics.select()
                                root.activeTab = tabChip.index
                            }

                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: tabChip.current
                                       ? Theme.primary
                                       : (tabChip.down ? Theme.panelMuted : Theme.panelAccent)
                                border.width: Theme.borderWidth
                                border.color: tabChip.current ? Theme.primary : Theme.panelBorder

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }
                                Behavior on border.color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: Theme.borderWidthFocus
                                    border.color: Theme.focusRing
                                    visible: tabChip.visualFocus
                                }
                            }

                            // Wrapped in an Item so the two rows sit centred in the
                            // pill; a bare Column would stack them against its top.
                            contentItem: Item {
                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.spacingSm

                                    IconGlyph {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        glyph: root.tabIcons[tabChip.model.icon]
                                        iconSize: Theme.iconSizeLg
                                        iconColor: tabChip.current ? Theme.primaryForeground
                                                                   : Theme.panelForeground
                                    }

                                    Text {
                                        id: tabChipLabel
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.tabLabels[tabChip.model.tabId]
                                        color: tabChip.current ? Theme.primaryForeground
                                                               : Theme.panelForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        font.weight: tabChip.current ? Font.Medium : Font.Normal
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.borderWidth
                color: Theme.panelBorder
            }
        }

        // === Desktop: vertical icon rail + tab content ================================
        // Up/Down move between tabs once the rail has focus.
        // Flickable so short panel heights can still reach lower icons.
        Flickable {
            id: propertiesTabRail
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: root.sheetMode ? 0 : Theme.tabRailWidth
            visible: !root.sheetMode
            contentWidth: width
            contentHeight: propertiesTabRailColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: propertiesTabRail.contentHeight > propertiesTabRail.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            Accessible.role: Accessible.PageTabList

            Keys.onUpPressed: function(event) {
                let next = root.activeTab
                for (let step = 0; step < tabsModel.count; ++step) {
                    next = (next - 1 + tabsModel.count) % tabsModel.count
                    if (root.tabVisible(tabsModel.get(next).tabId)) {
                        root.activeTab = next
                        break
                    }
                }
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                let next = root.activeTab
                for (let step = 0; step < tabsModel.count; ++step) {
                    next = (next + 1) % tabsModel.count
                    if (root.tabVisible(tabsModel.get(next).tabId)) {
                        root.activeTab = next
                        break
                    }
                }
                event.accepted = true
            }

            Column {
                id: propertiesTabRailColumn
                width: parent.width
                topPadding: Theme.spacingSm
                spacing: Theme.spacingXs

                Repeater {
                    model: tabsModel
                    delegate: Column {
                        required property int index
                        required property var model

                        width: parent.width
                        spacing: 0
                        // Collapse so hidden contextual tabs leave no rail gap.
                        visible: root.tabVisible(model.tabId)
                        height: visible ? implicitHeight : 0

                        IconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: root.tabIcons[model.icon]
                            variant: "ghost"
                            tooltip: root.tabLabels[model.tabId]
                            active: root.activeTab === index
                            onClicked: root.activeTab = index

                            Accessible.role: Accessible.PageTab
                            Accessible.name: root.tabLabels[model.tabId]
                            Accessible.checked: root.activeTab === index
                        }

                        Item {
                            visible: root.showSeparatorAfter(index)
                            width: parent.width
                            height: visible ? Theme.spacingLg + Theme.borderWidth : 0

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.iconSizeSm
                                height: Theme.borderWidth
                                radius: height / 2
                                color: Theme.panelBorder
                                opacity: 0.85
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: railDivider
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: propertiesTabRail.right
            width: root.sheetMode ? 0 : Theme.borderWidth
            visible: !root.sheetMode
            color: Theme.panelBorder
        }

        Flickable {
            id: tabFlick
            anchors.top: tabStripHost.bottom
            anchors.left: railDivider.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.currentTabId !== "subtitles"
            contentWidth: width
            // Include topPadding so the last controls stay reachable (SettingsTab pattern).
            contentHeight: tabColumn.height + Theme.spacing3xl
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: tabFlick.contentHeight > tabFlick.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            // Tall tabs (Audio/Effects) leave contentY deep; reset when switching.
            Connections {
                target: root
                function onActiveTabChanged() {
                    tabFlick.contentY = 0
                    tabColumn.opacity = 0
                    tabFadeIn.restart()
                }
            }

            // Fade the new tab in rather than hard-cutting to it. Fade-in only,
            // not a crossfade: the inspectors share a Column, which excludes
            // invisible items from layout, so overlapping two of them would
            // double-count height and jump the panel mid-transition.
            NumberAnimation {
                id: tabFadeIn
                target: tabColumn
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }

            Column {
                id: tabColumn
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                // Sections need to read apart on a phone, where the whole pane is
                // about two of them tall.
                spacing: root.sheetMode ? Theme.spacing2xl : Theme.spacingXl
                topPadding: root.sheetMode ? Theme.spacing2xl : Theme.pagePadding

                Text {
                    // The strip above already names the tab, in a size you can read.
                    visible: !root.sheetMode
                    text: root.tabLabels[tabsModel.get(root.activeTab).tabId]
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                GeneralInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "general"
                }

                TextInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "text"
                }

                TransformInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "transform"
                }

                StabilizeInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "stabilize"
                }

                AnimationInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "animation"
                }

                AudioInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "audio"
                }

                SpeedFadeInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "speed"
                }

                TransitionInspector {
                    id: transitionInspector
                    width: tabColumn.width
                    visible: root.currentTabId === "transition"
                }

                BlendingInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "blending"
                }

                ShapeInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "shape"
                }

                MasksInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "masks"
                }

                EffectsInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "effects"
                    onBrowseEffectsRequested: root.browseEffectsRequested()
                    onSaveEffectPresetRequested: function(effectIndex) {
                        root.savePresetEffectIndex = effectIndex
                        effectPresetNameDialog.openWith(
                            effectIndex < 0 ? qsTr("Save effect preset")
                                            : qsTr("Save effect as preset"),
                            root.hasSelection ? (root.clipData.name || "") : "")
                    }
                }

                AudioEffectsInspector {
                    width: tabColumn.width
                    visible: root.currentTabId === "audioEffects"
                    onBrowseAudioEffectsRequested: root.browseAudioEffectsRequested()
                }
            }
        }

        // Full-height editor with its own internal cue list scrolling, so it
        // sits beside the tab Flickable rather than inside it.
        SubtitleEditor {
            anchors.top: tabStripHost.bottom
            anchors.left: railDivider.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.currentTabId === "subtitles"
            clip: root.hasSelection ? root.clipData : null
            formatSeconds: root.formatSeconds
        }
    }

    NameDialog {
        id: effectPresetNameDialog
        placeholder: qsTr("My look")
        onSubmitted: function(name) {
            if (root.savePresetEffectIndex < 0)
                EditorState.saveClipEffectsAsPreset(EditorState.selectedTrack,
                                                    EditorState.selectedClip, name)
            else
                EditorState.saveEffectAsPreset(EditorState.selectedTrack,
                                               EditorState.selectedClip,
                                               root.savePresetEffectIndex, name)
            root.savePresetEffectIndex = -1
        }
        onRejected: root.savePresetEffectIndex = -1
    }
}
