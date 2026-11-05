import QtQuick
import Drift

Column {
    id: root
    property var inspector
    width: parent.width
    spacing: Theme.spacingMd
    visible: inspector.slotHasPreset && !!inspector.selectedPreset
    enabled: !inspector.slotData.custom

    Row {
        width: parent.width
        spacing: Theme.spacingSm

        Text {
            width: parent.width - previewButton.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: inspector.selectedPreset ? inspector.selectedPreset.label : ""
            elide: Text.ElideRight
            color: Theme.c.textInspectorAnimControls.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
        }
        IconButton {
            id: previewButton
            glyph: EditorState.playing && inspector.animPreviewRunning ? Theme.icons.pause : Theme.icons.play
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            tooltip: qsTr("Preview this animation")
            onClicked: {
                if (EditorState.playing && inspector.animPreviewRunning) {
                    inspector.stopAnimPreview()
                } else {
                    inspector.previewAnimation()
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: 4
        visible: inspector.animSlot === "loop" || !inspector.presetFlags.durationLocked

        Row {
            width: parent.width
            spacing: 6
            Text {
                text: inspector.animSlot === "loop" ? qsTr("Period") : qsTr("Duration")
                color: Theme.c.textInspectorAnimControls.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            Text {
                text: Number(durationSlider.value).toFixed(2) + " s"
                color: Theme.c.textInspectorAnimControls.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.monoFontFamily
            }
        }
        ThemedSlider {
            id: durationSlider
            lockWhilePlaying: true
            width: parent.width
            label: inspector.animSlot === "loop" ? qsTr("Period") : qsTr("Duration")
            from: inspector.animSlot === "loop" ? 0.1 : 0
            to: inspector.animSlot === "loop" ? 10 : 5
            stepSize: 0.01
            valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
            Binding on value {
                when: !durationSlider.pressed
                value: inspector.animSlot === "loop"
                       ? (Number(inspector.slotData.period) || 1)
                       : (Number(inspector.slotData.duration) || 0)
            }
            onMoved: {
                const patch = {}
                patch[inspector.animSlot === "loop" ? "period" : "duration"] = value
                if (pressed)
                    inspector.previewSlot(patch)
                else
                    inspector.setSlot(patch)
            }
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Edit text animation"))
                else
                    EditorState.commitPreviewDrag()
            }
        }
    }

    Row {
        width: parent.width
        spacing: 8

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            visible: !inspector.presetFlags.unitLocked
            Text {
                text: qsTr("By")
                color: Theme.c.textInspectorAnimControls.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedComboBox {
                width: parent.width
                model: inspector.animUnitLabels
                currentIndex: Math.max(0, inspector.animUnits.indexOf(inspector.slotData.unit || "block"))
                onActivated: inspector.setSlot({ "unit": inspector.animUnits[currentIndex] })
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: 4
            Text {
                text: inspector.animSlot === "loop" ? qsTr("Phase") : qsTr("Stagger")
                HoverHandler { id: staggerHover }
                TapHandler {
                    id: staggerTap
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: staggerTip.held = true
                    onPressedChanged: if (!pressed) staggerTip.held = false
                }
                ThemedToolTip {
                    id: staggerTip
                    property bool held: false
                    text: inspector.animSlot === "loop"
                          ? qsTr("Delay between one unit and the next along the cycle")
                          : qsTr("Delay between one unit starting and the next")
                    visible: staggerHover.hovered || held
                }
                color: Theme.c.textInspectorAnimControls.foregroundMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }
            ThemedNumberField {
                id: staggerField
                width: parent.width
                decimals: 0
                step: 10
                from: 0
                to: 2000
                unit: "ms"
                enabled: (inspector.slotData.unit || "block") !== "block"
                Binding on value {
                    when: !staggerField.activeFocus
                    value: Math.round((Number(inspector.slotData.stagger) || 0) * 1000)
                }
                onEdited: v => inspector.setSlot({ "stagger": v / 1000 })
            }
        }
    }

    Column {
        width: (parent.width - 8) / 2
        spacing: 4
        visible: !inspector.presetFlags.orderLocked
        Text {
            text: qsTr("Order")
            color: Theme.c.textInspectorAnimControls.foregroundMuted
            font.pixelSize: Theme.fontSizeXs
            font.family: Theme.fontFamily
        }
        ThemedComboBox {
            width: parent.width
            model: inspector.animOrderLabels
            enabled: (inspector.slotData.unit || "block") !== "block"
            currentIndex: Math.max(0, inspector.animOrders.indexOf(inspector.slotData.order || "forward"))
            onActivated: inspector.setSlot({ "order": inspector.animOrders[currentIndex] })
        }
    }

    Column {
        width: parent.width
        spacing: 4
        visible: !inspector.presetFlags.easeLocked
        Text {
            text: qsTr("Ease")
            color: Theme.c.textInspectorAnimControls.foregroundMuted
            font.pixelSize: Theme.fontSizeXs
            font.family: Theme.fontFamily
        }
        CurveField {
            width: parent.width
            domain: "text"
            curve: inspector.slotData.ease || "easeOut"
            onCurveChosen: (curve, handles) => inspector.setSlot({ "ease": curve })
        }
    }

    TextParamSlots {
        width: parent.width
        visible: inspector.presetOwnParams.length > 0
        specs: inspector.presetOwnParams
        values: inspector.slotData.params || ({})
        onChanged: (id, value) => inspector.setSlot(inspector.slotParamPatch(id, value))
        onPreviewChanged: (id, value) => inspector.previewSlot(inspector.slotParamPatch(id, value))
        onDragStarted: EditorState.beginPreviewDrag(qsTr("Edit text animation"))
        onDragEnded: EditorState.commitPreviewDrag()
    }
}
