pragma Singleton
import QtQuick
import Drift

// The single source of truth for canvas presets: the platform catalog and the arithmetic that
// turns (template, quality) into a pixel size.
//
// This existed twice — once in components/LayoutChooserDialog.qml and once, near-verbatim, in
// AndroidLayoutPicker.qml — and the two copies had drifted apart rather than merely duplicated:
// the mobile one hard-coded 3840/1280 for 9:16 instead of deriving them, added 4:3, and dropped
// 1440p entirely. The same question therefore had two different answer sets depending on which
// screen asked it. The desktop {shortEdge, longEdge} form is canonical here because it derives
// every size from one pair of numbers instead of restating them per aspect.
//
// QML rather than a C++ model: the labels are qsTr() and have to re-translate live (main.cpp
// wires uiLanguageChanged to QQmlEngine::retranslate), and the icons are Theme.icons references.
QtObject {
    id: presets

    readonly property var categories: [
        { id: "youtube", label: qsTr("YouTube"), icon: Theme.icons.brandYoutube },
        { id: "instagram", label: qsTr("Instagram"), icon: Theme.icons.brandInstagram },
        { id: "facebook", label: qsTr("Facebook"), icon: Theme.icons.brandFacebook },
        { id: "tiktok", label: qsTr("TikTok"), icon: Theme.icons.brandTiktok },
        { id: "more", label: qsTr("More"), icon: Theme.icons.grid }
    ]

    // aspect: "9:16" | "16:9" | "1:1" | "4:5" | "4:3"
    readonly property var templates: [
        { id: "yt_video", category: "youtube", label: qsTr("YT Video"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandYoutube },
        { id: "yt_short", category: "youtube", label: qsTr("YT Short"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandYoutube },
        { id: "ig_reel", category: "instagram", label: qsTr("IG Reel"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandInstagram },
        { id: "ig_story", category: "instagram", label: qsTr("IG Story"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandInstagram },
        { id: "ig_post", category: "instagram", label: qsTr("IG Post"), detail: "1:1", aspect: "1:1", icon: Theme.icons.brandInstagram },
        { id: "ig_feed", category: "instagram", label: qsTr("IG Feed"), detail: "4:5", aspect: "4:5", icon: Theme.icons.brandInstagram },
        { id: "fb_reel", category: "facebook", label: qsTr("FB Reel"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandFacebook },
        { id: "fb_video", category: "facebook", label: qsTr("FB Video"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandFacebook },
        { id: "fb_story", category: "facebook", label: qsTr("FB Story"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandFacebook },
        { id: "tiktok", category: "tiktok", label: qsTr("TikTok"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandTiktok },
        { id: "snapchat", category: "more", label: qsTr("Snapchat"), detail: "9:16", aspect: "9:16", icon: Theme.icons.brandSnapchat },
        { id: "x_video", category: "more", label: qsTr("X / Twitter"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandX },
        { id: "linkedin", category: "more", label: qsTr("LinkedIn"), detail: "16:9", aspect: "16:9", icon: Theme.icons.brandLinkedin },
        { id: "square", category: "more", label: qsTr("Square"), detail: "1:1", aspect: "1:1", icon: Theme.icons.square },
        { id: "landscape", category: "more", label: qsTr("Landscape"), detail: "16:9", aspect: "16:9", icon: Theme.icons.monitor },
        { id: "portrait", category: "more", label: qsTr("Portrait"), detail: "9:16", aspect: "9:16", icon: Theme.icons.smartphone },
        { id: "classic", category: "more", label: qsTr("Classic"), detail: "4:3", aspect: "4:3", icon: Theme.icons.monitor }
    ]

    // Deliberately not in `templates`: a free size needs width and height fields to mean
    // anything, and the desktop dialog has none. A view that offers custom sizing appends this
    // itself; sizeFor() understands the aspect either way, so the arithmetic stays in one place.
    readonly property var customTemplate: {
        return { id: "custom", category: "more", label: qsTr("Custom"), detail: qsTr("Any size"),
                 aspect: "custom", icon: Theme.icons.sliders }
    }

    // shortEdge/longEdge are the two dimensions of a 16:9 frame at this quality; which one
    // becomes width or height depends on the template's aspect.
    readonly property var qualities: [
        { id: "4k", label: qsTr("4K"), shortEdge: 2160, longEdge: 3840 },
        { id: "1440p", label: qsTr("1440p"), shortEdge: 1440, longEdge: 2560 },
        { id: "1080p", label: qsTr("1080p"), shortEdge: 1080, longEdge: 1920 },
        { id: "720p", label: qsTr("720p"), shortEdge: 720, longEdge: 1280 }
    ]

    function templatesFor(categoryId) {
        const out = []
        for (let i = 0; i < presets.templates.length; ++i) {
            if (presets.templates[i].category === categoryId)
                out.push(presets.templates[i])
        }
        return out
    }

    function templateById(id) {
        for (let i = 0; i < presets.templates.length; ++i) {
            if (presets.templates[i].id === id)
                return presets.templates[i]
        }
        if (id === "custom")
            return presets.customTemplate
        return presets.templates[0]
    }

    function qualityById(id) {
        for (let i = 0; i < presets.qualities.length; ++i) {
            if (presets.qualities[i].id === id)
                return presets.qualities[i]
        }
        return presets.qualities[0]
    }

    // The one place a (template, quality) pair becomes pixels.
    function sizeFor(templateId, qualityId, customWidth, customHeight) {
        const tpl = presets.templateById(templateId)
        const aspect = tpl.aspect || "16:9"
        if (aspect === "custom") {
            return { width: Math.max(1, Math.round(customWidth)),
                     height: Math.max(1, Math.round(customHeight)),
                     aspect: aspect }
        }
        const q = presets.qualityById(qualityId)
        const shortEdge = q.shortEdge
        const longEdge = q.longEdge
        let w = 0
        let h = 0
        switch (aspect) {
        case "9:16":
            w = shortEdge
            h = longEdge
            break
        case "4:5":
            w = shortEdge
            h = Math.round(shortEdge * 5 / 4)
            break
        case "1:1":
            w = shortEdge
            h = shortEdge
            break
        case "4:3":
            w = Math.round(shortEdge * 4 / 3)
            h = shortEdge
            break
        case "16:9":
        default:
            w = longEdge
            h = shortEdge
            break
        }
        return { width: w, height: h, aspect: aspect }
    }

    // Proportions for drawing an aspect swatch, not pixel sizes.
    function ratioFor(aspectId) {
        switch (aspectId) {
        case "9:16": return { w: 9, h: 16 }
        case "4:5": return { w: 4, h: 5 }
        case "1:1": return { w: 1, h: 1 }
        case "4:3": return { w: 4, h: 3 }
        case "16:9":
        default: return { w: 16, h: 9 }
        }
    }

    // Nearest preset to a canvas the project already has. Custom is excluded by construction:
    // a free size matches anything and would win every comparison.
    function matchProject(width, height) {
        let best = { templateId: "yt_video", categoryId: "youtube", qualityId: "1080p" }
        let bestScore = Number.MAX_VALUE
        for (let i = 0; i < presets.templates.length; ++i) {
            const t = presets.templates[i]
            for (let q = 0; q < presets.qualities.length; ++q) {
                const size = presets.sizeFor(t.id, presets.qualities[q].id, width, height)
                const score = Math.abs(size.width - width) + Math.abs(size.height - height)
                if (score < bestScore) {
                    bestScore = score
                    best = { templateId: t.id, categoryId: t.category,
                             qualityId: presets.qualities[q].id }
                }
            }
        }
        return best
    }

    // The two views open on different defaults on purpose: a phone project is overwhelmingly
    // vertical, a desktop one overwhelmingly not. That is the only divergence worth keeping,
    // and stating it here is what stops it being re-derived as a second catalog.
    function defaultTemplateId(touch) {
        return touch ? "tiktok" : "yt_video"
    }

    function defaultCategoryId(touch) {
        return touch ? "tiktok" : "youtube"
    }
}
