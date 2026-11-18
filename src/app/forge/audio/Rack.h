#pragma once

#include "app/forge/doc/ForgeDoc.h"

#include <QList>
#include <QMap>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace drift::forge {

// A knob's value on the board: a number, a switch, or a binding to a slider of the document.
struct KnobValue
{
    QString param;
    double number = 0;
    bool isBool = false;

    static KnobValue num(double v) { return {{}, v, false}; }
    static KnobValue flag(bool v) { return {{}, v ? 1.0 : 0.0, true}; }
    static KnobValue ref(const QString &name) { return {name, 0, false}; }

    bool bound() const { return !param.isEmpty(); }
    // The literal as the number Drift's graph reads (switches are 0/1).
    double asNumber() const { return number; }
    bool truthy() const { return number != 0; }

    QJsonValue toJson() const;
    static KnobValue fromJson(const QJsonValue &v);
    bool operator==(const KnobValue &o) const { return param == o.param && number == o.number && isBool == o.isBool; }
};

struct SplitLane;

// A pedal, or (type "split") a split block. Keys this model does not know survive in `extra`.
struct RackItem
{
    QString id;
    QString type;
    QJsonObject extra;

    // Pedals.
    QMap<QString, KnobValue> knobs;
    std::optional<KnobValue> bypass;
    // convolution: "builtin:<name>" or an audio asset id.
    QString ir;

    // Splits.
    QString mode;
    bool crossfade = false;
    std::optional<KnobValue> blend;
    std::optional<QList<KnobValue>> crossovers;
    std::vector<SplitLane> lanes;

    bool isSplit() const { return type == QLatin1String("split"); }
};

struct SplitLane
{
    QString id;
    double gain = 1;
    std::vector<RackItem> chain;
    QJsonObject extra;
};

struct Modulator
{
    QString id;
    QString type;
    QMap<QString, KnobValue> knobs;
    std::optional<QList<double>> steps;
    // envelope: "input" or a pedal id.
    QString source;
    QJsonObject extra;
};

struct ModRoute
{
    QString id;
    QString from;
    QString to;
    QString knob;
    KnobValue depth;
    QJsonObject extra;
};

struct Rack
{
    std::vector<RackItem> chain;
    QList<Modulator> modulators;
    QList<ModRoute> routes;
    QJsonObject extra;

    static Rack fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

// The document's board; empty when it has none.
Rack rackOf(const ForgeDoc &doc);
ForgeDoc withRack(const ForgeDoc &doc, const Rack &rack);

// "builtin:<name>" -> name; null string for a recording asset.
QString builtinIr(const QString &ir);

struct Located
{
    RackItem *item = nullptr;
    // The list holding it: the top chain or a lane's chain.
    std::vector<RackItem> *list = nullptr;
    int index = -1;
    // Splits it sits inside.
    int depth = 0;
    SplitLane *lane = nullptr;
};

// Every item depth-first, a split before its lanes: the order Drift numbers graph nodes in.
void walkRack(std::vector<RackItem> &chain, const std::function<void(const Located &)> &fn, int depth = 0, SplitLane *lane = nullptr);
void walkRack(const std::vector<RackItem> &chain, const std::function<void(const RackItem &, int depth)> &fn, int depth = 0);

// Null item when the id is not on the board.
Located findItem(Rack &rack, const QString &id);
RackItem *pedalAt(Rack &rack, const QString &id);
RackItem *splitAt(Rack &rack, const QString &id);
int itemCount(const Rack &rack);

// Where an item goes: a lane's chain, or the top chain when `lane` is empty.
struct Slot
{
    QString lane;
    int index = -1;
};

struct SlotList
{
    std::vector<RackItem> *list = nullptr;
    int depth = 0;
};
SlotList slotList(Rack &rack, const Slot &slot);

}
