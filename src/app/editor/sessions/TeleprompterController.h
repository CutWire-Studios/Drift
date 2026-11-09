#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class AppController;

// Script text and shared scroll state for the teleprompter panel and the preview overlay, reached
// from QML as EditorState.teleprompter. The script is saved with the project but is not undoable.
// Position is measured in lines so each view can scale it by its own line height.
class TeleprompterController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString script READ script WRITE setScript NOTIFY scriptChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayEnabledChanged)
    Q_PROPERTY(bool followRecording READ followRecording WRITE setFollowRecording NOTIFY followRecordingChanged)

public:
    explicit TeleprompterController(AppController *app, QObject *parent = nullptr);

    QString script() const;
    void setScript(const QString &script);
    bool playing() const { return m_playing; }
    void setPlaying(bool playing);
    double position() const { return m_position; }
    void setPosition(double position);
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int fontSize);
    bool overlayEnabled() const { return m_overlayEnabled; }
    void setOverlayEnabled(bool enabled);
    bool followRecording() const { return m_followRecording; }
    void setFollowRecording(bool follow);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void reset();

signals:
    void scriptChanged();
    void playingChanged();
    void positionChanged();
    void speedChanged();
    void fontSizeChanged();
    void overlayEnabledChanged();
    void followRecordingChanged();

private:
    void tick();
    void onRecordingChanged(bool recording);
    void onProjectMetadataChanged();

    AppController *m_app;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QString m_lastScript;
    bool m_playing = false;
    double m_position = 0.0;
    double m_speed = 0.6;
    int m_fontSize = 32;
    bool m_overlayEnabled = false;
    bool m_followRecording = true;
};
