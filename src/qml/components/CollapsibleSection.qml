import QtQuick
import Drift

// Reusable inspector block: optional chevron collapse, optional enable switch,
// hairline separator, and a default-property content slot.
//
// Content is shown when the section is expanded, and — if `showSwitch` — only
// while the switch is on (so feature groups like Shadow hide their knobs when off).
Item {
    id: root

    property string title: ""
    property string tooltip: ""

    // Major sections (Style, Appearance, …) use the chevron. Nested feature
    // rows (Outline, Shadow, …) usually set collapsible: false and showSwitch.
    property bool collapsible: true
    property bool expanded: true
    property bool showSeparator: true
    property bool showHeader: true

    property bool showSwitch: false
    property bool switchChecked: false
    property string switchTooltip: ""

    signal switchToggled(bool checked)

    default property alias content: body.data

    readonly property bool contentVisible: (!collapsible || expanded)
                                           && (!showSwitch || switchChecked)

    width: parent ? parent.width : implicitWidth
    implicitWidth: 200
    implicitHeight: column.height

    Column {
        id: column
        width: parent.width
        spacing: 0

        CollapsibleSectionRule {
            shown: root.showSeparator && root.showHeader
        }

        CollapsibleSectionHeader {
            visible: root.showHeader
            title: root.title
            tooltip: root.tooltip
            collapsible: root.collapsible
            expanded: root.expanded
            showSwitch: root.showSwitch
            switchChecked: root.switchChecked
            switchTooltip: root.switchTooltip
            showSeparator: root.showSeparator
            onExpandToggled: root.expanded = !root.expanded
            onSwitchToggled: function(checked) { root.switchToggled(checked) }
        }

        Column {
            id: body
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.contentVisible
            topPadding: visible ? Theme.spacingSm : 0
            bottomPadding: visible ? Theme.spacingSm : 0
        }
    }
}
