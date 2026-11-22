#include "app/editor/sessions/KeyframeInteraction.h"

#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/KeyframeCurvesModel.h"
#include "app/editor/sessions/KeyframeRowsModel.h"
#include "app/editor/sessions/KeyframeTimeView.h"

#include <QSet>

// The part of KeyframeInteraction that knows the app: it reads the geometry from the panel's models
// and writes the project through KeyframeController. Kept apart so the state machine links without them.
void KeyframeInteraction::attach(QObject *rowsModel, QObject *curvesModel, QObject *timeView, QObject *controller)
{
    const QPointer<KeyframeRowsModel> rows = qobject_cast<KeyframeRowsModel *>(rowsModel);
    const QPointer<KeyframeCurvesModel> curves = qobject_cast<KeyframeCurvesModel *>(curvesModel);
    const QPointer<KeyframeTimeView> view = qobject_cast<KeyframeTimeView *>(timeView);
    const QPointer<KeyframeController> kc = qobject_cast<KeyframeController *>(controller);

    Ops ops;
    ops.scene = [rows, curves, view](Scene &scene) {
        if (rows)
            scene.rows = rows->list();
        if (view) {
            scene.axis = {view->pxPerSecond(), view->contentX()};
            scene.clipStart = view->clipStart();
            scene.clipDuration = view->clipDuration();
        }
        if (curves) {
            scene.series = curves->seriesList();
            scene.graphMargin = curves->graphMargin();
            scene.graphHeight = curves->graphHeight();
            scene.focusedIndex = curves->focusedIndex();
            scene.showHandles = curves->showHandles();
        }
    };
    ops.lockRanges = [curves](bool lock) {
        if (!curves)
            return;
        if (lock)
            curves->lockRanges();
        else
            curves->unlockRanges();
    };
    if (kc) {
        ops.moveKeys = [this, kc](const QVariantList &refs, double dt) {
            return kc->previewMoveKeys(m_track, m_clip, refs, dt);
        };
        ops.stretchKeys = [this, kc](const QVariantList &refs, double pivot, double factor) {
            return kc->previewStretchKeys(m_track, m_clip, refs, pivot, factor);
        };
        ops.setTangents = [this, kc](const QString &prop, double seconds, double inDx, double inDy, double outDx,
                                     double outDy, bool corner) {
            kc->previewTangents(m_track, m_clip, prop, seconds, inDx, inDy, outDx, outDy, corner);
        };
        ops.removeKeys = [this, kc](const QVariantList &refs) { kc->removeKeys(m_track, m_clip, refs); };
        ops.addKeys = [this, kc](const QStringList &props, double seconds) {
            kc->addKeysAt(m_track, m_clip, props, seconds);
        };
        ops.clearCollisions = [kc] { kc->clearCollisions(); };
        ops.snapTime = [this, kc, rows](double seconds, const QVariantList &exclude, bool noSnap) {
            QSet<qint64> skip;
            for (const QVariant &v : exclude)
                skip.insert(qRound64(v.toMap().value(QStringLiteral("seconds")).toDouble() * 1e6));
            QList<double> times;
            if (rows) {
                times = KeyframeRows::allKeyTimes(m_scene.rows);
            } else {
                for (const KeyframeCurves::Series &s : std::as_const(m_scene.series))
                    for (const KeyframeCurves::Point &p : s.points)
                        times.append(p.seconds);
            }
            QVariantList others;
            for (double t : std::as_const(times)) {
                if (!skip.contains(qRound64(t * 1e6)))
                    others.append(t);
            }
            return kc->gestureTime(seconds, m_scene.axis.pxPerSecond, others, noSnap, m_timelineSnap);
        };
        setDragSession(kc->dragSession());
    }
    setOps(std::move(ops));
}
