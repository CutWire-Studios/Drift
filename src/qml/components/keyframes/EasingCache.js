.pragma library

const sampleCache = {}

// Samples depend only on the easing params, so one C++ round trip per distinct
// spec is enough. `fetch` is EditorState.easingSamples, passed in because a
// library script cannot see the context singleton.
function samples(spec, n, fetch) {
    const key = spec.fn + "|" + spec.amplitude + "|" + spec.period + "|" + n
    let hit = sampleCache[key]
    if (hit === undefined) {
        const raw = fetch(spec, n)
        hit = new Array(raw.length)
        for (let i = 0; i < raw.length; ++i)
            hit[i] = { x: raw[i].x, y: raw[i].y }
        sampleCache[key] = hit
    }
    return hit
}

function sortedPoints(entry) {
    return entry.points.slice().sort((a, b) => a.seconds - b.seconds)
}
