import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown when the AI engine picked in "How AI runs" cannot load a model. While `waiting`, the
// feature that hit the failure is paused until this dialog answers, and resumes on the new engine.
ThemedDialog {
    id: root

    property string failedVariant: ""
    property string errorText: ""
    property bool waiting: false
    property var options: []
    property bool answered: false

    readonly property string failedLabel: {
        var all = Addons.accelerationOptions()
        for (var i = 0; i < all.length; ++i) {
            if (all[i].value === failedVariant)
                return all[i].label
        }
        return failedVariant
    }

    title: qsTr("AI engine isn't working")
    acceptText: qsTr("Use this engine")
    rejectText: qsTr("Cancel")
    preferredWidth: Theme.dialogWidthMd

    function openFor(variant, error, isWaiting) {
        // A paused load's question outranks a report about one that already gave up.
        if (visible && waiting)
            return
        failedVariant = variant
        errorText = error
        waiting = isWaiting
        options = Addons.fallbackAccelerationOptions(variant)
        engineBox.currentIndex = 0
        answered = false
        open()
    }

    function answer(variant) {
        if (answered)
            return
        answered = true
        if (waiting)
            Addons.answerAccelerationFailure(variant)
        else if (variant.length > 0)
            Addons.setAcceleration(variant)
    }

    onAccepted: answer(options.length > 0 ? options[engineBox.currentIndex].value : "")
    onRejected: answer("")
    // Deferred: without an exit transition `closed` fires inside accept(), ahead of `accepted`.
    onClosed: Qt.callLater(answer, "")

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            size: "sm"
            tone: "default"
            text: root.waiting
                  ? qsTr("\"%1\" couldn't run this feature. Pick a different AI engine to continue.").arg(root.failedLabel)
                  : qsTr("\"%1\" couldn't run this feature. Pick a different AI engine, then try again.").arg(root.failedLabel)
        }

        ThemedComboBox {
            id: engineBox
            width: Math.min(parent.width, 260)
            textRole: "label"
            valueRole: "value"
            model: root.options
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            text: root.errorText
        }
    }
}
