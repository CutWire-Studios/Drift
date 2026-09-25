import QtQuick
import Drift

// Audio FX tab: audio-effect preset library (browse left, edit in the Audio inspector).
Item {
    id: root

    property alias searchText: browser.searchText
    property alias activeCategory: browser.activeCategory
    property alias categories: browser.categories

    AudioEffectBrowser {
        id: browser
        anchors.fill: parent
    }
}
