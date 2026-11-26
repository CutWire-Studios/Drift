#include "app/surfaces/osc/OscCodec.h"

#include <QtEndian>

#include <cstring>

namespace drift {

namespace {

struct Reader {
    const QByteArray &data;
    int pos = 0;
    bool ok = true;

    bool has(int n) { return ok && pos + n <= data.size(); }

    QString string()
    {
        const int end = data.indexOf('\0', pos);
        if (end < 0) {
            ok = false;
            return {};
        }
        const QString s = QString::fromUtf8(data.constData() + pos, end - pos);
        pos = (end + 4) & ~3;
        if (pos > data.size())
            ok = false;
        return s;
    }

    qint32 int32()
    {
        if (!has(4)) {
            ok = false;
            return 0;
        }
        const qint32 v = qFromBigEndian<qint32>(data.constData() + pos);
        pos += 4;
        return v;
    }

    qint64 int64()
    {
        if (!has(8)) {
            ok = false;
            return 0;
        }
        const qint64 v = qFromBigEndian<qint64>(data.constData() + pos);
        pos += 8;
        return v;
    }

    double float32()
    {
        const quint32 bits = static_cast<quint32>(int32());
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }

    double float64()
    {
        const quint64 bits = static_cast<quint64>(int64());
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }

    QByteArray blob()
    {
        const int size = int32();
        if (size < 0 || !has(size)) {
            ok = false;
            return {};
        }
        const QByteArray b = data.mid(pos, size);
        pos = (pos + size + 3) & ~3;
        return b;
    }
};

void readMessage(Reader &r, int end, QList<OscMessage> &out)
{
    OscMessage m;
    m.address = r.string();
    if (!r.ok || !m.address.startsWith(QLatin1Char('/')))
        return;
    if (r.pos >= end) {
        out.append(m);
        return;
    }
    const QString tags = r.string();
    if (!r.ok || !tags.startsWith(QLatin1Char(',')))
        return;
    for (int i = 1; i < tags.size() && r.ok; ++i) {
        switch (tags.at(i).toLatin1()) {
        case 'i':
            m.args.append(r.int32());
            break;
        case 'f':
            m.args.append(r.float32());
            break;
        case 'd':
            m.args.append(r.float64());
            break;
        case 'h':
        case 't':
            m.args.append(r.int64());
            break;
        case 's':
        case 'S':
            m.args.append(r.string());
            break;
        case 'b':
            m.args.append(r.blob());
            break;
        case 'c':
            m.args.append(QString(QChar(static_cast<ushort>(r.int32()))));
            break;
        case 'r':
        case 'm':
            m.args.append(r.int32());
            break;
        case 'T':
            m.args.append(true);
            break;
        case 'F':
            m.args.append(false);
            break;
        case 'N':
        case 'I':
            m.args.append(QVariant());
            break;
        default:
            r.ok = false;
        }
    }
    if (r.ok)
        out.append(m);
}

void readPacket(Reader &r, int end, QList<OscMessage> &out, int depth)
{
    if (depth > 8 || end - r.pos < 4)
        return;
    if (r.data.at(r.pos) == '#') {
        if (r.string() != QLatin1String("#bundle"))
            return;
        r.int64();
        while (r.ok && r.pos + 4 <= end) {
            const int size = r.int32();
            if (size < 4 || r.pos + size > end) {
                r.ok = false;
                return;
            }
            const int elementEnd = r.pos + size;
            readPacket(r, elementEnd, out, depth + 1);
            r.pos = elementEnd;
        }
        return;
    }
    readMessage(r, end, out);
}

void pad(QByteArray &out)
{
    while (out.size() % 4 != 0)
        out.append('\0');
}

void appendString(QByteArray &out, const QString &s)
{
    out.append(s.toUtf8());
    out.append('\0');
    pad(out);
}

void appendInt(QByteArray &out, qint32 v)
{
    char buf[4];
    qToBigEndian(v, buf);
    out.append(buf, 4);
}

} // namespace

QList<OscMessage> parseOscPacket(const QByteArray &packet)
{
    QList<OscMessage> out;
    Reader r{packet};
    readPacket(r, static_cast<int>(packet.size()), out, 0);
    return out;
}

QByteArray encodeOscMessage(const OscMessage &message)
{
    QByteArray out;
    appendString(out, message.address);
    QString tags = QStringLiteral(",");
    QByteArray body;
    for (const QVariant &v : message.args) {
        switch (v.typeId()) {
        case QMetaType::Bool:
            tags += v.toBool() ? QLatin1Char('T') : QLatin1Char('F');
            break;
        case QMetaType::Int:
        case QMetaType::LongLong:
            tags += QLatin1Char('i');
            appendInt(body, v.toInt());
            break;
        case QMetaType::QString:
            tags += QLatin1Char('s');
            appendString(body, v.toString());
            break;
        case QMetaType::QByteArray: {
            tags += QLatin1Char('b');
            const QByteArray b = v.toByteArray();
            appendInt(body, static_cast<qint32>(b.size()));
            body.append(b);
            pad(body);
            break;
        }
        default: {
            tags += QLatin1Char('f');
            const float f = v.toFloat();
            quint32 bits;
            std::memcpy(&bits, &f, sizeof bits);
            appendInt(body, static_cast<qint32>(bits));
        }
        }
    }
    appendString(out, tags);
    out.append(body);
    return out;
}

} // namespace drift
