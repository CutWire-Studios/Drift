import QtQuick
import Drift

// Compact label for form sections / field captions.
// tone: "muted" | "default"   size: "xs" | "sm" | "base"
Text {
    property string tone: "muted"
    property string size: "xs"

    color: tone === "default" ? Theme.panelForeground : Theme.mutedForeground
    font.family: Theme.fontFamily
    font.pixelSize: size === "base" ? Theme.fontSizeBase
                     : (size === "sm" ? Theme.fontSizeSm : Theme.fontSizeXs)
    wrapMode: Text.WordWrap
}
