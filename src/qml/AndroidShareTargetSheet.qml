import QtQuick
import Drift
import "components"

// What a share into an already-open project asks.
//
// Sharing a clip while the editor is up is genuinely ambiguous — it can mean "put this in what
// I am working on" or "start something new with this" — and only the user knows which. Guessing
// either way throws work away half the time: adding to the wrong project, or replacing one.
AndroidBottomSheet {
    id: root

    signal addToProject(var urls)
    signal startQuickEdit(var urls)

    property var pendingUrls: []

    title: qsTr("Add shared media")

    readonly property real desiredHeight: Theme.androidSheetHeaderHeight
                                          + Theme.androidAddRowHeight * 2
                                          + Theme.spacingLg * 2
                                          + Theme.spacing2xl
                                          + root.safeBottom
    sheetHeightFraction: root.height > 0
                         ? Math.min(Theme.androidSheetExpandedFraction,
                                    root.desiredHeight / root.height)
                         : Theme.androidSheetHeightFraction
    sheetExpandedFraction: sheetHeightFraction

    function openFor(urls) {
        root.pendingUrls = urls || []
        root.open()
    }

    Column {
        anchors.fill: parent
        topPadding: Theme.spacingLg

        SheetActionRow {
            width: parent.width
            label: qsTr("Add to this project")
            detail: qsTr("Import at the playhead and stay here")
            glyph: Theme.icons.plus
            sideInset: root.safeLeft
            sideInsetRight: root.safeRight
            onClicked: {
                Haptics.select()
                const urls = root.pendingUrls
                root.dismiss()
                root.addToProject(urls)
            }
        }

        SheetActionRow {
            width: parent.width
            label: qsTr("New quick edit")
            detail: qsTr("Start a new project from this clip")
            glyph: Theme.icons.sparkles
            sideInset: root.safeLeft
            sideInsetRight: root.safeRight
            onClicked: {
                Haptics.select()
                const urls = root.pendingUrls
                root.dismiss()
                root.startQuickEdit(urls)
            }
        }
    }
}
