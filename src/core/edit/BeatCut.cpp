#include "core/edit/BeatCut.h"

#include <QtGlobal>

#include <algorithm>

namespace drift {

QList<double> beatsEvery(const QList<double> &beats, int every, int anchorIndex)
{
    QList<double> out;
    every = qMax(1, every);
    const int first = beats.isEmpty() ? 0 : ((anchorIndex % every) + every) % every;
    for (int i = first; i < beats.size(); i += every)
        out.append(beats.at(i));
    return out;
}

QList<BeatCutSlot> planBeatCut(const QList<double> &beats, const QList<double> &available,
                               const BeatCutOptions &options)
{
    QList<BeatCutSlot> plan;
    if (beats.size() < 2 || available.isEmpty() || options.endSeconds <= options.startSeconds)
        return plan;

    const int step = qMax(1, options.beatsPerCut);
    const int perBar = qMax(1, options.beatsPerBar);
    const double period = (beats.last() - beats.first()) / (beats.size() - 1);
    const double minFirst = 0.5 * period;

    int anchor = -1;
    for (int i = 0; i < beats.size(); ++i) {
        if (beats.at(i) < options.startSeconds + minFirst)
            continue;
        if (options.syncToDownbeats && ((i - options.firstDownbeat) % perBar + perBar) % perBar != 0)
            continue;
        anchor = i;
        break;
    }
    if (anchor < 0)
        return plan;

    QList<double> bounds{options.startSeconds};
    for (int i = anchor; i < beats.size() && beats.at(i) < options.endSeconds; i += step)
        bounds.append(beats.at(i));
    // The music's own end closes the last slot unless that would leave a sliver.
    if (options.endSeconds - bounds.last() >= 0.5 * period * step)
        bounds.append(options.endSeconds);

    const int count = qMin<int>(bounds.size() - 1, available.size());
    for (int i = 0; i < count; ++i) {
        const double slot = bounds.at(i + 1) - bounds.at(i);
        const double len = qMin(slot, available.at(i));
        plan.append({i, bounds.at(i), len, available.at(i) < slot - 0.001});
    }
    return plan;
}

} // namespace drift
