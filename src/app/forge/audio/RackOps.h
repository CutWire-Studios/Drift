#pragma once

#include "app/forge/audio/Rack.h"
#include "app/forge/edit/OpResult.h"

namespace drift::forge {

inline constexpr const char *kDefaultIr = "builtin:plate";

// A control on the board that holds a value: the unit exposeValue and setValue work on.
struct ValuePath
{
    enum Kind { Knob, Bypass, Blend, Crossover, ModKnob, Depth };
    Kind kind = Knob;
    // Knob/Bypass/Blend/Crossover: the item id; ModKnob: the modulator id; Depth: the route id.
    QString item;
    QString knob;
    int index = 0;

    static ValuePath knobOf(const QString &item, const QString &knob) { return {Knob, item, knob, 0}; }
    static ValuePath bypassOf(const QString &item) { return {Bypass, item, {}, 0}; }
    static ValuePath blendOf(const QString &split) { return {Blend, split, {}, 0}; }
    static ValuePath crossoverOf(const QString &split, int index) { return {Crossover, split, {}, index}; }
    static ValuePath modKnobOf(const QString &mod, const QString &knob) { return {ModKnob, mod, knob, 0}; }
    static ValuePath depthOf(const QString &route) { return {Depth, route, {}, 0}; }
};

// Items. created[0] is the new id.
OpResult addPedal(const ForgeDoc &doc, const QString &type, const Slot &slot = {});
OpResult addSplit(const ForgeDoc &doc, const QString &mode, int lanes = 2, const Slot &slot = {});
OpResult moveItem(const ForgeDoc &doc, const QString &id, const Slot &to);
ForgeDoc removeItem(const ForgeDoc &doc, const QString &id);
OpResult addLane(const ForgeDoc &doc, const QString &splitId);
OpResult removeLane(const ForgeDoc &doc, const QString &splitId, const QString &laneId);
ForgeDoc setLaneGain(const ForgeDoc &doc, const QString &splitId, const QString &laneId, double gain);
ForgeDoc setSplitMode(const ForgeDoc &doc, const QString &splitId, const QString &mode);
OpResult setCrossfade(const ForgeDoc &doc, const QString &splitId, bool on);
ForgeDoc setIr(const ForgeDoc &doc, const QString &pedalId, const QString &ir);

// Modulators and routes. created[0] is the new id.
OpResult addModulator(const ForgeDoc &doc, const QString &type);
ForgeDoc removeModulator(const ForgeDoc &doc, const QString &id);
ForgeDoc setModulatorSource(const ForgeDoc &doc, const QString &id, const QString &source);
ForgeDoc setSteps(const ForgeDoc &doc, const QString &id, const QList<double> &steps);
// One route per modulator and knob: routing again changes its depth.
OpResult addRoute(const ForgeDoc &doc, const QString &from, const QString &to, const QString &knob, double depth = 0.3);
ForgeDoc removeRoute(const ForgeDoc &doc, const QString &id);
ForgeDoc setRouteDepth(const ForgeDoc &doc, const QString &routeId, double depth);

// Values.
std::optional<KnobValue> getValue(const ForgeDoc &doc, const ValuePath &path);
// Sets a fixed value; a control bound to a slider is changed through the slider instead.
ForgeDoc setValue(const ForgeDoc &doc, const ValuePath &path, double v);
// created[0] is the slider's name.
OpResult exposeValue(const ForgeDoc &doc, const ValuePath &path);
ForgeDoc unexposeValue(const ForgeDoc &doc, const ValuePath &path);

}
