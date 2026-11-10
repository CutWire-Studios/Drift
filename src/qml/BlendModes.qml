pragma Singleton
import QtQuick

QtObject {
    readonly property var groups: [
        { "label": "", "modes": [
            { "id": "normal", "label": qsTr("Normal") } ] },
        { "label": qsTr("Darken"), "modes": [
            { "id": "darken", "label": qsTr("Darken") },
            { "id": "multiply", "label": qsTr("Multiply") },
            { "id": "colorBurn", "label": qsTr("Color Burn") },
            { "id": "linearBurn", "label": qsTr("Linear Burn") },
            { "id": "darkerColor", "label": qsTr("Darker Color") } ] },
        { "label": qsTr("Lighten"), "modes": [
            { "id": "lighten", "label": qsTr("Lighten") },
            { "id": "screen", "label": qsTr("Screen") },
            { "id": "colorDodge", "label": qsTr("Color Dodge") },
            { "id": "add", "label": qsTr("Add") },
            { "id": "lighterColor", "label": qsTr("Lighter Color") } ] },
        { "label": qsTr("Contrast"), "modes": [
            { "id": "overlay", "label": qsTr("Overlay") },
            { "id": "softLight", "label": qsTr("Soft Light") },
            { "id": "hardLight", "label": qsTr("Hard Light") },
            { "id": "vividLight", "label": qsTr("Vivid Light") },
            { "id": "linearLight", "label": qsTr("Linear Light") },
            { "id": "pinLight", "label": qsTr("Pin Light") },
            { "id": "hardMix", "label": qsTr("Hard Mix") } ] },
        { "label": qsTr("Inversion"), "modes": [
            { "id": "difference", "label": qsTr("Difference") },
            { "id": "exclusion", "label": qsTr("Exclusion") },
            { "id": "subtract", "label": qsTr("Subtract") },
            { "id": "divide", "label": qsTr("Divide") } ] },
        { "label": qsTr("Component"), "modes": [
            { "id": "hue", "label": qsTr("Hue") },
            { "id": "saturation", "label": qsTr("Saturation") },
            { "id": "color", "label": qsTr("Color") },
            { "id": "luminosity", "label": qsTr("Luminosity") } ] }
    ]

    readonly property var flat: {
        const out = []
        for (const g of groups)
            for (const m of g.modes)
                out.push({ "id": m.id, "label": m.label, "group": g.label })
        return out
    }
    readonly property var ids: flat.map(m => m.id)
    readonly property var labels: flat.map(m => m.label)
    readonly property var sections: flat.map(m => m.group)
}
