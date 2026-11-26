#pragma once

#include "app/surfaces/ControlSurfaceManager.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace drift::remote {

inline constexpr int kDefaultPort = 47890;
inline constexpr int kMaxFrame = 64 * 1024;
inline constexpr int kTrackCount = 8;

// A frame is a 4-byte big-endian length and a UTF-8 JSON object.
inline QByteArray encodeFrame(const QJsonObject &object)
{
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray out(4, '\0');
    qToBigEndian(static_cast<quint32>(body.size()), out.data());
    out.append(body);
    return out;
}

// Feed it what a socket delivers; take() returns the next complete object. `broken` is set when
// the stream cannot be framed (oversize or not JSON) and the connection should be dropped.
class FrameReader {
public:
    void append(const QByteArray &bytes) { m_buffer.append(bytes); }

    bool take(QJsonObject *out)
    {
        if (m_buffer.size() < 4)
            return false;
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
        if (length == 0 || length > static_cast<quint32>(kMaxFrame)) {
            m_broken = true;
            return false;
        }
        if (m_buffer.size() < 4 + static_cast<qsizetype>(length))
            return false;
        const QJsonDocument doc = QJsonDocument::fromJson(m_buffer.mid(4, static_cast<qsizetype>(length)));
        m_buffer.remove(0, 4 + static_cast<qsizetype>(length));
        if (!doc.isObject()) {
            m_broken = true;
            return false;
        }
        *out = doc.object();
        return true;
    }

    bool broken() const { return m_broken; }

private:
    QByteArray m_buffer;
    bool m_broken = false;
};

// The phone's controls, as the desktop registers them. The Remote page sends these ids.
inline QList<SurfaceControl> controls()
{
    QList<SurfaceControl> out;
    out.append({QStringLiteral("jog"), QObject::tr("Jog wheel"), SurfaceControl::Relative});
    out.append({QStringLiteral("play"), QObject::tr("Play"), SurfaceControl::Button});
    out.append({QStringLiteral("stop"), QObject::tr("Stop"), SurfaceControl::Button});
    out.append({QStringLiteral("stepBack"), QObject::tr("Step back"), SurfaceControl::Button});
    out.append({QStringLiteral("stepForward"), QObject::tr("Step forward"), SurfaceControl::Button});
    for (const char *ball : {"lift", "gamma", "gain"}) {
        const QString name = QLatin1String(ball);
        out.append({name + QStringLiteral(".x"), QObject::tr("%1 trackball X").arg(name), SurfaceControl::Absolute});
        out.append({name + QStringLiteral(".y"), QObject::tr("%1 trackball Y").arg(name), SurfaceControl::Absolute});
        out.append({name + QStringLiteral(".luma"), QObject::tr("%1 ring").arg(name), SurfaceControl::Absolute});
    }
    for (int i = 1; i <= kTrackCount; ++i)
        out.append({QStringLiteral("fader%1").arg(i), QObject::tr("Fader %1").arg(i), SurfaceControl::Absolute});
    out.append({QStringLiteral("look.x"), QObject::tr("Look pad X"), SurfaceControl::Relative});
    out.append({QStringLiteral("look.y"), QObject::tr("Look pad Y"), SurfaceControl::Relative});
    out.append({QStringLiteral("gyro.x"), QObject::tr("Gyro yaw"), SurfaceControl::Relative});
    out.append({QStringLiteral("gyro.y"), QObject::tr("Gyro pitch"), SurfaceControl::Relative});
    return out;
}

inline QList<SurfaceBindingDef> defaults()
{
    QList<SurfaceBindingDef> out;
    out.append({QStringLiteral("jog"), QStringLiteral("playhead.jog"), {}, false, 1.0});
    out.append({QStringLiteral("play"), QStringLiteral("playPause"), {}, false, 1.0});
    out.append({QStringLiteral("stop"), QStringLiteral("stop"), {}, false, 1.0});
    out.append({QStringLiteral("stepBack"), QStringLiteral("stepBack"), {}, false, 1.0});
    out.append({QStringLiteral("stepForward"), QStringLiteral("stepForward"), {}, false, 1.0});
    for (const char *ball : {"lift", "gamma", "gain"}) {
        const QString name = QLatin1String(ball);
        out.append({name + QStringLiteral(".x"), QStringLiteral("color.") + name + QStringLiteral(".x"), {}, false, 1.0});
        out.append({name + QStringLiteral(".y"), QStringLiteral("color.") + name + QStringLiteral(".y"), {}, false, 1.0});
        out.append({name + QStringLiteral(".luma"), QStringLiteral("color.") + name + QStringLiteral(".luma"), {}, false, 1.0});
    }
    for (int i = 1; i <= kTrackCount; ++i) {
        out.append({QStringLiteral("fader%1").arg(i), QStringLiteral("mixer.volume.%1").arg(i), {}, false, 1.0});
    }
    out.append({QStringLiteral("look.x"), QStringLiteral("view3d.fly.yaw"), {}, false, 1.0});
    out.append({QStringLiteral("look.y"), QStringLiteral("view3d.fly.pitch"), {}, true, 1.0});
    out.append({QStringLiteral("gyro.x"), QStringLiteral("view3d.fly.yaw"), {}, false, 1.0});
    out.append({QStringLiteral("gyro.y"), QStringLiteral("view3d.fly.pitch"), {}, true, 1.0});
    return out;
}

} // namespace drift::remote
