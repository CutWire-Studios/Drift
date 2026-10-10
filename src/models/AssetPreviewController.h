#pragma once

#include "playback/ClipPreviewPlayer.h"

#include <QObject>
#include <QSize>

class AppController;

// Media-bin preview session, reached from QML as EditorState.assetPreview. Drives the phone's
// preview-and-edit page. The asset is auditioned through its own single-clip player rather than
// QtMultimedia: a VideoOutput in a secondary window paints black on Android, and this decodes
// through the same FFmpeg the timeline uses, so whatever the editor plays the preview plays.
class AssetPreviewController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY sessionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY frameChanged)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged)
    Q_PROPERTY(double duration READ duration NOTIFY sessionChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // Set by MediaPreviewWindow.qml/AndroidMediaPreview.qml while open, so the bin grid can defer
    // rebuilding its delegate array (and the scroll-position flicker that causes) until the
    // window closes rather than on every metadata change (rotate, trim, a probe landing) it emits.
    Q_PROPERTY(bool windowOpen READ windowOpen WRITE setWindowOpen NOTIFY windowOpenChanged)

public:
    explicit AssetPreviewController(AppController *app, QObject *parent = nullptr);

    bool active() const { return m_active; }
    int revision() const { return m_revision; }
    QSize frameSize() const { return m_player.frameSize(); }
    double duration() const;
    double position() const;
    bool playing() const { return m_player.isPlaying(); }
    bool windowOpen() const { return m_windowOpen; }
    void setWindowOpen(bool open);

    // begin auditions the bin row; the page owns the trim and crop values and hands them to
    // saveAssetEdit itself.
    Q_INVOKABLE void begin(int index);
    // A timeline clip's whole source, for the crop window.
    Q_INVOKABLE void beginClip(int track, int index);
    Q_INVOKABLE void end();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(double seconds);

    // C++ only. AppController routes the chosen output device here.
    void setAudioDeviceId(const QByteArray &id);

signals:
    void sessionChanged();
    void frameChanged();
    void positionChanged();
    void playingChanged();
    void windowOpenChanged();

private:
    AppController *m_app = nullptr;
    // The synthetic whole-source clip the bin row is auditioned as.
    ClipPreviewPlayer m_player;
    int m_index = -1;
    int m_revision = 0;
    bool m_active = false;
    bool m_windowOpen = false;
};
