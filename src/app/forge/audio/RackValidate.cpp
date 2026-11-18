#include "app/forge/audio/RackValidate.h"

#include "engine/audio/dsp/graph/AudioGraph.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"

#include <QSet>

#include <algorithm>

namespace drift::forge {

using audiofx::KnobSpec;
using audiofx::PedalSpec;

namespace {

struct Check
{
    const ForgeDoc &doc;
    QStringList errors;

    void value(const std::optional<KnobValue> &v, const QString &where)
    {
        if (v && v->bound() && !doc.param(v->param))
            errors << QStringLiteral("%1 uses a slider that no longer exists.").arg(where);
    }
};

void splitProblems(const RackItem &item, int depth, Check &c)
{
    if (depth >= audiofx::kMaxSplitDepth)
        c.errors << QStringLiteral("Splits nest at most two deep.");
    if (item.lanes.size() < 2 || int(item.lanes.size()) > audiofx::kMaxSplitLanes)
        c.errors << QStringLiteral("A split needs 2 to %1 lanes.").arg(audiofx::kMaxSplitLanes);
    if (item.crossfade && (item.mode != QLatin1String("parallel") || item.lanes.size() != 2))
        c.errors << QStringLiteral("Blending needs a parallel split with two lanes.");
    if (item.mode == QLatin1String("bands") && (item.crossovers ? item.crossovers->size() : 0) != int(item.lanes.size()) - 1)
        c.errors << QStringLiteral("A band split needs one crossover per lane boundary.");
    c.value(item.blend, QStringLiteral("A split"));
    if (item.crossovers)
        for (const KnobValue &v : *item.crossovers)
            c.value(v, QStringLiteral("A crossover"));
}

void pedalProblems(const RackItem &item, Check &c)
{
    const PedalSpec *spec = audiofx::pedalSpec(item.type.toStdString());
    if (!spec) {
        c.errors << QStringLiteral("Drift has no pedal called \"%1\".").arg(item.type);
        return;
    }
    const QString label = QString::fromLatin1(spec->label);
    for (auto it = item.knobs.begin(); it != item.knobs.end(); ++it) {
        if (!audiofx::findKnob(*spec, it.key().toStdString()))
            c.errors << QStringLiteral("%1 has no knob \"%2\".").arg(label, it.key());
        c.value(it.value(), label);
    }
    c.value(item.bypass, label);
    if (item.type == QLatin1String("convolution")) {
        if (item.ir.isEmpty())
            c.errors << QStringLiteral("A convolution reverb needs an impulse response.");
        else if (builtinIr(item.ir).isNull()) {
            const bool found = std::any_of(c.doc.assets.cbegin(), c.doc.assets.cend(), [&](const ForgeAsset &a) { return a.id == item.ir; });
            if (!found)
                c.errors << QStringLiteral("A convolution reverb's impulse response is missing.");
        }
    }
}

}

QStringList validateRack(const ForgeDoc &doc)
{
    Rack rack = rackOf(doc);
    Check c{doc, {}};
    QSet<QString> ids;
    int count = 0;
    walkRack(rack.chain, [&](const Located &at) {
        ++count;
        const RackItem &item = *at.item;
        if (ids.contains(item.id))
            c.errors << QStringLiteral("Two pedals share the id %1.").arg(item.id);
        ids.insert(item.id);
        if (item.isSplit())
            splitProblems(item, at.depth, c);
        else
            pedalProblems(item, c);
    });
    if (count > audiofx::kMaxGraphNodes)
        c.errors << QStringLiteral("A pedalboard holds at most %1 pedals.").arg(audiofx::kMaxGraphNodes);

    QSet<QString> mods;
    for (const Modulator &m : std::as_const(rack.modulators))
        mods.insert(m.id);
    for (const Modulator &m : std::as_const(rack.modulators)) {
        if (!audiofx::modulatorSpec(m.type.toStdString()))
            c.errors << QStringLiteral("Drift has no modulator called \"%1\".").arg(m.type);
        if (m.type == QLatin1String("steps") && (!m.steps || m.steps->isEmpty() || m.steps->size() > audiofx::kMaxSteps))
            c.errors << QStringLiteral("A step sequencer has 1 to %1 steps.").arg(audiofx::kMaxSteps);
        if (!m.source.isEmpty() && m.source != QLatin1String("input") && !ids.contains(m.source))
            c.errors << QStringLiteral("An envelope follower listens to a pedal that's gone.");
        for (const KnobValue &v : m.knobs)
            c.value(v, QStringLiteral("A modulator"));
    }
    for (const ModRoute &r : std::as_const(rack.routes)) {
        if (!mods.contains(r.from))
            c.errors << QStringLiteral("A modulation route comes from a modulator that's gone.");
        const RackItem *target = pedalAt(rack, r.to);
        const PedalSpec *spec = target ? audiofx::pedalSpec(target->type.toStdString()) : nullptr;
        const KnobSpec *k = spec ? audiofx::findKnob(*spec, r.knob.toStdString()) : nullptr;
        if (!k)
            c.errors << QStringLiteral("A modulation route goes to a knob that's gone.");
        else if (!k->continuous())
            c.errors << QStringLiteral("%1's %2 is a switch and can't be modulated.").arg(QString::fromLatin1(spec->label), QString::fromLatin1(k->label));
        c.value(r.depth, QStringLiteral("A modulation route"));
    }
    QStringList unique;
    for (const QString &e : std::as_const(c.errors))
        if (!unique.contains(e))
            unique << e;
    return unique;
}

}
