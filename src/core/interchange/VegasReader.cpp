#include "core/interchange/VegasReader.h"

#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "engine/decode/probe/MediaProbe.h"

#include <QDir>
#include <QColor>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace drift::vegas {

namespace {
static bool isEditFriendlyCodec(const QString &codecName)
{
    const QString lower = codecName.toLower();
    return lower == QLatin1String("prores")
        || lower == QLatin1String("dnxhd")
        || lower == QLatin1String("ffv1")
        || lower == QLatin1String("rawvideo")
        || lower == QLatin1String("cfhd");
}


// 16-byte Sony/MAGIX Wave64 RIFF chunk and type GUIDs (little-endian)
static const uint8_t kGuidW64Riff[16] = {
    0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kGuidW64List[16] = {
    0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kTypeMediaPool[16] = {
    0x5b, 0x2d, 0x8f, 0xb2, 0x0f, 0x23, 0xd2, 0x11, 0x86, 0xaf, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTimeline[16] = {
    0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTrack[16] = {
    0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventContainer[16] = {
    0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEvent[16] = {
    0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTime[16] = {
    0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTake[16] = {
    0x6d, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventGroupList[16] = {
    0x18, 0x3e, 0xe7, 0xb8, 0x4c, 0x7f, 0x6b, 0x45, 0x93, 0xcd, 0x68, 0x66, 0xb9, 0x5e, 0x5f, 0x2b
};
static const uint8_t kChunkEventGroup[16] = {
    0x32, 0xe1, 0x01, 0xe9, 0x77, 0xc6, 0x04, 0x46, 0xac, 0x9d, 0xdb, 0x0b, 0x87, 0xb4, 0xee, 0xbd
};
static const uint8_t kListAudioTrack[16] = {
    0x26, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkAudioTrackProps[16] = {
    0x27, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeMarkerList[16] = {
    0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
};
static const uint8_t kGuidMarker[16] = {
    0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};

inline quint32 readU32LE(const uint8_t *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

inline quint64 readU64LE(const uint8_t *p)
{
    const quint64 lo = readU32LE(p);
    const quint64 hi = readU32LE(p + 4);
    return lo | (hi << 32);
}

inline double readDoubleLE(const uint8_t *p)
{
    const quint64 val = readU64LE(p);
    double d = 0.0;
    std::memcpy(&d, &val, sizeof(d));
    return d;
}

inline float readFloatLE(const uint8_t *p)
{
    const quint32 val = readU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &val, sizeof(f));
    return f;
}

// Chunk sizes come from the file, so callers check them against the bytes left before advancing.
inline quint64 alignedSize(quint64 size)
{
    return (size + 7) & ~quint64(7);
}

inline TimeUs ticksToUs(quint64 ticks)
{
    constexpr quint64 kMaxUs = 10'000'000'000'000ULL;
    return static_cast<TimeUs>(std::min<quint64>(ticks / 10, kMaxUs));
}

QString utf16Copy(const uint8_t *data, qsizetype chars)
{
    QString s(chars, Qt::Uninitialized);
    std::memcpy(s.data(), data, size_t(chars) * 2);
    return s;
}

QString readUtf16String(const uint8_t *data, qsizetype size, qsizetype offset)
{
    if (offset < 0 || offset + 2 > size)
        return {};
    const uint8_t *p = data + offset;
    const qsizetype maxChars = (size - offset) / 2;
    qsizetype len = 0;
    while (len < maxChars && (p[len * 2] | p[len * 2 + 1]) != 0)
        ++len;
    return utf16Copy(p, len);
}

double readUnitDouble(const uint8_t *p)
{
    const double v = readDoubleLE(p);
    return std::isnan(v) ? 0.0 : qBound(0.0, v, 1.0);
}

QColor readRgbaColor(const uint8_t *p)
{
    return QColor::fromRgbF(readUnitDouble(p), readUnitDouble(p + 8), readUnitDouble(p + 16), readUnitDouble(p + 24));
}

struct ParsedMarker
{
    quint32 id = 0;
    TimeUs posUs = 0;
    TimeUs durUs = 0;
    bool isRegion = false;
    QString name;
};

std::optional<ParsedMarker> parseMarkerPayload(const uint8_t *p, qsizetype avail)
{
    if (avail < 28)
        return std::nullopt;

    quint32 ident = 0;
    qint64 posTicks = 0;
    qint64 lenTicks = -1;
    quint32 nameBytes = 0;
    const uint8_t *namePtr = nullptr;

    const quint32 val0 = readU32LE(p);
    const quint32 val4 = readU32LE(p + 4);
    const quint32 val8 = (avail >= 12) ? readU32LE(p + 8) : 0;

    if (val4 == 28 && avail >= 32) {
        // Format A (property format): uint32 dataSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 8);
        posTicks = static_cast<qint64>(readU64LE(p + 12));
        lenTicks = static_cast<qint64>(readU64LE(p + 20));
        nameBytes = readU32LE(p + 28);
        namePtr = p + 32;
    } else if (val8 == 28 && avail >= 36) {
        // Format B (Wave64 64-bit chunk): uint64 chunkSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 12);
        posTicks = static_cast<qint64>(readU64LE(p + 16));
        lenTicks = static_cast<qint64>(readU64LE(p + 24));
        nameBytes = readU32LE(p + 32);
        namePtr = p + 36;
    } else if (val0 == 28 && avail >= 28) {
        // Format C (direct payload): uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 4);
        posTicks = static_cast<qint64>(readU64LE(p + 8));
        lenTicks = static_cast<qint64>(readU64LE(p + 16));
        nameBytes = readU32LE(p + 24);
        namePtr = p + 28;
    } else {
        return std::nullopt;
    }

    if (posTicks < 0)
        posTicks = 0;
    if (lenTicks < 0)
        lenTicks = 0;

    ParsedMarker m;
    m.id = ident;
    m.posUs = ticksToUs(quint64(posTicks));
    if (lenTicks > 0) {
        m.isRegion = true;
        m.durUs = ticksToUs(quint64(lenTicks));
    } else {
        m.isRegion = false;
        m.durUs = 0;
    }

    if (nameBytes > 0 && quint64(nameBytes) <= quint64(avail - (namePtr - p)))
        m.name = readUtf16String(namePtr, qsizetype(nameBytes), 0).trimmed();

    return m;
}

void detectTransitionsForTrack(Track &track)
{
    if (track.clips.size() < 2)
        return;

    QList<int> sortedIndices;
    sortedIndices.reserve(track.clips.size());
    for (int i = 0; i < track.clips.size(); ++i)
        sortedIndices.append(i);

    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        if (track.clips[a].timelineStart != track.clips[b].timelineStart)
            return track.clips[a].timelineStart < track.clips[b].timelineStart;
        return track.clips[a].timelineDuration > track.clips[b].timelineDuration;
    });

    for (int idx = 0; idx < sortedIndices.size() - 1; ++idx) {
        const Clip &fromClip = track.clips[sortedIndices[idx]];
        const Clip &toClip = track.clips[sortedIndices[idx + 1]];

        const TimeUs fromEnd = fromClip.timelineEnd();
        const TimeUs toStart = toClip.timelineStart;

        TimeUs durationUs = 0;
        bool isTransition = false;

        // Case 1: Physical overlap on the timeline
        if (toStart > fromClip.timelineStart && toStart < fromEnd) {
            const TimeUs overlap = fromEnd - toStart;
            if (overlap >= 1000) {
                durationUs = overlap;
                isTransition = true;
            }
        }
        // Case 2: Abutting clips with fade-out on outgoing and fade-in on incoming
        else if (qAbs(toStart - fromEnd) <= 1000 && fromClip.fadeOutUs > 0 && toClip.fadeInUs > 0) {
            const TimeUs span = fromClip.fadeOutUs + toClip.fadeInUs;
            if (span >= 1000) {
                durationUs = span;
                isTransition = true;
            }
        }

        if (isTransition && durationUs > 0) {
            bool exists = false;
            for (const Transition &existing : track.transitions) {
                if (existing.fromClipId == fromClip.id && existing.toClipId == toClip.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                Transition tr;
                tr.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                tr.fromClipId = fromClip.id;
                tr.toClipId = toClip.id;
                tr.kindId = QStringLiteral("crossfade");
                tr.durationUs = durationUs;
                tr.easingCurve = FadeCurve::Linear;
                track.transitions.append(tr);
            }
        }
    }
}

qsizetype findUtf16(const uint8_t *data, qsizetype size, const char *ascii)
{
    const qsizetype asciiLen = static_cast<qsizetype>(std::strlen(ascii));
    if (size < asciiLen * 2)
        return -1;
    for (qsizetype i = 0; i + asciiLen * 2 <= size; i += 2) {
        bool match = true;
        for (qsizetype j = 0; j < asciiLen; ++j) {
            if (data[i + j * 2] != static_cast<uint8_t>(ascii[j]) || data[i + j * 2 + 1] != 0) {
                match = false;
                break;
            }
        }
        if (match)
            return i;
    }
    return -1;
}

double parseDoubleParam(const uint8_t *data, qsizetype size, const char *paramName, double defaultVal)
{
    const qsizetype pos = findUtf16(data, size, paramName);
    if (pos < 0)
        return defaultVal;
    const qsizetype valPos = pos + static_cast<qsizetype>(std::strlen(paramName) * 2) + 2;
    if (valPos + 8 <= size) {
        const double v = readDoubleLE(data + valPos);
        if (!std::isnan(v) && !std::isinf(v))
            return v;
    }
    return defaultVal;
}

struct VegasParsedRtf
{
    QString text;
    QString fontFamily = QStringLiteral("Inter");
    int pixelSize = 64;
    int fontWeight = 400;
    bool italic = false;
    TextAlign align = TextAlign::Center;
};

VegasParsedRtf parseVegasRtf(const QString &rtf)
{
    VegasParsedRtf res;
    if (rtf.isEmpty())
        return res;

    const int fontTblIdx = rtf.indexOf(QLatin1String("\\fonttbl"));
    if (fontTblIdx >= 0) {
        const int endTbl = rtf.indexOf(QLatin1String("}}"), fontTblIdx);
        if (endTbl > fontTblIdx) {
            const QString tblStr = rtf.mid(fontTblIdx, endTbl - fontTblIdx);
            const int fcIdx = tblStr.indexOf(QLatin1String("fcharset0"));
            if (fcIdx >= 0) {
                const int semi = tblStr.indexOf(QLatin1Char(';'), fcIdx);
                if (semi > fcIdx + 9) {
                    const QString font = tblStr.mid(fcIdx + 9, semi - (fcIdx + 9)).trimmed();
                    if (!font.isEmpty())
                        res.fontFamily = font;
                }
            } else {
                const int f0Idx = tblStr.indexOf(QLatin1String("\\f0"));
                if (f0Idx >= 0) {
                    const int semi = tblStr.indexOf(QLatin1Char(';'), f0Idx);
                    if (semi > f0Idx) {
                        const QString segment = tblStr.mid(f0Idx, semi - f0Idx);
                        const int space = segment.lastIndexOf(QLatin1Char(' '));
                        if (space >= 0) {
                            const QString font = segment.mid(space + 1).trimmed();
                            if (!font.isEmpty())
                                res.fontFamily = font;
                        }
                    }
                }
            }
        }
    }

    if (rtf.contains(QLatin1String("\\ql")))
        res.align = TextAlign::Left;
    else if (rtf.contains(QLatin1String("\\qr")))
        res.align = TextAlign::Right;
    else if (rtf.contains(QLatin1String("\\qc")))
        res.align = TextAlign::Center;

    static const QRegularExpression fsRegex(QStringLiteral(R"(\\fs(\d+))"));
    const auto fsMatch = fsRegex.match(rtf);
    if (fsMatch.hasMatch()) {
        const int halfPoints = fsMatch.captured(1).toInt();
        res.pixelSize = qBound(12, static_cast<int>(halfPoints * 0.8), 256);
    }

    if (rtf.contains(QLatin1String("\\b ")) || rtf.contains(QLatin1String("\\b\\")) || rtf.contains(QLatin1String("\\b1")))
        res.fontWeight = 700;
    if (rtf.contains(QLatin1String("\\i ")) || rtf.contains(QLatin1String("\\i\\")) || rtf.contains(QLatin1String("\\i1")))
        res.italic = true;

    QString text;
    int groupDepth = 0;
    bool skipGroup = false;
    int skipDepth = 0;
    int i = 0;
    const int len = rtf.size();

    while (i < len) {
        const QChar ch = rtf.at(i);
        if (ch == QLatin1Char('{')) {
            ++groupDepth;
            if (rtf.mid(i, 9) == QLatin1String("{\\fonttbl")
                || rtf.mid(i, 9) == QLatin1String("{\\colortb")
                || rtf.mid(i, 9) == QLatin1String("{\\stylesh")
                || rtf.mid(i, 3) == QLatin1String("{\\*")) {
                skipGroup = true;
                skipDepth = groupDepth;
            }
            ++i;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            if (skipGroup && groupDepth == skipDepth)
                skipGroup = false;
            --groupDepth;
            ++i;
            continue;
        }

        if (skipGroup) {
            ++i;
            continue;
        }

        if (ch == QLatin1Char('\\')) {
            const bool isPar = (rtf.mid(i, 4) == QLatin1String("\\par")
                                && (i + 4 >= len || !rtf.at(i + 4).isLetter()));
            const bool isLine = (rtf.mid(i, 5) == QLatin1String("\\line")
                                 && (i + 5 >= len || !rtf.at(i + 5).isLetter()));
            if (isPar || isLine) {
                text.append(QLatin1Char('\n'));
                i += isPar ? 4 : 5;
                while (i < len && (rtf.at(i) == QLatin1Char(' ') || rtf.at(i) == QLatin1Char('\r') || rtf.at(i) == QLatin1Char('\n')))
                    ++i;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\'") && i + 4 <= len) {
                bool ok = false;
                const int code = rtf.mid(i + 2, 2).toInt(&ok, 16);
                if (ok) {
                    char c = static_cast<char>(code);
                    text.append(QString::fromLatin1(&c, 1));
                }
                i += 4;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\\\")) {
                text.append(QLatin1Char('\\'));
                i += 2;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\{")) {
                text.append(QLatin1Char('{'));
                i += 2;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\}")) {
                text.append(QLatin1Char('}'));
                i += 2;
                continue;
            }
            ++i;
            while (i < len && (rtf.at(i).isLetterOrNumber() || rtf.at(i) == QLatin1Char('-')))
                ++i;
            if (i < len && rtf.at(i) == QLatin1Char(' '))
                ++i;
            continue;
        }

        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            ++i;
            continue;
        }

        text.append(ch);
        ++i;
    }

    res.text = text.trimmed();
    return res;
}

bool extractTitlesAndText(const uint8_t *payload, qsizetype payloadSize,
                         QString *outText, TextStyle *outStyle,
                         double *outScale, QPointF *outLocation)
{
    const qsizetype rtfPos = findUtf16(payload, payloadSize, "rtf1");
    if (rtfPos < 0)
        return false;

    qsizetype ob = -1;
    for (qsizetype i = rtfPos - 2; i >= 0 && i >= rtfPos - 64; i -= 2) {
        if (payload[i] == '{' && payload[i + 1] == 0) {
            ob = i;
            break;
        }
    }
    if (ob < 0)
        ob = std::max<qsizetype>(0, rtfPos - 4);

    qsizetype cb = ob;
    int depth = 0;
    while (cb + 2 <= payloadSize) {
        if (payload[cb] == '{' && payload[cb + 1] == 0) {
            ++depth;
        } else if (payload[cb] == '}' && payload[cb + 1] == 0) {
            --depth;
            if (depth == 0) {
                cb += 2;
                break;
            }
        }
        cb += 2;
    }

    if (cb <= ob)
        return false;

    const QString rtfStr = utf16Copy(payload + ob, (cb - ob) / 2);

    const VegasParsedRtf parsed = parseVegasRtf(rtfStr);
    if (outText)
        *outText = parsed.text;

    if (outStyle) {
        outStyle->fontFamily = parsed.fontFamily;
        outStyle->pixelSize = parsed.pixelSize;
        outStyle->fontWeight = parsed.fontWeight;
        outStyle->italic = parsed.italic;
        outStyle->align = parsed.align;
    }

    QColor textColor = Qt::white;
    const qsizetype tcPos = findUtf16(payload, payloadSize, "TextColor");
    if (tcPos >= 0) {
        const qsizetype valPos = tcPos + static_cast<qsizetype>(std::strlen("TextColor") * 2) + 2;
        if (valPos + 32 <= payloadSize) {
            textColor = readRgbaColor(payload + valPos);
        }
    }
    if (outStyle)
        setSolidFill(*outStyle, textColor);

    if (outScale) {
        const double s = parseDoubleParam(payload, payloadSize, "Scale", 1.0);
        if (s > 0.01 && s < 20.0)
            *outScale = s;
    }

    if (outLocation) {
        const qsizetype locPos = findUtf16(payload, payloadSize, "Location");
        if (locPos >= 0) {
            const qsizetype valPos = locPos + static_cast<qsizetype>(std::strlen("Location") * 2) + 2;
            if (valPos + 16 <= payloadSize) {
                const double lx = readDoubleLE(payload + valPos);
                const double ly = readDoubleLE(payload + valPos + 8);
                if (std::isfinite(lx) && std::isfinite(ly))
                    *outLocation = QPointF(lx, ly);
            }
        }
    }

    const qsizetype trPos = findUtf16(payload, payloadSize, "Tracking");
    if (trPos >= 0 && outStyle) {
        const double tr = parseDoubleParam(payload, payloadSize, "Tracking", 0.0);
        outStyle->letterSpacing = tr * 20.0;
    }

    const qsizetype owPos = findUtf16(payload, payloadSize, "OutlineWidth");
    if (owPos >= 0 && outStyle) {
        const double ow = parseDoubleParam(payload, payloadSize, "OutlineWidth", 0.0);
        if (ow > 0.001) {
            QColor outlineColor = Qt::black;
            const qsizetype ocPos = findUtf16(payload, payloadSize, "OutlineColor");
            if (ocPos >= 0) {
                const qsizetype ocVal = ocPos + static_cast<qsizetype>(std::strlen("OutlineColor") * 2) + 2;
                if (ocVal + 32 <= payloadSize) {
                    outlineColor = readRgbaColor(payload + ocVal);
                }
            }
            outStyle->layers.prepend(strokeLayer(ow * 10.0, outlineColor));
        }
    }

    const qsizetype sePos = findUtf16(payload, payloadSize, "ShadowEnable");
    if (sePos >= 0 && outStyle) {
        const qsizetype valPos = sePos + static_cast<qsizetype>(std::strlen("ShadowEnable") * 2) + 2;
        if (valPos + 4 <= payloadSize) {
            const quint32 enable = readU32LE(payload + valPos);
            if (enable != 0) {
                QColor shadowColor = QColor(0, 0, 0, 180);
                const qsizetype scPos = findUtf16(payload, payloadSize, "ShadowColor");
                if (scPos >= 0) {
                    const qsizetype scVal = scPos + static_cast<qsizetype>(std::strlen("ShadowColor") * 2) + 2;
                    if (scVal + 32 <= payloadSize) {
                        shadowColor = readRgbaColor(payload + scVal);
                    }
                }
                const double ox = parseDoubleParam(payload, payloadSize, "ShadowOffsetX", 0.05) * 50.0;
                const double oy = parseDoubleParam(payload, payloadSize, "ShadowOffsetY", 0.05) * 50.0;
                const double blur = parseDoubleParam(payload, payloadSize, "ShadowBlur", 0.1) * 50.0;
                outStyle->layers.prepend(shadowLayer(shadowColor, ox, oy, blur, shadowColor.alphaF()));
            }
        }
    }

    return true;
}

QColor extractSolidColor(const uint8_t *payload, qsizetype payloadSize)
{
    const qsizetype cPos = findUtf16(payload, payloadSize, "Color");
    if (cPos >= 0) {
        const qsizetype valPos = cPos + static_cast<qsizetype>(std::strlen("Color") * 2) + 2;
        if (valPos + 32 <= payloadSize)
            return readRgbaColor(payload + valPos);
    }
    return Qt::black;
}

QList<Effect> extractVideoEffects(const uint8_t *payload, qsizetype payloadSize)
{
    QList<Effect> effects;
    if (payloadSize < 16)
        return effects;

    // 1. Levels
    const qsizetype lPos = findUtf16(payload, payloadSize, "levels}");
    if (lPos >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - lPos, 800);
        const uint8_t *sub = payload + lPos;

        const double inStart = parseDoubleParam(sub, subSize, "InputStart", 0.0);
        const double inEnd = parseDoubleParam(sub, subSize, "InputEnd", 1.0);
        const double gamma = parseDoubleParam(sub, subSize, "Gamma", 1.0);
        const double outStart = parseDoubleParam(sub, subSize, "OutputStart", 0.0);
        const double outEnd = parseDoubleParam(sub, subSize, "OutputEnd", 1.0);

        if (inStart > 0.001 || inEnd < 0.999 || std::abs(gamma - 1.0) > 0.01 || outStart > 0.001 || outEnd < 0.999) {
            Effect fx;
            fx.catalogId = QStringLiteral("levels");
            fx.name = QStringLiteral("Levels");
            fx.parameters[QStringLiteral("inBlack")] = inStart;
            fx.parameters[QStringLiteral("inWhite")] = inEnd;
            fx.parameters[QStringLiteral("gamma")] = gamma;
            fx.parameters[QStringLiteral("outBlack")] = outStart;
            fx.parameters[QStringLiteral("outWhite")] = outEnd;
            effects.append(fx);
        }
    }

    // 2. Color Corrector
    const qsizetype cPos = findUtf16(payload, payloadSize, "colorcorrector}");
    if (cPos >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - cPos, 1000);
        const uint8_t *sub = payload + cPos;

        const double sat = parseDoubleParam(sub, subSize, "Saturation", 1.0);
        const double gamma = parseDoubleParam(sub, subSize, "Gamma", 1.0);
        const double gain = parseDoubleParam(sub, subSize, "Gain", 1.0);
        const double offset = parseDoubleParam(sub, subSize, "Offset", 0.0);

        if (std::abs(sat - 1.0) > 0.01 || std::abs(gamma - 1.0) > 0.01 || std::abs(gain - 1.0) > 0.01 || std::abs(offset) > 0.01) {
            Effect fx;
            fx.catalogId = QStringLiteral("color.grade");
            fx.name = QStringLiteral("Color Corrector");
            fx.parameters[QStringLiteral("saturation")] = sat;
            fx.parameters[QStringLiteral("gammaLuma")] = gamma - 1.0;
            fx.parameters[QStringLiteral("gainLuma")] = gain - 1.0;
            fx.parameters[QStringLiteral("liftLuma")] = offset / 100.0;
            effects.append(fx);
        }
    }

    // 3. Sharpen
    const qsizetype sPos = findUtf16(payload, payloadSize, "sharpen}");
    if (sPos >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - sPos, 400);
        const uint8_t *sub = payload + sPos;

        const double amount = parseDoubleParam(sub, subSize, "Amount", 0.0);
        if (amount > 0.01) {
            Effect fx;
            fx.catalogId = QStringLiteral("stylize.sharpen");
            fx.name = QStringLiteral("Sharpen");
            fx.parameters[QStringLiteral("luma_amount")] = amount;
            effects.append(fx);
        }
    }

    return effects;
}

QString extractPathFromPayload(const uint8_t *data, qsizetype size)
{
    if (size < 4)
        return {};

    // 1. Search for UTF-16LE Windows drive separator ":\" (0x3a, 0x00, 0x5c, 0x00) or ":/" (0x3a, 0x00, 0x2f, 0x00)
    static const uint8_t kColonBackslash[4] = { 0x3a, 0x00, 0x5c, 0x00 };
    static const uint8_t kColonSlash[4] = { 0x3a, 0x00, 0x2f, 0x00 };
    for (qsizetype i = 0; i + 4 <= size; i += 2) {
        if (std::memcmp(data + i, kColonBackslash, 4) == 0 || std::memcmp(data + i, kColonSlash, 4) == 0) {
            const qsizetype start = (i >= 2) ? (i - 2) : i;
            const QString path = readUtf16String(data, size, start);
            if (!path.isEmpty())
                return path;
        }
    }

    // 2. Search for UTF-16LE "META:\"
    static const uint8_t kMetaPrefix[12] = { 'M', 0, 'E', 0, 'T', 0, 'A', 0, ':', 0, '\\', 0 };
    for (qsizetype i = 0; i + 12 <= size; i += 2) {
        if (std::memcmp(data + i, kMetaPrefix, 12) == 0) {
            const QString path = readUtf16String(data, size, i);
            if (!path.isEmpty())
                return path;
        }
    }

    // 3. Fallback to common VEGAS payload offsets (96, 102, 128, 134, 64)
    for (const qsizetype off : { 96, 102, 128, 134, 64 }) {
        if (off + 2 <= size) {
            const QString s = readUtf16String(data, size, off);
            if (!s.isEmpty() && (s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\'))))
                return s;
        }
    }

    return {};
}

QString cleanMediaPath(const QString &rawPath)
{
    QString s = rawPath;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Handle META:\SubClip\{<path>}
    const int openBrace = s.indexOf(QLatin1Char('{'));
    const int closeBrace = s.indexOf(QLatin1Char('}'), openBrace + 1);
    if (openBrace >= 0 && closeBrace > openBrace) {
        const QString inside = s.mid(openBrace + 1, closeBrace - openBrace - 1);
        if (inside.contains(QLatin1Char('/')) || inside.contains(QLatin1Char('.')))
            s = inside;
    }
    return s;
}

QString cleanMediaFilename(const QString &rawPath)
{
    QString diskName = QFileInfo(cleanMediaPath(rawPath)).fileName();
    const int braceIdx = diskName.indexOf(QLatin1Char('}'));
    if (braceIdx > 0 && !diskName.startsWith(QLatin1Char('{')))
        diskName = diskName.left(braceIdx);
    return diskName;
}

// The stored path when it exists here, else the same file found under the project's own folder,
// else the stored path unchanged so the asset shows up as missing media.
QString resolveMediaFile(const QString &winPath, const QString &diskName, const QString &sourceDir,
                         const QHash<QString, QStringList> &indexedFiles)
{
    if (diskName.isEmpty())
        return {};

    const QString stored = cleanMediaPath(winPath);
    const QFileInfo storedInfo(stored);
    if (storedInfo.isAbsolute() && storedInfo.exists())
        return storedInfo.canonicalFilePath();

    const QStringList parts = stored.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!sourceDir.isEmpty()) {
        const QDir dir(sourceDir);
        for (int i = std::min<int>(4, parts.size()); i >= 1; --i) {
            const QString sub = parts.mid(parts.size() - i).join(QLatin1Char('/'));
            if (dir.exists(sub))
                return dir.filePath(sub);
        }

        const auto it = indexedFiles.constFind(diskName.toLower());
        if (it != indexedFiles.constEnd()) {
            if (parts.size() >= 2) {
                for (const QString &c : it.value()) {
                    if (c.contains(parts.at(parts.size() - 2), Qt::CaseInsensitive))
                        return c;
                }
            }
            return it.value().first();
        }
    }

    return stored.isEmpty() ? diskName : stored;
}

struct ImportContext
{
    QSet<QString> stack;
    QHash<QString, std::shared_ptr<const Project>> parsed;
    int depth = 0;
    int skipped = 0;
};

std::optional<Project> readDataImpl(const QByteArray &data, const QString &sourceDir, QString *error,
                                    ImportContext &ctx);

} // namespace

bool isVegasData(const QByteArray &data)
{
    if (data.size() < 40)
        return false;
    return std::memcmp(data.constData(), kGuidW64Riff, 16) == 0;
}

bool isVegasProject(const QString &filePath)
{
    if (filePath.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive))
        return true;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(40);
    return isVegasData(header);
}

namespace {

std::optional<Project> readFileImpl(const QString &filePath, QString *error, ImportContext &ctx)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Could not open file: %1").arg(filePath);
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    const QFileInfo info(filePath);
    const QString canon = info.canonicalFilePath();
    if (!canon.isEmpty())
        ctx.stack.insert(canon);
    auto result = readDataImpl(data, info.canonicalPath(), error, ctx);
    ctx.stack.remove(canon);
    return result;
}

std::shared_ptr<const Project> loadNestedProject(ImportContext &ctx, const QString &canon, const QString &path)
{
    if (canon.isEmpty() || ctx.stack.contains(canon) || ctx.depth >= 8)
        return nullptr;
    const auto cached = ctx.parsed.constFind(canon);
    if (cached != ctx.parsed.constEnd())
        return cached.value();

    ++ctx.depth;
    QString nestedError;
    auto parsed = readFileImpl(path, &nestedError, ctx);
    --ctx.depth;
    std::shared_ptr<const Project> result;
    if (parsed)
        result = std::make_shared<const Project>(std::move(*parsed));
    ctx.parsed.insert(canon, result);
    return result;
}

std::optional<Project> readDataImpl(const QByteArray &data, const QString &sourceDir, QString *error,
                                    ImportContext &ctx)
{
    if (data.size() < 64 || !isVegasData(data)) {
        if (error)
            *error = QObject::tr("Not a valid VEGAS Pro project file");
        return std::nullopt;
    }

    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    const qsizetype totalSize = data.size();

    // Chunk 0: Project settings (starts at offset 40)
    const quint64 c0Size = readU64LE(bytes + 56);
    if (c0Size < 24 || c0Size > quint64(totalSize - 40)) {
        if (error)
            *error = QObject::tr("The VEGAS Pro project header is corrupted");
        return std::nullopt;
    }

    const uint8_t *c0Payload = bytes + 64;
    quint32 sampleRate = 48000;
    if (c0Size >= 40)
        sampleRate = readU32LE(c0Payload + 12);
    if (sampleRate < 8000 || sampleRate > 192000)
        sampleRate = 48000;

    double fps = 23.976023976023978;
    if (c0Size >= 48) {
        const double readFps = readDoubleLE(c0Payload + 16);
        if (readFps >= 1.0 && readFps <= 240.0)
            fps = readFps;
    }

    int canvasWidth = 1920;
    int canvasHeight = 1080;
    if (c0Size >= 472) {
        const quint32 wCand = readU32LE(c0Payload + 440);
        const quint32 hCand = readU32LE(c0Payload + 444);
        if (wCand >= 320 && wCand <= 7680 && hCand >= 240 && hCand <= 4320) {
            canvasWidth = static_cast<int>(wCand);
            canvasHeight = static_cast<int>(hCand);
        }
    }

    Project project;
    project.setResolution(canvasWidth, canvasHeight);
    project.setFps(qRound(fps));
    project.setSampleRate(static_cast<int>(sampleRate));

    QHash<QString, QStringList> indexedFiles;
    if (!sourceDir.isEmpty()) {
        QDirIterator it(sourceDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int count = 0;
        while (it.hasNext() && count < 10000) {
            it.next();
            ++count;
            indexedFiles[it.fileName().toLower()].append(it.filePath());
        }
    }

    // 1. Parse Media Pool
    struct MediaEntry
    {
        QString rawFilename;
        QString resolvedPath;
        QString assetId;
        QString sequenceId;
        bool isNestedProject = false;
        bool isTextGenerator = false;
        bool isSolidColorGenerator = false;
        QString textContent;
        TextStyle textStyle;
        double textScale = 1.0;
        QPointF textLocation{0.5, 0.5};
        QColor solidColor{0, 0, 0, 255};
        QList<Effect> mediaEffects;
    };
    QMap<quint32, MediaEntry> mediaPool;
    QHash<QString, MediaEntry> nestedEntries;
    qsizetype mediaPoolStart = 0;
    qsizetype mediaPoolEnd = 0;

    qsizetype pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || chunkSize > quint64(totalSize - pos))
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeMediaPool, 16) == 0) {
            qsizetype cpos = pos + 40;
            const qsizetype cend = pos + static_cast<qsizetype>(chunkSize);
            mediaPoolStart = pos;
            mediaPoolEnd = cend;
            while (cpos + 24 <= cend) {
                const uint8_t *cptr = bytes + cpos;
                const quint64 csize = readU64LE(cptr + 16);
                if (csize < 24 || csize > quint64(cend - cpos))
                    break;

                const uint8_t *payload = cptr + 24;
                const qsizetype payloadSize = static_cast<qsizetype>(csize - 24);
                if (payloadSize >= 64) {
                    const quint32 mid = readU32LE(payload + 60);
                    const QString winPath = extractPathFromPayload(payload, payloadSize);

                    const bool isTitles = winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive)
                                          || findUtf16(payload, payloadSize, "titlesandtext") >= 0;
                    const bool isSolid = winPath.contains(QLatin1String("solidcolor"), Qt::CaseInsensitive)
                                         || winPath.contains(QLatin1String("02FD3B60-986B-11D4"), Qt::CaseInsensitive)
                                         || findUtf16(payload, payloadSize, "solidcolor") >= 0;

                    if (isTitles) {
                        MediaEntry entry;
                        entry.isTextGenerator = true;
                        extractTitlesAndText(payload, payloadSize, &entry.textContent, &entry.textStyle,
                                             &entry.textScale, &entry.textLocation);
                        entry.rawFilename = entry.textContent.split(QLatin1Char('\n')).first().trimmed();
                        if (entry.rawFilename.size() > 30)
                            entry.rawFilename = entry.rawFilename.left(30) + QStringLiteral("…");
                        if (entry.rawFilename.isEmpty())
                            entry.rawFilename = QStringLiteral("Title");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += alignedSize(csize);
                        continue;
                    }

                    if (isSolid) {
                        MediaEntry entry;
                        entry.isSolidColorGenerator = true;
                        entry.solidColor = extractSolidColor(payload, payloadSize);
                        entry.rawFilename = QStringLiteral("Solid Color");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += alignedSize(csize);
                        continue;
                    }

                    QString diskName = cleanMediaFilename(winPath);
                    if (diskName.isEmpty() || diskName.startsWith(QLatin1Char('{')))
                        diskName = QStringLiteral("media_%1").arg(mid);

                    const QString resolved = resolveMediaFile(winPath, diskName, sourceDir, indexedFiles);
                    const QString assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);

                    const bool isVeg = resolved.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive);
                    if (isVeg && QFile::exists(resolved)) {
                        const QString canon = QFileInfo(resolved).canonicalFilePath();
                        const auto imported = nestedEntries.constFind(canon);
                        if (imported != nestedEntries.constEnd()) {
                            MediaEntry entry = *imported;
                            entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                            mediaPool.insert(mid, entry);
                            cpos += alignedSize(csize);
                            continue;
                        }

                        const std::shared_ptr<const Project> nested = loadNestedProject(ctx, canon, resolved);
                        if (nested && !nested->tracks().isEmpty()) {
                            const Project &nestedProj = *nested;
                            const QString seqId = project.addSequence(nestedProj.tracks());

                            for (const auto &nAsset : nestedProj.assets()) {
                                if (!project.assets().contains(nAsset.id)) {
                                    project.assets().insert(nAsset.id, nAsset);
                                    project.assetOrder().append(nAsset.id);
                                }
                            }

                            for (const QString &subSeqId : nestedProj.sequenceIds()) {
                                if (!project.hasSequence(subSeqId))
                                    project.addSequence(nestedProj.sequenceTracks(subSeqId), subSeqId);
                            }

                            MediaAsset asset;
                            asset.id = assetId;
                            asset.kind = MediaKind::Composite;
                            asset.sequenceId = seqId;
                            asset.name = QFileInfo(resolved).completeBaseName();
                            asset.path = resolved;
                            asset.durationUs = nestedProj.sequenceDurationUs({});
                            asset.width = nestedProj.width() > 0 ? nestedProj.width() : canvasWidth;
                            asset.height = nestedProj.height() > 0 ? nestedProj.height() : canvasHeight;
                            asset.fps = nestedProj.fps() > 0 ? nestedProj.fps() : fps;
                            asset.editFriendly = true;

                            project.assets().insert(asset.id, asset);
                            project.assetOrder().append(asset.id);

                            MediaEntry entry;
                            entry.rawFilename = diskName;
                            entry.resolvedPath = resolved;
                            entry.assetId = assetId;
                            entry.sequenceId = seqId;
                            entry.isNestedProject = true;
                            nestedEntries.insert(canon, entry);
                            entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                            mediaPool.insert(mid, entry);

                            cpos += alignedSize(csize);
                            continue;
                        }
                        ++ctx.skipped;
                    }

                    MediaEntry entry;
                    entry.rawFilename = diskName;
                    entry.resolvedPath = resolved;
                    entry.assetId = assetId;
                    entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                    mediaPool.insert(mid, entry);

                    MediaAsset asset;
                    asset.id = assetId;
                    asset.path = resolved;
                    asset.name = QFileInfo(resolved).fileName();

                    const QFileInfo resolvedInfo(resolved);
                    if (resolvedInfo.exists() && resolvedInfo.isFile()) {
                        const MediaInfo probed = MediaProbe::probe(resolved);
                        if (probed.ok) {
                            bool hasVideo = false;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    hasVideo = true;
                                    break;
                                }
                            }
                            asset.kind = hasVideo ? MediaKind::Video : MediaKind::Audio;
                            asset.durationUs = probed.durationUs;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    asset.width = st.width;
                                    asset.height = st.height;
                                    asset.fps = st.fps;
                                    asset.codecName = st.codecName;
                                    if (isEditFriendlyCodec(st.codecName)
                                        || resolved.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)) {
                                        asset.editFriendly = true;
                                    }
                                } else if (st.type == StreamInfo::Type::Audio) {
                                    asset.channels = st.channels;
                                    asset.sampleRate = st.sampleRate;
                                    asset.hasAudio = true;
                                    asset.hasAudioKnown = true;
                                }
                            }
                        }
                    } else {
                        const QString ext = resolvedInfo.suffix().toLower();
                        if (ext == QLatin1String("mov") || ext == QLatin1String("mp4")
                            || ext == QLatin1String("mts")
                            || winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive)) {
                            asset.kind = MediaKind::Video;
                            asset.width = canvasWidth;
                            asset.height = canvasHeight;
                            asset.fps = fps;
                            asset.editFriendly = ext == QLatin1String("mov");
                            if (winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive))
                                asset.name = QStringLiteral("Titles & Text");
                        } else {
                            asset.kind = MediaKind::Audio;
                        }
                    }

                    // Extract embedded source markers from media payload if present
                    qsizetype mScan = 0;
                    while (mScan + 16 <= payloadSize) {
                        const void *found = std::memchr(payload + mScan, kGuidMarker[0], payloadSize - mScan);
                        if (!found)
                            break;
                        const qsizetype midx = static_cast<const uint8_t *>(found) - payload;
                        if (midx + 16 > payloadSize)
                            break;
                        if (std::memcmp(payload + midx, kGuidMarker, 16) == 0) {
                            auto optMarker = parseMarkerPayload(payload + midx + 16, payloadSize - (midx + 16));
                            if (optMarker.has_value() && !optMarker->name.isEmpty()
                                && !optMarker->name.startsWith(QLatin1String("Tempo: "))) {
                                SourceMarker sm;
                                sm.id = QString::number(optMarker->id);
                                sm.timeUs = optMarker->posUs;
                                sm.name = optMarker->name;
                                asset.markers.append(sm);
                            }
                        }
                        mScan = midx + 1;
                    }

                    project.assets().insert(asset.id, asset);
                    project.assetOrder().append(asset.id);
                }
                cpos += alignedSize(csize);
            }
            break;
        }
        pos += alignedSize(chunkSize);
    }

    // 2. Parse Event Group List (kTypeEventGroupList)
    QList<QList<quint32>> eventGroups;
    pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || chunkSize > quint64(totalSize - pos))
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeEventGroupList, 16) == 0) {
            qsizetype gpos = pos + 40;
            const qsizetype gend = pos + static_cast<qsizetype>(chunkSize);
            while (gpos + 24 <= gend) {
                const uint8_t *gptr = bytes + gpos;
                const quint64 gsize = readU64LE(gptr + 16);
                if (gsize < 24 || gsize > quint64(gend - gpos))
                    break;

                if (std::memcmp(gptr, kChunkEventGroup, 16) == 0) {
                    const uint8_t *gpayload = gptr + 24;
                    const qsizetype gpayloadSize = static_cast<qsizetype>(gsize - 24);
                    if (gpayloadSize >= 16) {
                        const quint32 eventCount = readU32LE(gpayload + 8);
                        if (eventCount > 0 && quint64(eventCount) * 8 + 16 <= quint64(gpayloadSize)) {
                            QList<quint32> groupEvents;
                            groupEvents.reserve(eventCount);
                            for (quint32 i = 0; i < eventCount; ++i) {
                                const quint32 evId = readU32LE(gpayload + 16 + i * 8);
                                groupEvents.append(evId);
                            }
                            eventGroups.append(groupEvents);
                        }
                    }
                }
                gpos += alignedSize(gsize);
            }
            break;
        }
        pos += alignedSize(chunkSize);
    }

    // 3. Parse Timeline Tracks & Events
    project.tracks().clear();
    QHash<quint32, QPair<int, int>> eventIdToClipLoc;
    pos = 40;
    int trackNumber = 1;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || chunkSize > quint64(totalSize - pos))
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeTimeline, 16) == 0) {
            qsizetype tpos = pos + 40;
            const qsizetype tend = pos + static_cast<qsizetype>(chunkSize);
            while (tpos + 40 <= tend) {
                const quint64 tsize = readU64LE(bytes + tpos + 16);
                if (tsize < 40 || tsize > quint64(tend - tpos))
                    break;

                if (std::memcmp(bytes + tpos, kGuidW64List, 16) == 0
                    && std::memcmp(bytes + tpos + 24, kTypeTrack, 16) == 0) {
                    // Track Header
                    TrackType trackType = TrackType::Video;
                    bool trackMuted = false;
                    bool trackSolo = false;
                    QString trackCustomName;

                    if (tpos + 64 <= tend) {
                        const quint64 tcsize = readU64LE(bytes + tpos + 56);
                        if (tcsize >= 24 && tcsize <= quint64(tend - tpos - 40)) {
                            const uint8_t *cpayload = bytes + tpos + 64;
                            const qsizetype cpayloadSize = static_cast<qsizetype>(tcsize - 24);

                            if (cpayloadSize >= 8) {
                                const quint32 flags = readU32LE(cpayload + 4);
                                trackMuted = (flags & 0x01) != 0;
                                trackSolo = (flags & 0x02) != 0;
                            }
                            if (cpayloadSize >= 20) {
                                const quint32 nameBytes = readU32LE(cpayload + 16);
                                if (nameBytes > 0 && cpayloadSize >= 56 && quint64(nameBytes) <= quint64(cpayloadSize - 56))
                                    trackCustomName = readUtf16String(cpayload + 56, qsizetype(nameBytes), 0).trimmed();
                            }
                            if (cpayloadSize >= 36) {
                                const quint32 trackTypeCode = readU32LE(cpayload + 32);
                                trackType = (trackTypeCode == 2) ? TrackType::Video : TrackType::Audio;
                            }
                        }
                    }

                    Track track;
                    track.type = trackType;
                    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    track.number = trackNumber++;
                    track.muted = trackMuted;
                    track.solo = trackSolo;
                    if (!trackCustomName.isEmpty()) {
                        track.name = trackCustomName;
                    } else {
                        track.name = QStringLiteral("Track %1 (%2)")
                                          .arg(track.number)
                                          .arg(trackType == TrackType::Video ? QStringLiteral("video")
                                                                             : QStringLiteral("audio"));
                    }

                    qsizetype evpos = tpos + 40;
                    const qsizetype evend = tpos + static_cast<qsizetype>(tsize);
                    while (evpos + 40 <= evend) {
                        const quint64 evsize = readU64LE(bytes + evpos + 16);
                        if (evsize < 40 || evsize > quint64(evend - evpos))
                            break;

                        if (std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                            && std::memcmp(bytes + evpos + 24, kTypeEventContainer, 16) == 0) {
                            qsizetype epos = evpos + 40;
                            const qsizetype eend = evpos + static_cast<qsizetype>(evsize);
                            while (epos + 40 <= eend) {
                                const quint64 esize = readU64LE(bytes + epos + 16);
                                if (esize < 40 || esize > quint64(eend - epos))
                                    break;

                                if (std::memcmp(bytes + epos, kGuidW64List, 16) == 0
                                    && std::memcmp(bytes + epos + 24, kTypeEvent, 16) == 0) {
                                    qsizetype cpos = epos + 40;
                                    const qsizetype cend = epos + static_cast<qsizetype>(esize);
                                    quint64 startTicks = 0;
                                    quint64 durTicks = 0;
                                    quint64 fadeInTicks = 0;
                                    quint64 fadeOutTicks = 0;
                                    quint64 srcInTicks = 0;
                                    quint32 mid = 0;
                                    quint32 eventId = 0;

                                    while (cpos + 24 <= cend) {
                                        const uint8_t *cptr = bytes + cpos;
                                        const quint64 csize = readU64LE(cptr + 16);
                                        if (csize < 24 || csize > quint64(cend - cpos))
                                            break;

                                        const uint8_t *cpayload = cptr + 24;
                                        const qsizetype cpayloadSize = static_cast<qsizetype>(csize - 24);

                                        if (std::memcmp(cptr, kChunkEventTime, 16) == 0) {
                                            if (cpayloadSize >= 32) {
                                                startTicks = readU64LE(cpayload + 16);
                                                durTicks = readU64LE(cpayload + 24);
                                            }
                                            if (cpayloadSize >= 68) {
                                                eventId = readU32LE(cpayload + 64);
                                            }
                                            if (cpayloadSize >= 152) {
                                                fadeInTicks = readU64LE(cpayload + 136);
                                                fadeOutTicks = readU64LE(cpayload + 144);
                                            }
                                        } else if (std::memcmp(cptr, kChunkEventTake, 16) == 0) {
                                            if (cpayloadSize >= 40) {
                                                srcInTicks = readU64LE(cpayload + 16);
                                                mid = readU32LE(cpayload + 36);
                                            }
                                        }
                                        cpos += alignedSize(csize);
                                    }

                                    if (durTicks > 0) {
                                        Clip clip;
                                        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

                                        bool isNested = false;
                                        QString nestedSeqId;
                                        if (mediaPool.contains(mid)) {
                                            const MediaEntry &m = mediaPool[mid];
                                            clip.assetId = m.assetId;
                                            clip.path = m.resolvedPath;
                                            clip.name = QFileInfo(m.resolvedPath).fileName();
                                            if (m.isTextGenerator) {
                                                clip.type = ClipType::Text;
                                                clip.name = m.rawFilename.isEmpty() ? QStringLiteral("Title") : m.rawFilename;
                                                clip.textContent = m.textContent;
                                                clip.textStyle = m.textStyle;
                                                clip.assetId.clear();
                                                clip.path.clear();
                                                if (m.textScale > 0.01 && std::abs(m.textScale - 1.0) > 0.01) {
                                                    clip.scaleX.setKeyframe(0, m.textScale);
                                                    clip.scaleY.setKeyframe(0, m.textScale);
                                                }
                                            } else if (m.isSolidColorGenerator) {
                                                clip.type = ClipType::Shape;
                                                clip.name = QStringLiteral("Solid Color");
                                                clip.shapeStyle.kind = ShapeKind::Rectangle;
                                                clip.shapeStyle.setSolidFill(m.solidColor);
                                                clip.shapeStyle.setStroke(0.0);
                                                clip.assetId.clear();
                                                clip.path.clear();
                                            } else if (m.isNestedProject && !m.sequenceId.isEmpty()) {
                                                isNested = true;
                                                nestedSeqId = m.sequenceId;
                                                clip.name = QFileInfo(m.resolvedPath).completeBaseName();
                                            }
                                            for (const Effect &fx : m.mediaEffects) {
                                                clip.effects.append(fx);
                                            }
                                        } else {
                                            clip.name = QStringLiteral("clip_%1").arg(track.clips.size());
                                        }

                                        if (isNested) {
                                            if (trackType == TrackType::Video) {
                                                clip.type = ClipType::Composite;
                                                clip.sequenceId = nestedSeqId;
                                            } else {
                                                clip.type = ClipType::Audio;
                                                clip.sequenceId = nestedSeqId;
                                            }
                                        } else if (clip.type != ClipType::Text && clip.type != ClipType::Shape) {
                                            clip.type = (trackType == TrackType::Video) ? ClipType::Video
                                                                                        : ClipType::Audio;
                                        }

                                        // Convert 100ns ticks to microseconds
                                        clip.timelineStart = ticksToUs(startTicks);
                                        clip.timelineDuration = std::max<TimeUs>(ticksToUs(durTicks), 1);
                                        clip.srcIn = ticksToUs(srcInTicks);
                                        clip.srcOut = clip.srcIn + clip.timelineDuration;
                                        clip.fadeInUs = std::min(ticksToUs(fadeInTicks), clip.timelineDuration);
                                        clip.fadeOutUs = std::min(ticksToUs(fadeOutTicks), clip.timelineDuration);

                                        track.clips.append(clip);
                                        if (eventId > 0) {
                                            eventIdToClipLoc.insert(eventId, qMakePair(project.tracks().size(), track.clips.size() - 1));
                                        }
                                    }
                                }
                                epos += alignedSize(esize);
                            }
                        } else if (evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                                   && std::memcmp(bytes + evpos + 24, kListAudioTrack, 16) == 0) {
                            qsizetype apos = evpos + 40;
                            const qsizetype aend = evpos + static_cast<qsizetype>(evsize);
                            while (apos + 24 <= aend) {
                                const uint8_t *aptr = bytes + apos;
                                const quint64 asize = readU64LE(aptr + 16);
                                if (asize < 24 || asize > quint64(aend - apos))
                                    break;

                                if (std::memcmp(aptr, kChunkAudioTrackProps, 16) == 0) {
                                    const uint8_t *apayload = aptr + 24;
                                    const qsizetype apayloadSize = static_cast<qsizetype>(asize - 24);
                                    if (apayloadSize >= 16) {
                                        const float vol = readFloatLE(apayload + 12);
                                        if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                            track.volume = static_cast<double>(vol);
                                    }
                                    if (apayloadSize >= 20) {
                                        float pan = readFloatLE(apayload + 16);
                                        if (pan == 0.0f && apayloadSize >= 24)
                                            pan = readFloatLE(apayload + 20);
                                        if (!std::isnan(pan) && !std::isinf(pan))
                                            track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                                    }
                                }
                                apos += alignedSize(asize);
                            }
                        } else if (std::memcmp(bytes + evpos, kChunkAudioTrackProps, 16) == 0) {
                            const uint8_t *apayload = bytes + evpos + 24;
                            const qsizetype apayloadSize = static_cast<qsizetype>(evsize - 24);
                            if (apayloadSize >= 16) {
                                const float vol = readFloatLE(apayload + 12);
                                if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                    track.volume = static_cast<double>(vol);
                            }
                            if (apayloadSize >= 20) {
                                float pan = readFloatLE(apayload + 16);
                                if (pan == 0.0f && apayloadSize >= 24)
                                    pan = readFloatLE(apayload + 20);
                                if (!std::isnan(pan) && !std::isinf(pan))
                                    track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                            }
                        }
                        evpos += alignedSize(evsize);
                    }

                    detectTransitionsForTrack(track);

                    if (!track.clips.isEmpty()) {
                        bool allText = true;
                        for (const auto &c : track.clips) {
                            if (c.type != ClipType::Text) {
                                allText = false;
                                break;
                            }
                        }
                        if (allText)
                            track.type = TrackType::Text;
                    }

                    project.tracks().append(track);
                }
                tpos += alignedSize(tsize);
            }
            break;
        }
        pos += alignedSize(chunkSize);
    }

    if (project.tracks().isEmpty()) {
        if (error)
            *error = QObject::tr("No timeline tracks found in the VEGAS Pro project");
        return std::nullopt;
    }

    // Link clips via native VEGAS event groups (kTypeEventGroupList)
    for (const QList<quint32> &group : eventGroups) {
        if (group.size() < 2)
            continue;

        int matchedCount = 0;
        for (quint32 evId : group) {
            if (eventIdToClipLoc.contains(evId))
                ++matchedCount;
        }
        if (matchedCount < 2)
            continue;

        QString commonLinkId;
        bool hasVideo = false;
        bool hasAudio = false;

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                const Clip &c = project.tracks()[loc.first].clips[loc.second];
                if (!c.linkId.isEmpty()) {
                    commonLinkId = c.linkId;
                    break;
                }
            }
        }

        if (commonLinkId.isEmpty()) {
            commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                Clip &c = project.tracks()[loc.first].clips[loc.second];
                c.linkId = commonLinkId;
                if (project.tracks()[loc.first].type == TrackType::Video)
                    hasVideo = true;
                else if (project.tracks()[loc.first].type == TrackType::Audio)
                    hasAudio = true;
            }
        }

        if (hasVideo && hasAudio) {
            for (quint32 evId : group) {
                if (!eventIdToClipLoc.contains(evId))
                    continue;
                const auto loc = eventIdToClipLoc.value(evId);
                if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                    if (project.tracks()[loc.first].type == TrackType::Video) {
                        project.tracks()[loc.first].clips[loc.second].suppressEmbeddedAudio = true;
                    }
                }
            }
        }
    }

    // Fallback: Symmetrically link companion video and audio clips sharing the same asset and timeline span
    for (Track &vTrack : project.tracks()) {
        if (vTrack.type != TrackType::Video)
            continue;
        for (Clip &vClip : vTrack.clips) {
            if (!vClip.linkId.isEmpty() || vClip.assetId.isEmpty())
                continue;
            for (Track &aTrack : project.tracks()) {
                if (aTrack.type != TrackType::Audio)
                    continue;
                for (Clip &aClip : aTrack.clips) {
                    if (!aClip.linkId.isEmpty() || aClip.assetId != vClip.assetId)
                        continue;
                    if (qAbs(aClip.timelineStart - vClip.timelineStart) <= 1000 &&
                        qAbs(aClip.timelineDuration - vClip.timelineDuration) <= 1000) {
                        const QString commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        vClip.linkId = commonLinkId;
                        aClip.linkId = commonLinkId;
                        if (vClip.type == ClipType::Composite)
                            vClip.suppressEmbeddedAudio = true;
                        break;
                    }
                }
                if (!vClip.linkId.isEmpty())
                    break;
            }
        }
    }

    // 4. Parse Timeline Markers and Regions (kTypeMarkerList and kGuidMarker)
    if (ctx.depth == 0) {
        qsizetype markerListStart = -1;
        qsizetype markerListEnd = -1;

        qsizetype lpos = 40;
        while (lpos + 24 <= totalSize) {
            const uint8_t *lptr = bytes + lpos;
            const quint64 chunkSize = readU64LE(lptr + 16);
            if (chunkSize < 24 || chunkSize > quint64(totalSize - lpos))
                break;

            if (std::memcmp(lptr, kGuidW64List, 16) == 0 && lpos + 40 <= totalSize
                && std::memcmp(lptr + 24, kTypeMarkerList, 16) == 0) {
                markerListStart = lpos + 40;
                markerListEnd = lpos + static_cast<qsizetype>(chunkSize);
                break;
            }
            lpos += alignedSize(chunkSize);
        }

        qsizetype scanPos = 40;
        while (scanPos + 16 <= totalSize) {
            const void *found = std::memchr(bytes + scanPos, kGuidMarker[0], totalSize - scanPos);
            if (!found)
                break;
            const qsizetype idx = static_cast<const uint8_t *>(found) - bytes;
            if (idx + 16 > totalSize)
                break;

            if (idx >= mediaPoolStart && idx < mediaPoolEnd) {
                scanPos = mediaPoolEnd;
                continue;
            }

            if (std::memcmp(bytes + idx, kGuidMarker, 16) == 0) {
                const bool inMarkerList = (markerListStart >= 0 && idx >= markerListStart && idx < markerListEnd);
                auto optMarker = parseMarkerPayload(bytes + idx + 16, totalSize - (idx + 16));
                if (optMarker.has_value()) {
                    const ParsedMarker &m = *optMarker;
                    const bool isTempo = m.name.startsWith(QLatin1String("Tempo: "));
                    if (!isTempo && (inMarkerList || !m.name.isEmpty())) {
                        if (m.isRegion) {
                            const QString baseLabel = m.name.isEmpty()
                                ? QStringLiteral("Region %1").arg(m.id)
                                : m.name;
                            Bookmark bmIn;
                            bmIn.timeUs = m.posUs;
                            bmIn.label = baseLabel;
                            project.bookmarks().append(bmIn);

                            if (m.durUs > 0) {
                                Bookmark bmOut;
                                bmOut.timeUs = m.posUs + m.durUs;
                                bmOut.label = QStringLiteral("%1 (Out)").arg(baseLabel);
                                project.bookmarks().append(bmOut);
                            }
                        } else {
                            Bookmark bm;
                            bm.timeUs = m.posUs;
                            bm.label = m.name.isEmpty()
                                ? QStringLiteral("Marker %1").arg(m.id)
                                : m.name;
                            project.bookmarks().append(bm);
                        }
                    }
                }
            }
            scanPos = idx + 1;
        }

        if (!project.bookmarks().isEmpty()) {
            QList<Bookmark> uniqueBookmarks;
            QSet<QString> seen;
            for (const Bookmark &bm : project.bookmarks()) {
                const QString key = QString::number(bm.timeUs) + QLatin1Char('|') + bm.label;
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                uniqueBookmarks.append(bm);
            }
            std::sort(uniqueBookmarks.begin(), uniqueBookmarks.end(),
                      [](const Bookmark &a, const Bookmark &b) {
                          if (a.timeUs != b.timeUs)
                              return a.timeUs < b.timeUs;
                          return a.label < b.label;
                      });
            project.bookmarks() = std::move(uniqueBookmarks);
        }

        for (const QString &seqId : project.sequenceIds()) {
            if (!project.openSequenceTabs().contains(seqId))
                project.openSequenceTabs().append(seqId);
        }
    }

    project.ensureTrackIds();
    return project;
}

} // namespace

std::optional<Project> readProject(const QString &filePath, QString *error, int *skippedOut)
{
    ImportContext ctx;
    auto result = readFileImpl(filePath, error, ctx);
    if (skippedOut)
        *skippedOut = ctx.skipped;
    return result;
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir, QString *error,
                                       int *skippedOut)
{
    ImportContext ctx;
    auto result = readDataImpl(data, sourceDir, error, ctx);
    if (skippedOut)
        *skippedOut = ctx.skipped;
    return result;
}

} // namespace drift::vegas
