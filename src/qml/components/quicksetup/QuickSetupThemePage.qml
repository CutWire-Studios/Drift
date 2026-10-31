import QtQuick
import QtQuick.Layouts
import Drift

QuickSetupPage {
    id: root

    title: qsTr("Pick a look")
    subtitle: qsTr("Choose how Drift looks. You can change this any time in Settings.")
    glyph: Theme.icons.sun
    contentMaxWidth: 900

    readonly property var options: {
        const _revision = ThemeManager.themes
        const out = [{ id: "system", name: qsTr("Match system"), detail: qsTr("Follows light and dark") }]
        const themes = ThemeManager.themes || []
        for (let i = 0; i < themes.length; ++i) {
            const t = themes[i]
            if (!t.builtIn)
                continue
            out.push({
                id: t.id,
                name: t.name,
                detail: t.hasVariants ? qsTr("Dark and light")
                        : t.variant === "light" ? qsTr("Light") : qsTr("Dark")
            })
        }
        return out
    }

    function choose(themeId) {
        if (themeId === "dark" || themeId === "light")
            EditorState.preferences.setDarkModePreference(themeId === "dark")
        ThemeManager.select(themeId)
    }

    GridLayout {
        width: parent.width
        columns: root.mobile ? 1 : 2
        columnSpacing: Theme.spacing2xl
        rowSpacing: Theme.spacingXl

        Grid {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: root.mobile ? parent.width : 260
            columns: root.mobile ? 2 : 1
            spacing: Theme.spacingMd

            Repeater {
                model: root.options

                QuickSetupCard {
                    id: card

                    required property var modelData
                    readonly property var swatch: {
                        const _variant = ThemeManager.effectiveVariant
                        return ThemeManager.previewColors(modelData.id)
                    }

                    width: root.mobile ? (parent.width - parent.spacing) / 2 : 260
                    height: root.mobile ? 72 : 60
                    text: modelData.name
                    selected: ThemeManager.selection === modelData.id
                    onClicked: root.choose(modelData.id)

                    Row {
                        x: Theme.spacingLg
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingMd

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 36
                            height: 36
                            radius: Theme.c.quickSetupCard.radius / 2
                            color: card.swatch.surface || "transparent"
                            border.width: Theme.c.quickSetupCard.borderWidth
                            border.color: Theme.c.quickSetupCard.border

                            Rectangle {
                                x: 5
                                y: 5
                                width: 26
                                height: 12
                                radius: 3
                                color: card.swatch.panel || "transparent"
                            }

                            Rectangle {
                                x: 5
                                y: 21
                                width: 12
                                height: 10
                                radius: 3
                                color: card.swatch.primary || "transparent"
                            }

                            Rectangle {
                                x: 20
                                y: 21
                                width: 11
                                height: 10
                                radius: 3
                                color: card.swatch.accent || "transparent"
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: card.modelData.name
                                color: Theme.c.quickSetupCard.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                font.weight: Font.Medium
                            }

                            Text {
                                text: card.modelData.detail
                                color: Theme.c.quickSetupCard.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }
                }
            }
        }

        QuickSetupMockEditor {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 0.625
            Layout.alignment: Qt.AlignTop
        }
    }
}
