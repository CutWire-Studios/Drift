import QtQuick
import QtQuick.Controls.Basic
import Drift

// One cell of a bottom-sheet action grid: an icon square over a short label. For sheets with too
// many actions to list as rows (More tools). The detail line a row would show becomes the
// accessible description instead; the label has to carry the meaning on its own.
AbstractButton {
    id: root

    property string label: ""
    property string detail: ""
    property string glyph: ""
    // A toggle shows its state on the tile itself and does not dismiss its sheet.
    property bool toggle: false

    hoverEnabled: true
    implicitHeight: Theme.androidMinTouchTarget + Theme.spacing3xl + Theme.spacingLg

    Accessible.role: root.toggle ? Accessible.CheckBox : Accessible.Button
    Accessible.name: root.label
    Accessible.description: root.detail
    Accessible.checkable: root.toggle
    Accessible.checked: root.checked
    Accessible.onPressAction: root.clicked()

    scale: root.down ? Theme.pressScale : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    background: Item { }

    contentItem: Column {
        spacing: Theme.spacingSm
        opacity: root.enabled ? 1 : 0.45
        topPadding: Theme.spacingMd

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.androidMinTouchTarget
            height: Theme.androidMinTouchTarget
            radius: Theme.radiusMd
            color: root.toggle && root.checked
                   ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.22)
                   : (root.down ? Theme.popoverHover : Theme.panelAccent)
            border.width: root.toggle && root.checked ? Theme.borderWidth : 0
            border.color: Theme.primary

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: root.glyph
                iconSize: Theme.iconSizeLg
                iconColor: root.toggle && root.checked ? Theme.accentOnPanel : Theme.panelForeground
            }
        }

        Text {
            width: root.width - Theme.spacingSm * 2
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            lineHeight: 1.1
        }
    }
}
