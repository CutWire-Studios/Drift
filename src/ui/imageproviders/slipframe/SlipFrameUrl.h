#pragma once

#include "core/base/Time.h"

#include <QRectF>
#include <QString>

namespace drift {

// image://slipframe/f?path=<percent-encoded>&us=<sourceUs>&rot=<deg>&s=<salt>[&fr=x,y,w,h&ang=<deg>]
// The two frames a slip drag shows in the program monitor: one source frame of one file, decoded
// on request by SlipFrameImageProvider. The salt tells the first and last frame apart so each has
// its own decode stream.
// `frame`/`angle` are the clip's sourceFrame crop and straighten angle; they are left out of the url
// when they are the identity.
QString slipFrameUrl(const QString &path, TimeUs sourceUs, int rotationCorrection, int salt,
                     const QRectF &frame = QRectF(0, 0, 1, 1), double angle = 0.0);

struct SlipFrameRequest
{
    QString path;
    TimeUs sourceUs = 0;
    int rotation = 0;
    int salt = 0;
    QRectF frame{0, 0, 1, 1};
    double angle = 0.0;
};

// Parses the id QML hands the provider (the part after "image://slipframe/"). False when malformed.
bool parseSlipFrameId(const QString &id, SlipFrameRequest *out);

} // namespace drift
