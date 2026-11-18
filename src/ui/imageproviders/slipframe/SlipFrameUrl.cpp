#include "ui/imageproviders/slipframe/SlipFrameUrl.h"

#include <QStringList>
#include <QUrl>

namespace drift {

QString slipFrameUrl(const QString &path, TimeUs sourceUs, int rotationCorrection, int salt,
                     const QRectF &frame, double angle)
{
    // Built by concatenation: QString::arg would read the encoded path's own "%2F" as a placeholder.
    QString url = QStringLiteral("image://slipframe/f?path=") + QString::fromLatin1(QUrl::toPercentEncoding(path))
                  + QStringLiteral("&us=") + QString::number(sourceUs) + QStringLiteral("&rot=")
                  + QString::number(rotationCorrection) + QStringLiteral("&s=") + QString::number(salt);
    if (frame != QRectF(0, 0, 1, 1))
        url += QStringLiteral("&fr=") + QString::number(frame.x(), 'g', 9) + QLatin1Char(',')
               + QString::number(frame.y(), 'g', 9) + QLatin1Char(',') + QString::number(frame.width(), 'g', 9)
               + QLatin1Char(',') + QString::number(frame.height(), 'g', 9);
    if (angle != 0.0)
        url += QStringLiteral("&ang=") + QString::number(angle, 'g', 9);
    return url;
}

bool parseSlipFrameId(const QString &id, SlipFrameRequest *out)
{
    const int q = id.indexOf(QLatin1Char('?'));
    if (q < 0)
        return false;
    // Split by hand: QUrlQuery would decode the path, and its value may hold a literal '+' once
    // decoded. Percent-decoding each value ourselves keeps the round trip exact.
    SlipFrameRequest req;
    bool havePath = false;
    bool haveUs = false;
    const QStringList parts = id.mid(q + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0)
            return false;
        const QString key = part.left(eq);
        const QString raw = part.mid(eq + 1);
        bool ok = true;
        if (key == QLatin1String("path")) {
            req.path = QUrl::fromPercentEncoding(raw.toUtf8());
            havePath = !req.path.isEmpty();
        } else if (key == QLatin1String("us")) {
            req.sourceUs = raw.toLongLong(&ok);
            haveUs = ok;
        } else if (key == QLatin1String("rot")) {
            req.rotation = raw.toInt(&ok);
        } else if (key == QLatin1String("s")) {
            req.salt = raw.toInt(&ok);
        } else if (key == QLatin1String("fr")) {
            const QStringList v = raw.split(QLatin1Char(','));
            ok = v.size() == 4;
            if (ok) {
                double n[4] = {0, 0, 1, 1};
                for (int i = 0; i < 4 && ok; ++i)
                    n[i] = v.at(i).toDouble(&ok);
                if (ok && n[2] > 0 && n[3] > 0)
                    req.frame = QRectF(n[0], n[1], n[2], n[3]);
                else
                    ok = false;
            }
        } else if (key == QLatin1String("ang")) {
            req.angle = raw.toDouble(&ok);
        }
        if (!ok)
            return false;
    }
    if (!havePath || !haveUs)
        return false;
    if (out)
        *out = req;
    return true;
}

} // namespace drift
