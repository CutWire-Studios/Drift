#pragma once

#include "core/Clip.h"
#include "core/SpeedCurve.h"
#include "playback/ClipPreviewPlayer.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

class AppController;

// Speed-curve editing session, reached from QML as EditorState.speedCurve. The curve is held
// here as a candidate and auditioned through a private single-clip player; the project is not
// touched until apply() mints the retimed copy.
class SpeedCurveController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY sessionChanged)
    Q_PROPERTY(QVariantList points READ points NOTIFY curveChanged)
    Q_PROPERTY(int revision READ revision NOTIFY frameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged)
    Q_PROPERTY(double sourceStart READ sourceStart NOTIFY sessionChanged)
    // Whole media length, not the clip's trimmed span — the filmstrip's frames are sampled across
    // the source file, so placing them needs both.
    Q_PROPERTY(double mediaDuration READ mediaDuration NOTIFY sessionChanged)
    Q_PROPERTY(double sourceDuration READ sourceDuration NOTIFY sessionChanged)
    Q_PROPERTY(double retimedDuration READ retimedDuration NOTIFY curveChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    // Where the playhead sits along the *source*, 0..1 — the graph's own axis.
    Q_PROPERTY(double sourcePosition READ sourcePosition NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString clipName READ clipName NOTIFY sessionChanged)
    Q_PROPERTY(QString clipPath READ clipPath NOTIFY sessionChanged)
    Q_PROPERTY(QString filmstripPath READ filmstripPath NOTIFY sessionChanged)

public:
    explicit SpeedCurveController(AppController *app, QObject *parent = nullptr);

    bool active() const { return m_active; }
    QVariantList points() const;
    Q_INVOKABLE void setPoints(const QVariantList &points);
    int revision() const { return m_revision; }
    QSize frameSize() const { return m_player.frameSize(); }
    double sourceStart() const;
    double mediaDuration() const;
    double sourceDuration() const;
    double retimedDuration() const;
    double position() const;
    double sourcePosition() const;
    bool playing() const { return m_player.isPlaying(); }
    QString clipName() const { return m_clip.name; }
    QString clipPath() const { return m_clip.path; }
    QString filmstripPath() const { return m_clip.filmstripPath; }

    Q_INVOKABLE void begin(int trackIndex, int clipIndex, bool allowNested = false);
    Q_INVOKABLE void end();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(double seconds);
    // Seeks by graph position rather than by retimed time, so clicking the strip lands on the
    // frame under the cursor.
    Q_INVOKABLE void seekAtSource(double position);
    Q_INVOKABLE void apply();

    // C++ only. AppController routes the chosen output device here.
    void setAudioDeviceId(const QByteArray &id);

signals:
    void sessionChanged();
    void curveChanged();
    void frameChanged();
    void positionChanged();
    void playingChanged();
    void applied();

private:
    AppController *m_app = nullptr;
    ClipPreviewPlayer m_player;
    drift::Clip m_clip;
    drift::SpeedCurve m_curve;
    int m_track = -1;
    int m_clipIndex = -1;
    int m_revision = 0;
    bool m_active = false;
};
