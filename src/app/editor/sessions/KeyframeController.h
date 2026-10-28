#pragma once

#include "core/animation/Keyframe.h"
#include "core/base/Time.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class AppController;

// Keyframe editing for the Keyframes panel, reached from QML as EditorState.keyframes. Keys are
// addressed by {prop, seconds} on the clip's timeline; fx, afx and mask keys resolve to the host
// clip that owns them. Every batch op is one undo step.
class KeyframeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int clipboardCount READ clipboardCount NOTIFY clipboardChanged)

public:
    explicit KeyframeController(AppController *app, QObject *parent = nullptr);

    int clipboardCount() const { return m_clipboard.size(); }

    Q_INVOKABLE QVariantList animatableProperties(int track, int clip) const;

    Q_INVOKABLE void addKeysAt(int track, int clip, const QStringList &props, double seconds);
    Q_INVOKABLE void removeKeysAt(int track, int clip, const QStringList &props, double seconds);
    Q_INVOKABLE void removeKeys(int track, int clip, const QVariantList &keys);
    Q_INVOKABLE void clearProperties(int track, int clip, const QStringList &props);

    Q_INVOKABLE double snapKeyTime(double seconds, double pxPerSecond, const QVariantList &otherKeyTimes,
                                   bool snap) const;

    Q_INVOKABLE QVariantList previewMoveKeys(int track, int clip, const QVariantList &keys,
                                             double dtSeconds);
    Q_INVOKABLE QVariantList moveKeys(int track, int clip, const QVariantList &keys,
                                      double dtSeconds);
    Q_INVOKABLE QVariantList previewStretchKeys(int track, int clip, const QVariantList &keys,
                                                double pivotSeconds, double factor);
    Q_INVOKABLE void setSegmentsEasing(int track, int clip, const QVariantList &keys,
                                       const QVariantMap &spec);

    Q_INVOKABLE int copyKeys(int track, int clip, const QVariantList &keys);
    Q_INVOKABLE QVariantList pasteKeys(int track, int clip, double atSeconds);

signals:
    void clipboardChanged();

private:
    struct ClipKey
    {
        QString prop;
        QString catalogId;
        int occurrence = -1;
        drift::TimeUs offsetUs = 0;
        drift::Keyframe<double> key;
    };

    // A resolved key: `rel` is the key's own time on the host clip, `dv` an optional value offset.
    struct KeyTarget
    {
        QString prop;
        int track = -1;
        int clip = -1;
        drift::TimeUs rel = 0;
        double dv = 0.0;
    };

    struct Moved
    {
        KeyTarget from;
        drift::TimeUs to = 0;
        drift::Keyframe<double> key;
    };

    bool resolveKey(int track, int clip, const QString &prop, double seconds, KeyTarget *out) const;
    QList<KeyTarget> resolveKeys(int track, int clip, const QVariantList &keys) const;
    QList<KeyTarget> expandLinked(const QList<KeyTarget> &keys) const;
    QStringList mirrorLinkedProps(const QStringList &props) const;
    double shownValue(int track, int clip, const QString &prop, double seconds) const;
    bool eraseKeys(const QList<KeyTarget> &keys);
    QVariantMap keyRef(const KeyTarget &key, drift::TimeUs rel) const;
    void restorePreviewTracks(int track, int clip, const QVariantList &keys);
    void relocate(const QList<Moved> &moved);
    QVariantList applyMove(const QList<KeyTarget> &own, double dtSeconds, bool *changed);
    QVariantList applyStretch(const QList<KeyTarget> &own, double pivotSeconds, double factor);

    AppController *m_app = nullptr;
    QList<ClipKey> m_clipboard;
};
