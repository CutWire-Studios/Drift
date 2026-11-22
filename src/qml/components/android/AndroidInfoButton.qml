import QtQuick
import Drift

// The hint a phone page hides to save space, one tap away: shows `hint` as a toast. Sits at the end
// of the page's first row.
IconButton {
    property string hint: ""

    visible: Theme.phone
    width: visible ? implicitWidth : 0
    glyph: Theme.icons.info
    buttonSize: Theme.androidIconButtonVisual
    iconSize: Theme.iconSizeBase
    Accessible.name: qsTr("Information")
    onClicked: Toasts.info(hint, 7000)
}
