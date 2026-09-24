#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Per-panel state every track's clip renderer reads: the viewport, the zoom, the theme, and the
// panel's live gesture mirrors (a multi-clip move, a linked trim, an effect drag).
//
// One of these per timeline panel, bound once to the panel's own properties. The per-clip QML
// delegates each held a dozen bindings to the same values, which on a scroll or a zoom meant
// re-evaluating all of them for every clip in the project; here a change is one signal per track.
class TimelineViewState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double viewX READ viewX WRITE setViewX NOTIFY viewChanged)
    Q_PROPERTY(double viewW READ viewW WRITE setViewW NOTIFY viewChanged)
    Q_PROPERTY(double pxPerSecond READ pxPerSecond WRITE setPxPerSecond NOTIFY viewChanged)
    Q_PROPERTY(bool touchMode READ touchMode WRITE setTouchMode NOTIFY viewChanged)
    Q_PROPERTY(bool multiSelectActive MEMBER m_multiSelectActive NOTIFY gestureChanged)
    Q_PROPERTY(double totalTracksHeight MEMBER m_totalTracksHeight NOTIFY gestureChanged)

    Q_PROPERTY(bool moveFollowActive MEMBER m_moveFollowActive NOTIFY gestureChanged)
    Q_PROPERTY(int moveLeaderTrack MEMBER m_moveLeaderTrack NOTIFY gestureChanged)
    Q_PROPERTY(int moveLeaderClip MEMBER m_moveLeaderClip NOTIFY gestureChanged)
    Q_PROPERTY(double moveFollowDeltaX MEMBER m_moveFollowDeltaX NOTIFY gestureChanged)
    Q_PROPERTY(double moveFollowDeltaY MEMBER m_moveFollowDeltaY NOTIFY gestureChanged)

    Q_PROPERTY(bool trimFollowActive MEMBER m_trimFollowActive NOTIFY gestureChanged)
    Q_PROPERTY(QString trimFollowLinkId MEMBER m_trimFollowLinkId NOTIFY gestureChanged)
    Q_PROPERTY(QString trimFollowClipId MEMBER m_trimFollowClipId NOTIFY gestureChanged)
    Q_PROPERTY(double trimFollowStart MEMBER m_trimFollowStart NOTIFY gestureChanged)
    Q_PROPERTY(double trimFollowDuration MEMBER m_trimFollowDuration NOTIFY gestureChanged)
    Q_PROPERTY(double trimFollowIn MEMBER m_trimFollowIn NOTIFY gestureChanged)
    Q_PROPERTY(double trimFollowOut MEMBER m_trimFollowOut NOTIFY gestureChanged)

    Q_PROPERTY(int effectDropTrack MEMBER m_effectDropTrack NOTIFY gestureChanged)
    Q_PROPERTY(int effectDropClip MEMBER m_effectDropClip NOTIFY gestureChanged)

    // Theme values, as a map so the panel can hand over the lot in one binding.
    Q_PROPERTY(QVariantMap style READ style WRITE setStyle NOTIFY styleChanged)

public:
    struct Style
    {
        QColor clipVideo{"#2b2b2b"};
        QColor clipAudio{"#8F5DBA"};
        QColor clipText{"#5DBAA0"};
        QColor clipSubtitle{"#4A9FD4"};
        QColor clipGraphic{"#BA5D7A"};
        QColor clipEffect{"#5d93ba"};
        QColor clipComposite{"#BA8F5D"};
        QColor adjustmentVideo{"#5d93ba"};
        QColor adjustmentAudio{"#9B6BC9"};
        QColor adjustmentMask{"#BA9B5D"};
        QColor primary{"#F8B81C"};
        QColor scrim{0, 0, 0, 102};
        QColor proxyBand{0x17, 0x4A, 0x36, 0xD9};
        QColor onMedia{Qt::white};
        QColor waveform{255, 255, 255, 179};
        QColor mutedForeground{Qt::gray};
        QColor panelBorder{Qt::darkGray};
        QColor proxyPill{"#3DBE8B"};
        QColor proxyPillForeground{"#06261A"};
        QColor editFriendlyPill{"#5AA9E6"};
        QColor editFriendlyPillForeground{"#06203A"};
        QColor warning{"#f97316"};
        QString fontFamily;
        double fontSizeTiny = 9.6;
        double fontSizeXs = 11.52;
        double radiusSm = 5.6;
        double radiusXs = 3.0;
        double ringWidth = 1.5;
        double borderWidthFocus = 2.0;
        double headerBandHeight = 20.0;
        double clipMinWidth = 24.0;
        double iconSizeSm = 12.0;
        double spacingLg = 8.0;
        double spacingMd = 6.0;
        double edgeMarginDesktop = 14.0;
        double edgeMarginTouch = 22.0;
        double trimHotspotExtraDesktop = 10.0;
        double trimHotspotExtraTouch = 14.0;
        QString proxyLabel;
        QString proxyTooltip;
        QString editFriendlyLabel;
        QString editFriendlyTooltip;
        QString vfrTooltip;
    };

    explicit TimelineViewState(QObject *parent = nullptr);

    double viewX() const { return m_viewX; }
    void setViewX(double x);
    double viewW() const { return m_viewW; }
    void setViewW(double w);
    double pxPerSecond() const { return m_pxPerSecond; }
    void setPxPerSecond(double pps);
    bool touchMode() const { return m_touchMode; }
    void setTouchMode(bool touch);

    QVariantMap style() const { return m_styleMap; }
    void setStyle(const QVariantMap &style);
    const Style &styleValues() const { return m_style; }

    bool multiSelectActive() const { return m_multiSelectActive; }
    double totalTracksHeight() const { return m_totalTracksHeight; }
    bool moveFollowActive() const { return m_moveFollowActive; }
    int moveLeaderTrack() const { return m_moveLeaderTrack; }
    int moveLeaderClip() const { return m_moveLeaderClip; }
    double moveFollowDeltaX() const { return m_moveFollowDeltaX; }
    double moveFollowDeltaY() const { return m_moveFollowDeltaY; }
    bool trimFollowActive() const { return m_trimFollowActive; }
    const QString &trimFollowLinkId() const { return m_trimFollowLinkId; }
    const QString &trimFollowClipId() const { return m_trimFollowClipId; }
    double trimFollowStart() const { return m_trimFollowStart; }
    double trimFollowDuration() const { return m_trimFollowDuration; }
    double trimFollowIn() const { return m_trimFollowIn; }
    double trimFollowOut() const { return m_trimFollowOut; }
    int effectDropTrack() const { return m_effectDropTrack; }
    int effectDropClip() const { return m_effectDropClip; }

signals:
    void viewChanged();
    void gestureChanged();
    void styleChanged();

private:
    double m_viewX = 0.0;
    double m_viewW = 0.0;
    double m_pxPerSecond = 100.0;
    bool m_touchMode = false;
    bool m_multiSelectActive = false;
    double m_totalTracksHeight = 0.0;
    bool m_moveFollowActive = false;
    int m_moveLeaderTrack = -1;
    int m_moveLeaderClip = -1;
    double m_moveFollowDeltaX = 0.0;
    double m_moveFollowDeltaY = 0.0;
    bool m_trimFollowActive = false;
    QString m_trimFollowLinkId;
    QString m_trimFollowClipId;
    double m_trimFollowStart = 0.0;
    double m_trimFollowDuration = 0.0;
    double m_trimFollowIn = 0.0;
    double m_trimFollowOut = 0.0;
    int m_effectDropTrack = -1;
    int m_effectDropClip = -1;
    QVariantMap m_styleMap;
    Style m_style;
};
