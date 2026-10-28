.pragma library

var TIME_EPS = 1e-4

function keyId(prop, seconds) {
    return prop + "@" + Math.round(seconds * 1e6)
}

function unionKeys(lists) {
    const all = []
    for (let i = 0; i < lists.length; ++i) {
        for (let j = 0; j < lists[i].length; ++j)
            all.push(lists[i][j])
    }
    all.sort(function (a, b) { return a.seconds - b.seconds })
    const out = []
    for (let i = 0; i < all.length; ++i) {
        if (out.length === 0 || all[i].seconds - out[out.length - 1].seconds > TIME_EPS)
            out.push(all[i])
    }
    return out
}

// A property with one key that is not animating is just its static value, stored at the clip start.
function visibleKeys(entry, keys, clipStart) {
    if (!entry.animated && entry.group === "transform" && keys.length === 1
            && Math.abs(keys[0].seconds - clipStart) < TIME_EPS)
        return []
    return keys
}

// opts: scaleLabel, sizeLinked, animatedOnly, collapsed (set of group ids), clipStart, keysFor(prop) -> keys
function buildRows(entries, opts) {
    const clipStart = opts.clipStart || 0
    const collapsed = opts.collapsed || {}
    const linked = !!opts.sizeLinked
    const groups = []
    const byGroup = {}
    for (let i = 0; i < entries.length; ++i) {
        const e = entries[i]
        if (!byGroup[e.group]) {
            byGroup[e.group] = { "group": e.group, "label": e.groupLabel, "items": [] }
            groups.push(byGroup[e.group])
        }
        byGroup[e.group].items.push(e)
    }

    const out = []
    for (let g = 0; g < groups.length; ++g) {
        const grp = groups[g]
        const isCollapsed = !!collapsed[grp.group]
        const items = []
        let scaleInserted = false
        const hasSize = linked && grp.group === "transform"
                        && grp.items.some(function (e) { return e.prop === "width" })
                        && grp.items.some(function (e) { return e.prop === "height" })
        for (let i = 0; i < grp.items.length; ++i) {
            const e = grp.items[i]
            if (hasSize && (e.prop === "width" || e.prop === "height")) {
                if (scaleInserted)
                    continue
                scaleInserted = true
                const w = grp.items.find(function (x) { return x.prop === "width" })
                const h = grp.items.find(function (x) { return x.prop === "height" })
                const animated = w.animated || h.animated
                if (opts.animatedOnly && !animated)
                    continue
                items.push({
                    "type": "scale", "label": opts.scaleLabel || "Scale", "group": grp.group,
                    "kind": "scalar", "prop": "scale", "tracks": ["width", "height"],
                    "keyProp": "width", "animated": animated,
                    "enabled": w.enabled && h.enabled, "entries": [w, h]
                })
                continue
            }
            if (opts.animatedOnly && !e.animated)
                continue
            items.push(Object.assign({ "type": "prop" }, e))
        }
        if (items.length === 0)
            continue

        let count = 0
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            const lists = []
            item.trackKeys = {}
            for (let t = 0; t < item.tracks.length; ++t) {
                const track = item.tracks[t]
                const own = visibleKeys(item, opts.keysFor(track), clipStart)
                item.trackKeys[track] = own
                lists.push(own)
            }
            item.keys = unionKeys(lists)
            count += item.keys.length
        }
        out.push({ "type": "group", "group": grp.group, "label": grp.label,
                   "count": count, "collapsed": isCollapsed, "children": items })
        if (!isCollapsed)
            Array.prototype.push.apply(out, items)
    }
    return out
}

function allKeyTimes(rows) {
    const times = []
    for (let i = 0; i < rows.length; ++i) {
        const r = rows[i]
        const list = r.type === "group" ? (r.collapsed ? r.children : []) : [r]
        for (let k = 0; k < list.length; ++k) {
            if (!list[k].keys)
                continue
            for (let j = 0; j < list[k].keys.length; ++j)
                times.push(list[k].keys[j].seconds)
        }
    }
    return times
}

function prevKeyTime(rows, t) {
    let best = null
    const times = allKeyTimes(rows)
    for (let i = 0; i < times.length; ++i) {
        if (times[i] < t - TIME_EPS && (best === null || times[i] > best))
            best = times[i]
    }
    return best
}

function nextKeyTime(rows, t) {
    let best = null
    const times = allKeyTimes(rows)
    for (let i = 0; i < times.length; ++i) {
        if (times[i] > t + TIME_EPS && (best === null || times[i] < best))
            best = times[i]
    }
    return best
}

// The key of each of the row's tracks near `seconds`. Scale and colour rows own several tracks,
// which may not all be keyed at the same instant.
function refsForRowAt(row, seconds, tolerance) {
    const tol = tolerance === undefined ? TIME_EPS : tolerance
    const refs = []
    const tracks = row.tracks || [row.prop]
    for (let i = 0; i < tracks.length; ++i) {
        const keys = row.trackKeys ? row.trackKeys[tracks[i]] : null
        if (!keys)
            continue
        let best = -1
        let bestDist = tol
        for (let j = 0; j < keys.length; ++j) {
            const d = Math.abs(keys[j].seconds - seconds)
            if (d <= bestDist) {
                bestDist = d
                best = j
            }
        }
        if (best >= 0)
            refs.push({ "prop": tracks[i], "seconds": keys[best].seconds })
    }
    return refs
}

// The diamonds drawn on one lane: {seconds, refs, hold}. A group row summarises its children,
// so a summary diamond carries the refs of every child key at that instant.
function laneDots(rows, index) {
    const row = rows[index]
    if (!row)
        return []
    const out = []
    if (row.type !== "group") {
        const keys = row.keys || []
        for (let i = 0; i < keys.length; ++i)
            out.push({ "seconds": keys[i].seconds, "refs": refsForRowAt(row, keys[i].seconds),
                       "hold": !!keys[i].hold })
        return out
    }
    const all = []
    const children = row.children || []
    for (let j = 0; j < children.length; ++j) {
        const keys = children[j].keys || []
        for (let i = 0; i < keys.length; ++i)
            all.push({ "seconds": keys[i].seconds, "refs": refsForRowAt(children[j], keys[i].seconds) })
    }
    all.sort(function (a, b) { return a.seconds - b.seconds })
    for (let i = 0; i < all.length; ++i) {
        const last = out.length > 0 ? out[out.length - 1] : null
        if (last && all[i].seconds - last.seconds <= TIME_EPS)
            Array.prototype.push.apply(last.refs, all[i].refs)
        else
            out.push({ "seconds": all[i].seconds, "refs": all[i].refs.slice(), "hold": false })
    }
    return out
}

function refsSelected(selection, refs) {
    if (refs.length === 0)
        return false
    for (let i = 0; i < refs.length; ++i) {
        if (!selection.has(refs[i].prop, refs[i].seconds))
            return false
    }
    return true
}
