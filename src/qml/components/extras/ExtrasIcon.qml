import QtQuick
import Drift

// Catalog icon names are a small server-side vocabulary; anything unknown falls back to a package.
IconGlyph {
    id: root

    property string name: ""

    readonly property var _map: ({
        "star": Theme.icons.star,
        "sparkles": Theme.icons.sparkles,
        "palette": Theme.icons.brush,
        "type": Theme.icons.type,
        "captions": Theme.icons.captions,
        "audio": Theme.icons.audioLines,
        "mic": Theme.icons.mic,
        "scissors": Theme.icons.scissors,
        "wand": Theme.icons.wand,
        "smile": Theme.icons.smile,
        "brain": Theme.icons.sparkles,
        "bot": Theme.icons.bot,
        "cpu": "cpu",
        "zap": Theme.icons.gauge,
        "layers": Theme.icons.layers,
        "file": Theme.icons.fileText
    })

    glyph: root._map[root.name] || Theme.icons.package
}
