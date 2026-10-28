#pragma once

#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeTimeView.h"

#include <QAbstractListModel>
#include <QColor>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class KeyframeController;

// The row tree of one Keyframes panel for the selected clip. Rebuilds are diffed against the
// current rows, so a delegate only goes away when its row does. Gesture maths that needs the rows
// (hit-testing, marquee, drag deltas, snapping) lives here so QML only forwards pointer events.
class KeyframeRowsModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(KeyframeTimeView *timeView READ timeView WRITE setTimeView NOTIFY timeViewChanged)
    Q_PROPERTY(int track MEMBER m_track NOTIFY inputsChanged)
    Q_PROPERTY(int clip MEMBER m_clip NOTIFY inputsChanged)
    Q_PROPERTY(int revision MEMBER m_revision NOTIFY inputsChanged)
    Q_PROPERTY(bool sizeLinked MEMBER m_sizeLinked NOTIFY inputsChanged)
    Q_PROPERTY(bool animatedOnly MEMBER m_animatedOnly NOTIFY inputsChanged)
    Q_PROPERTY(QString scaleLabel MEMBER m_scaleLabel NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap collapsed MEMBER m_collapsed NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap curveColors MEMBER m_curveColors NOTIFY styleChanged)
    Q_PROPERTY(QVariantList curveRamp MEMBER m_curveRamp NOTIFY styleChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { RowRole = Qt::UserRole + 1, RowKeyRole };

    explicit KeyframeRowsModel(QObject *parent = nullptr);

    QObject *controller() const;
    void setController(QObject *controller);
    KeyframeTimeView *timeView() const { return m_timeView; }
    void setTimeView(KeyframeTimeView *view);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList rows() const;
    int track() const { return m_track; }
    int clip() const { return m_clip; }
    KeyframeController *keyframes() const { return m_controller; }
    // The colour a property's lane and curve are drawn in: its own entry in `curveColors`, else a
    // ramp entry picked by a hash of the id, the same pick Theme.keyframeCurveColor makes.
    Q_INVOKABLE QColor colorFor(const QString &prop) const;

    const QList<KeyframeRows::Row> &list() const { return m_rows; }
    // Replaces the rows with `next`, inserting and removing only what changed.
    void setRows(const QList<KeyframeRows::Row> &next);

    Q_INVOKABLE void rebuild();
    Q_INVOKABLE void clear();

    Q_INVOKABLE QVariantMap rowAt(int index) const;
    Q_INVOKABLE QVariantList dots(int index) const;
    Q_INVOKABLE QVariantList refsAt(int index, double seconds, double tolerance) const;
    Q_INVOKABLE QVariantList rowRefs(int index) const;
    Q_INVOKABLE QVariantList allVisibleKeyRefs() const;

    // Row indices are into the flat row list. An empty list means every data row.
    Q_INVOKABLE double prevKeyTime(const QVariantList &indices, double t) const;
    Q_INVOKABLE double nextKeyTime(const QVariantList &indices, double t) const;
    Q_INVOKABLE QVariantList targetIndices(const QStringList &selectedProps, const QString &focusedProp) const;
    Q_INVOKABLE QStringList targetTracks(const QVariantList &indices) const;
    Q_INVOKABLE bool allTargetsKeyed(const QVariantList &indices, double t, double tolerance) const;
    Q_INVOKABLE bool targetsHaveKeys(const QVariantList &indices) const;

    Q_INVOKABLE QVariantMap hitTest(double x, double y, double contentY, double rowHeight, double slop) const;
    Q_INVOKABLE QVariantList marqueeRefs(double x0, double x1, double y0, double y1, double contentY,
                                         double rowHeight, const QVariantList &base) const;
    // The time shift of a dragged key group: `origin + dx / pxPerSecond`, clamped to the clip and
    // snapped, minus `origin`.
    Q_INVOKABLE double moveDelta(double origin, double dx, const QVariantList &excludeRefs, bool invertSnap,
                                 bool zeroWhenStill) const;
    Q_INVOKABLE double snapKeyTime(double seconds, const QVariantList &excludeRefs, bool invertSnap) const;

signals:
    void controllerChanged();
    void timeViewChanged();
    void inputsChanged();
    void styleChanged();
    void rowsChanged();
    void countChanged();

private:
    QList<int> indicesOrDataRows(const QVariantList &indices) const;
    QList<KeyframeRows::Row> pick(const QVariantList &indices) const;
    KeyframeRows::Axis axis() const;

    QPointer<KeyframeController> m_controller;
    QPointer<KeyframeTimeView> m_timeView;
    int m_track = -1;
    int m_clip = -1;
    int m_revision = 0;
    bool m_sizeLinked = false;
    bool m_animatedOnly = false;
    QString m_scaleLabel;
    QVariantMap m_collapsed;
    QVariantMap m_curveColors;
    QVariantList m_curveRamp;

    QVariantList m_entries;
    QString m_entriesKey;

    QList<KeyframeRows::Row> m_rows;
    mutable QVariantList m_rowsCache;
    mutable bool m_rowsCacheValid = false;
};
