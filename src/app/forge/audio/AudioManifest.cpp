#include "app/forge/audio/AudioManifest.h"

#include "app/forge/audio/RackGraph.h"
#include "app/forge/compiler/Manifest.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"

#include <QHash>
#include <QSet>

namespace drift::forge {

using audiofx::KnobSpec;
using audiofx::PedalSpec;

namespace {

QString iconFor(const QString &type)
{
    static const QHash<QString, QString> icons = {
        {"filter", "audio-waveform"}, {"ladder", "audio-waveform"}, {"drive", "flame"}, {"reverb", "waves"},
        {"convolution", "church"}, {"delay", "repeat"}, {"pan", "move-horizontal"}, {"gain", "volume-2"},
        {"classic.autopan", "move-horizontal"}, {"classic.chorus", "layers"}, {"classic.echo", "repeat"},
        {"classic.flanger", "circle-dashed"}, {"classic.phaser", "circle-dot-dashed"},
        {"classic.tremolo", "audio-waveform"}, {"classic.stereowiden", "unfold-horizontal"},
        {"classic.bitcrush", "binary"}, {"classic.crystalizer", "gem"}, {"classic.bitcrush_log", "cpu"},
        {"classic.tape", "cassette-tape"}, {"classic.vinyl", "disc-3"}, {"classic.megaphone", "megaphone"},
        {"classic.muffled", "volume-x"}, {"classic.bandlimit", "phone"}, {"classic.underwater", "droplets"},
        {"classic.walkie", "radio-receiver"}, {"classic.compressor", "gauge"}, {"classic.deesser", "ear"},
        {"classic.eq3", "sliders-horizontal"}, {"classic.gate", "door-closed"}, {"classic.leveler", "equal"},
        {"classic.limiter", "maximize"}, {"classic.pitch", "rabbit"}, {"classic.darklord", "shield"},
        {"classic.vibrato", "activity"},
    };
    return icons.value(type, QStringLiteral("audio-lines"));
}

bool knobBindsOwnSlider(const PedalSpec &spec, const QString &knob, const QString &param)
{
    if (param == knob)
        return true;
    const KnobSpec *k = audiofx::findKnob(spec, knob.toStdString());
    if (!k)
        return false;
    for (const char *alias : k->aliases)
        if (param == QLatin1String(alias))
            return true;
    return false;
}

}

QString legacyProcessorFor(const ForgeDoc &doc)
{
    const Rack rack = rackOf(doc);
    if (rack.chain.size() != 1 || !rack.modulators.isEmpty() || !rack.routes.isEmpty())
        return {};
    const RackItem &item = rack.chain.front();
    if (item.isSplit() || (item.bypass && !(item.bypass->isBool && !item.bypass->bound() && !item.bypass->truthy())))
        return {};
    const PedalSpec *spec = audiofx::pedalSpec(item.type.toStdString());
    const std::string type = item.type.toStdString();
    const std::string_view processor = audiofx::classicProcessorId(type);
    if (!spec || processor.empty() || item.knobs.isEmpty())
        return {};
    QStringList names;
    for (auto it = item.knobs.begin(); it != item.knobs.end(); ++it) {
        if (!it.value().bound() || !knobBindsOwnSlider(*spec, it.key(), it.value().param))
            return {};
        names << it.value().param;
    }
    if (doc.params.size() != names.size())
        return {};
    for (const ParamDef &p : doc.params)
        if (!names.contains(p.identifier))
            return {};
    return QString::fromUtf8(processor.data(), int(processor.size()));
}

QString audioMinAppVersion(const ForgeDoc &doc)
{
    return QString::fromLatin1(legacyProcessorFor(doc).isEmpty() ? kAudioGraphAppVersion : kAudioClassicAppVersion);
}

QJsonObject audioManifest(const ForgeDoc &doc)
{
    const Rack rack = rackOf(doc);
    const QString legacy = legacyProcessorFor(doc);
    QString first;
    bool usesIr = false;
    walkRack(rack.chain, [&](const RackItem &item, int) {
        if (item.isSplit())
            return;
        if (first.isEmpty())
            first = item.type;
        usesIr = usesIr || item.type == QLatin1String("convolution");
    });

    QJsonObject json{{"id", doc.meta.id}, {"displayName", doc.meta.displayName}, {"category", doc.meta.category},
                     {"icon", iconFor(first)}, {"order", 1000}};
    if (!doc.meta.description.isEmpty())
        json["description"] = doc.meta.description;
    json["backend"] = QStringLiteral("juce");
    json["processor"] = legacy.isEmpty() ? QStringLiteral("graph") : legacy;
    const PedalSpec *classic = legacy.isEmpty() ? nullptr : audiofx::pedalSpec(("classic." + legacy).toStdString());
    json["prerollMs"] = classic ? classic->prerollMs : 0;
    QJsonArray parameters;
    for (const ParamDef &p : doc.params)
        parameters.append(paramManifestEntry(doc, p));
    json["parameters"] = parameters;
    if (legacy.isEmpty()) {
        json["graph"] = rackGraphJson(doc);
        QStringList features{QStringLiteral("audio:graph")};
        if (!rack.modulators.isEmpty())
            features << QStringLiteral("audio:modulation");
        if (usesIr)
            features << QStringLiteral("audio:convolution");
        features.sort();
        json["nextFeatures"] = QJsonArray::fromStringList(features);
    }
    if (doc.presets && !doc.presets->isEmpty())
        json["presets"] = presetsManifest(doc);
    return json;
}

QJsonObject audioPreviewManifest(const ForgeDoc &doc, QJsonObject *hoisted)
{
    ForgeDoc d = doc;
    Rack rack = rackOf(doc);
    if (hoisted)
        *hoisted = {};

    const auto hoist = [&](KnobValue &v, const QString &name, const QString &label, double min, double max) {
        if (v.bound())
            return;
        ParamDef p;
        p.identifier = name;
        p.displayName = label;
        p.type = ParamType::Float;
        p.min = min;
        p.max = max;
        p.defaultValue = v.asNumber();
        d.params.append(p);
        if (hoisted)
            hoisted->insert(name, v.asNumber());
        v = KnobValue::ref(name);
    };
    const auto hoistKnobs = [&](QMap<QString, KnobValue> &knobs, const PedalSpec *spec, const QString &owner) {
        for (auto it = knobs.begin(); it != knobs.end(); ++it) {
            const KnobSpec *k = spec ? audiofx::findKnob(*spec, it.key().toStdString()) : nullptr;
            if (k)
                hoist(it.value(), QStringLiteral("__k_%1_%2").arg(owner, it.key()), QString::fromLatin1(k->label), k->min, k->max);
        }
    };

    walkRack(rack.chain, [&](const Located &at) {
        RackItem &item = *at.item;
        if (item.isSplit()) {
            if (item.blend)
                hoist(*item.blend, QStringLiteral("__k_%1_blend").arg(item.id), QStringLiteral("Blend"), 0, 1);
            if (item.crossovers)
                for (int i = 0; i < item.crossovers->size(); ++i)
                    hoist((*item.crossovers)[i], QStringLiteral("__k_%1_crossover%2").arg(item.id).arg(i), QStringLiteral("Crossover"), 20, 20000);
            return;
        }
        hoistKnobs(item.knobs, audiofx::pedalSpec(item.type.toStdString()), item.id);
        KnobValue off = item.bypass.value_or(KnobValue::flag(false));
        hoist(off, QStringLiteral("__k_%1_bypass").arg(item.id), QStringLiteral("Bypass"), 0, 1);
        item.bypass = off;
    });
    for (Modulator &m : rack.modulators)
        hoistKnobs(m.knobs, audiofx::modulatorSpec(m.type.toStdString()), m.id);
    for (ModRoute &r : rack.routes)
        hoist(r.depth, QStringLiteral("__k_%1_depth").arg(r.id), QStringLiteral("Depth"), -1, 1);

    d = withRack(d, rack);
    QJsonObject json = audioManifest(d);
    QJsonArray parameters = json.value("parameters").toArray();
    for (qsizetype i = doc.params.size(); i < parameters.size(); ++i) {
        QJsonObject e = parameters.at(i).toObject();
        e["hidden"] = true;
        parameters[i] = e;
    }
    json["parameters"] = parameters;
    return json;
}

}
